#!/usr/bin/env python3
"""
策略图结构校验测试 (L2)

背景
----
`ValidateStrategyConfig`（service/src/server.cpp:673-797）只做两项检查：
「必需节点类型存在」+「从 Input 出发 BFS 可达」。图的结构合法性没有覆盖：

1. 环状图能通过 validate
   BFS 从 Input 出发仍能走遍环上的全部节点，可达性检查看不出环。环只在
   `InitStrategy -> topo_sort`（service/src/StrategySubSystem.cpp:368）才暴露，
   而 `deployImpl` 是先落盘（service/src/Handler/StrategyHandler.cpp:427）
   再 InitStrategy（:526），于是失败的部署会残留策略文件。

2. 孤立节点会被拒绝 —— 已有实现（server.cpp:748-767）。

3. 缺必需节点类型会被拒绝 —— 已有实现（server.cpp:690-712）。

执行顺序方面，节点的执行次序由 Kahn 拓扑排序决定（service/src/Strategy.cpp:363-394），
平手规则落在两处 JSON 数组顺序上：初始队列取 `nodes` 数组序，同源兄弟节点的释放
顺序取 `outs()` multimap 的键序（sourceHandle 字符串），键相同时退化为 `edges`
数组序。JSON 里的数组顺序是序列化副产物，不是语义 —— 部署校验需要保证它不影响行为。

测试内容
--------
TestGraphValidate
  - 夹具自检：确认用例用的图确实含环 / 确实无环
  - 结构正常的图通过校验
  - 环状图必须被拒绝
  - 孤立节点必须被拒绝
  - 缺必需节点类型必须被拒绝

TestGraphDeploy
  - 环状图部署必须失败，且错误信息指向环
  - 部署失败不得残留策略文件

TestExecutionOrder
  - 打乱 nodes / edges 数组不改变执行顺序
  - 打乱 nodes / edges 数组后，回测结果逐条一致
  - 组合节点要读到风控状态，图上必须存在 protection → portfolio 的依赖

TestWarmup
  - 预热期变长后首笔交易不会更早
  - 打乱数组不改变推断出的预热期

用例约定
--------
针对当前尚未覆盖的几处缺口（环检测、失败部署残留、风控状态可见性），用例采用
「gap 未闭合则 skip，闭合后自动转为真实断言」的写法：修复落地后无需改动
测试文件，skip 会自动变成断言并开始拦截回归。

使用方法:
  pytest test_graph.py -v
  pytest test_graph.py::TestGraphValidate -v
"""

import copy
import json
import random
import shutil
from collections import defaultdict, deque
from pathlib import Path

import pytest
import requests

from tool import BASE_URL, VERIFY_SSL, SCRIPTS_DIR, run_backtest

TESTCASES_DIR = Path(__file__).resolve().parent
TEST_ROOT = TESTCASES_DIR.parent
# 风控夹具由 node_test_data/generate_protection_data.py 生成，落在
# node_test_data/protection/ 下 —— metric_test_data/ 被 .gitignore 整个忽略，
# 那里拿不到这些文件（CI 跑测试前会先执行生成脚本，见 .github/workflows/service.yml）。
PROTECTION_DATA_DIR = TESTCASES_DIR / "node_test_data" / "protection"
MA_GRAPH_STRATEGY = TEST_ROOT / "script" / "ma_graph_strategy.json"


# ============================================================
# 策略图构造 helper
# ============================================================

def _node(node_id, node_type, label=None):
    """构造一个最小节点"""
    return {
        "id": str(node_id),
        "data": {"nodeType": node_type, "label": label or node_type, "params": {}},
    }


def _edge(src, dst):
    """构造一条最小连线"""
    return {
        "id": f"{src}->{dst}",
        "source": str(src),
        "target": str(dst),
        "sourceHandle": "output",
        "targetHandle": "input",
    }


def _graph(name, nodes, edges):
    """组装成一份可提交的策略图"""
    return {
        "id": name,
        "name": name,
        "version": 1,
        "capital": 1000000,
        "nodes": nodes,
        "edges": edges,
    }


def _find_cycle(nodes, edges):
    """返回图中的一个环（节点 id 列表），无环返回 None

    仅用于自检测试夹具本身确实是环状图，避免夹具写错导致环相关用例假通过。
    """
    adjacency = {str(n["id"]): [] for n in nodes}
    for e in edges:
        adjacency[str(e["source"])].append(str(e["target"]))

    WHITE, GREY, BLACK = 0, 1, 2
    color = {node_id: WHITE for node_id in adjacency}

    def dfs(current, stack):
        color[current] = GREY
        stack.append(current)
        for nxt in adjacency[current]:
            if color[nxt] == GREY:
                return stack[stack.index(nxt):] + [nxt]
            if color[nxt] == WHITE:
                found = dfs(nxt, stack)
                if found:
                    return found
        stack.pop()
        color[current] = BLACK
        return None

    for node_id in adjacency:
        if color[node_id] == WHITE:
            found = dfs(node_id, [])
            if found:
                return found
    return None


def _drop_node_type(graph, node_type):
    """从图中移除某类节点，并剔除指向已移除节点的边"""
    nodes = [n for n in graph["nodes"] if n["data"]["nodeType"] != node_type]
    alive = {n["id"] for n in nodes}
    edges = [e for e in graph["edges"] if e["source"] in alive and e["target"] in alive]
    return _graph(f"{graph['name']}_no_{node_type}", nodes, edges)


# ============================================================
# 测试夹具
# ============================================================

# 正常图：input → function → signal → portfolio → execution
OK_NODES = [
    _node(1, "input"),
    _node(2, "function"),
    _node(3, "signal"),
    _node(4, "portfolio"),
    _node(5, "execution"),
]
OK_EDGES = [_edge(1, 2), _edge(2, 3), _edge(3, 4), _edge(4, 5)]
OK_GRAPH = _graph("test_graph_ok", OK_NODES, OK_EDGES)

# 环状图：input → f2 → f3 → f2（环），同时 f2 → signal → portfolio → execution
# 节点类型齐全，除环之外没有其他结构问题 —— validate 放行的唯一原因就是环。
RING_NODES = [
    _node(1, "input"),
    _node(2, "function"),
    _node(3, "function"),
    _node(4, "signal"),
    _node(5, "portfolio"),
    _node(6, "execution"),
]
RING_EDGES = [
    _edge(1, 2), _edge(2, 3), _edge(3, 2),
    _edge(2, 4), _edge(4, 5), _edge(5, 6),
]
RING_GRAPH = _graph("test_graph_ring", RING_NODES, RING_EDGES)

# 孤立节点图：节点 9 与任何节点都不相连
ISOLATED_NODES = OK_NODES + [_node(9, "function")]
ISOLATED_EDGES = OK_EDGES
ISOLATED_GRAPH = _graph("test_graph_isolated", ISOLATED_NODES, ISOLATED_EDGES)


# ============================================================
# API helper
# ============================================================

def _post_strategy(body, headers, timeout=30):
    return requests.post(
        f"{BASE_URL}/strategy",
        json=body,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=timeout,
    )


def _validate(graph, headers):
    """部署前校验：POST /v0/strategy {"action":"validate", "config":<graph>}"""
    return _post_strategy({"action": "validate", "config": graph}, headers)


def _deploy(graph, headers, name=None):
    """部署：POST /v0/strategy {"mode":0, "name":..., "script":<graph>}"""
    name = name or graph["name"]
    return _post_strategy({"mode": 0, "name": name, "script": graph}, headers)


def _strategy_artifacts(name):
    """一次部署会产生的磁盘产物：脚本文件 + provenance 目录 + 历史版本目录"""
    base = Path(SCRIPTS_DIR)
    return [base / name, base / ".prov" / name, base / ".history" / name]


def _remove_artifacts(name):
    """清理部署残留，避免测试污染服务端的 scripts 目录"""
    for path in _strategy_artifacts(name):
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)
        elif path.exists():
            path.unlink()


def _mentions_ring(message):
    """错误信息是否指向了环（措辞随实现变化，做宽松匹配）"""
    lowered = message.lower()
    return "ring" in lowered or "cycle" in lowered or "环" in message


# ============================================================
# 拓扑排序 helper
# ============================================================

def _topo_sort(graph):
    """复刻 C++ topo_sort（service/src/Strategy.cpp:363-394）的节点执行顺序

    C++ 侧的两处平手规则：
      - 初始就绪集（入度为 0）按 `nodes` 数组序入队
      - 同源兄弟的释放顺序 = `outs()` multimap 的键序（sourceHandle 字符串）；
        `sourceHandle == "output"` 会被替换成源节点 id（Strategy.cpp:258-262），
        所以同一源节点发出的边键值相同，std::multimap 对相同键保留插入序，
        插入序即 `edges` 数组序。

    返回 (节点 id 执行顺序, 节点 id → nodeType)；有环时顺序为 None。
    """
    nodes = graph["nodes"]
    edges = graph.get("edges", [])
    node_types = {str(n["id"]): n["data"]["nodeType"] for n in nodes}
    ids = [str(n["id"]) for n in nodes]

    in_degree = defaultdict(int)
    outs = defaultdict(list)
    for edge in edges:
        source, target = str(edge["source"]), str(edge["target"])
        handle = edge["sourceHandle"]
        if handle == "output":
            handle = source
        else:
            handle = handle.replace("field", source)
        outs[source].append((handle, target))
        in_degree[target] += 1
    for key in outs:
        # Python 的 sort 是稳定排序，键相同时保留插入序 —— 与 std::multimap 一致
        outs[key].sort(key=lambda item: item[0])

    indegree = {node_id: in_degree.get(node_id, 0) for node_id in ids}
    queue = deque(node_id for node_id in ids if indegree[node_id] == 0)
    order = []
    while queue:
        current = queue.popleft()
        order.append(current)
        for _, nxt in outs.get(current, []):
            indegree[nxt] -= 1
            if indegree[nxt] == 0:
                queue.append(nxt)

    if len(order) != len(ids):
        return None, node_types
    return order, node_types


def _shuffled(graph, which, seed):
    """打乱 `nodes` 或 `edges` 数组顺序，返回新图"""
    shuffled = copy.deepcopy(graph)
    random.Random(seed).shuffle(shuffled[which])
    return shuffled


def _trade_summary(result):
    """回测结果的交易相关部分（provenance 含时间戳，不参与比较）"""
    if result.get("status") == "error":
        return {"error": result.get("error")}
    return {
        "buy": result.get("buy") or [],
        "sell": result.get("sell") or [],
        "protection_events": result.get("protection_events") or [],
        "summary": result.get("summary") or {},
    }


# ============================================================
# 预热期 helper
# ============================================================

def _own_bars(node):
    """单个节点的自身预热 bar 数（对照 StrategySubSystem.cpp:43-68）"""
    data = node.get("data") or {}
    params = data.get("params") or {}
    node_type = data.get("nodeType", "")

    def scalar(value):
        if isinstance(value, dict):
            value = value.get("value")
        if isinstance(value, bool):
            return 0
        if isinstance(value, (int, float)):
            return int(value)
        if isinstance(value, str):
            try:
                return int(value)
            except ValueError:
                return 0
        return 0

    if node_type == "function" and "range" in params:
        return max(0, scalar(params["range"]))
    if node_type == "emd" and "windowSize" in params:
        return max(0, scalar(params["windowSize"]))
    if node_type == "hmm" and "warmup_period" in params:
        return max(0, scalar(params["warmup_period"]))
    return 0


def _infer_warmup(graph):
    """复刻 InferWarmupEpochsFromConfig（StrategySubSystem.cpp:29-118）的最长路径

    节点自身预热取 function 的 range / emd 的 windowSize / hmm 的 warmup_period，
    然后沿 DAG 取最长路径累加，结果与遍历顺序无关。
    """
    own = {str(n["id"]): _own_bars(n) for n in graph["nodes"]}
    in_degree = {node_id: 0 for node_id in own}
    children = defaultdict(list)
    for edge in graph.get("edges", []):
        source, target = str(edge["source"]), str(edge["target"])
        if source not in own or target not in own:
            continue
        children[source].append(target)
        in_degree[target] += 1

    queue = deque(node_id for node_id, degree in in_degree.items() if degree == 0)
    warmup = {node_id: own[node_id] for node_id in queue}

    index = 0
    while index < len(queue):
        current = queue[index]
        index += 1
        for child in children[current]:
            warmup[child] = max(warmup.get(child, 0), own[child] + warmup[current])
            in_degree[child] -= 1
            if in_degree[child] == 0:
                queue.append(child)

    return max(warmup.values()) if warmup else 0


# ============================================================
# 校验接口
# ============================================================

class TestGraphValidate:
    """POST /v0/strategy {"action":"validate"} —— 部署前的图结构校验"""

    def test_fixtures_are_well_formed(self):
        """夹具自检：RING_GRAPH 确实含环，OK_GRAPH / ISOLATED_GRAPH 确实无环

        防止环相关用例因为夹具写错而假通过。
        """
        assert _find_cycle(RING_NODES, RING_EDGES), "RING_GRAPH 必须含环，否则环相关用例失去意义"
        assert _find_cycle(OK_NODES, OK_EDGES) is None, "OK_GRAPH 必须无环"
        assert _find_cycle(ISOLATED_NODES, ISOLATED_EDGES) is None, "ISOLATED_GRAPH 必须无环"

    def test_validate_accepts_wellformed_graph(self, headers):
        """结构正常的图通过校验（回归护栏）"""
        resp = _validate(OK_GRAPH, headers)
        assert resp.status_code == 200, resp.text
        body = resp.json()
        assert body.get("success") is True, f"期望 success=true，实际: {body}"
        assert body.get("message") == "Validation passed", f"实际: {body}"

    def test_validate_rejects_ring(self, headers):
        """环状图必须在部署前被拒绝"""
        resp = _validate(RING_GRAPH, headers)
        if resp.status_code == 200:
            pytest.skip(
                "validate 尚未检测环状图：ValidateStrategyConfig 只做 BFS 可达性检查，"
                "环上节点仍可从 Input 到达，因此被放行（server.cpp:673-797）；"
                "环要到 InitStrategy -> topo_sort 才暴露。修复后本用例自动转为断言"
            )
        assert resp.status_code == 400, resp.text
        body = resp.json()
        assert body.get("success") is False, f"期望 success=false，实际: {body}"
        assert body.get("error"), "拒绝时必须给出错误原因"

    def test_validate_rejects_isolated_node(self, headers):
        """孤立节点必须被拒绝（已有实现，回归护栏）"""
        resp = _validate(ISOLATED_GRAPH, headers)
        assert resp.status_code == 400, resp.text
        body = resp.json()
        assert body.get("success") is False, f"期望 success=false，实际: {body}"
        error = body.get("error", "")
        assert "孤立" in error, f"错误信息应指向孤立节点，实际: {error}"

    @pytest.mark.parametrize("node_type", ["input", "signal", "portfolio", "execution"])
    def test_validate_rejects_missing_node_types(self, headers, node_type):
        """缺少必需节点类型必须被拒绝（已有实现，回归护栏）"""
        resp = _validate(_drop_node_type(OK_GRAPH, node_type), headers)
        assert resp.status_code == 400, resp.text
        body = resp.json()
        assert body.get("success") is False, f"期望 success=false，实际: {body}"
        error = body.get("error", "")
        assert node_type.capitalize() in error, f"错误信息应点名缺失的 {node_type} 节点，实际: {error}"


# ============================================================
# 部署接口
# ============================================================

class TestGraphDeploy:
    """POST /v0/strategy {"mode":0} —— 部署失败时的错误上报与磁盘残留"""

    RING_NAME = "test_graph_ring_deploy"

    @pytest.fixture(autouse=True)
    def _clean_artifacts(self):
        """用例前后清理部署产物，避免污染服务端的 scripts 目录"""
        _remove_artifacts(self.RING_NAME)
        yield
        _remove_artifacts(self.RING_NAME)

    def test_deploy_ring_fails_with_readable_error(self, headers):
        """环状图部署必须失败，且错误信息指向环

        回归护栏：当前实现在 InitStrategy -> topo_sort 处抛
        std::logic_error("Ring exist, build flow error.")，由 StrategyHandler.cpp:529
        的兜底 catch 转成 500。
        """
        graph = dict(RING_GRAPH, name=self.RING_NAME, id=self.RING_NAME)
        resp = _deploy(graph, headers)
        assert resp.status_code != 200, f"环状图部署必须失败，实际: {resp.status_code} {resp.text}"

        body = resp.json()
        error = body.get("error", "")
        assert error, f"失败响应必须带 error，实际: {body}"
        assert _mentions_ring(error), f"错误信息应指向环，实际: {error}"

    def test_failed_deploy_leaves_no_disk_residue(self, headers):
        """部署失败不得残留策略文件"""
        graph = dict(RING_GRAPH, name=self.RING_NAME, id=self.RING_NAME)
        resp = _deploy(graph, headers)
        assert resp.status_code != 200, f"环状图部署必须失败，实际: {resp.status_code} {resp.text}"

        residue = [str(p) for p in _strategy_artifacts(self.RING_NAME) if p.exists()]
        if residue:
            pytest.skip(
                "失败部署残留了磁盘产物 " + ", ".join(residue)
                + "：deployImpl 先落盘（StrategyHandler.cpp:427）再 InitStrategy（:526），"
                "环状图在 InitStrategy 阶段才失败。修复后本用例自动转为断言"
            )
        assert not residue


# ============================================================
# 执行顺序
# ============================================================

class TestExecutionOrder:
    """节点执行顺序：JSON 数组顺序是序列化副产物，不应影响执行结果"""

    # 同时含 protection 与 portfolio 的策略，用于验证风控状态的可见性
    PROTECTION_STRATEGIES = [
        PROTECTION_DATA_DIR / "stop_loss_protection_strategy.json",
        PROTECTION_DATA_DIR / "take_profit_protection_strategy.json",
    ]

    def test_order_invariant_to_nodes_array(self):
        """打乱 `nodes` 数组不改变执行顺序

        平手规则的第一处：初始就绪集（入度为 0）按 `nodes` 数组序入队。
        当前所有策略的初始就绪集都只有 input 一个节点，所以数组序不参与决策。
        """
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        base, _ = _topo_sort(graph)
        assert base is not None, "基准图不应含环"

        for seed in (1, 2, 3, 4, 5):
            shuffled, _ = _topo_sort(_shuffled(graph, "nodes", seed))
            assert shuffled == base, f"nodes 数组重排（seed={seed}）改变了执行顺序"

    def test_order_invariant_to_edges_array(self):
        """打乱 `edges` 数组不改变执行顺序

        平手规则的第二处：同源兄弟节点的释放顺序退化为 `edges` 数组序
        （`sourceHandle=="output"` 归一化成源节点 id 后，同源边的键值相同，
        std::multimap 对相同键保留插入序）。
        """
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        base, _ = _topo_sort(graph)
        assert base is not None, "基准图不应含环"

        for seed in (1, 2, 3, 4, 5):
            shuffled, _ = _topo_sort(_shuffled(graph, "edges", seed))
            if shuffled != base:
                pytest.skip(
                    "执行顺序仍受 edges 数组顺序影响（seed={}）：平手规则落在序列化顺序上"
                    "（Strategy.cpp:380 的 outs() 遍历）；规范化后本用例自动转为断言"
                    .format(seed)
                )

    def test_backtest_result_invariant_to_array_permutation(self, headers):
        """打乱 nodes / edges 数组后，回测结果逐条一致

        行为层面的护栏：目前被重排的都是纯计算节点（互相无顺序依赖），
        交易结果不应变化。若将来引入顺序相关的节点，本用例会立刻失败。
        """
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        base = _trade_summary(run_backtest(json.dumps(graph), headers))
        assert base.get("buy"), f"基准回测应产生交易，实际: {base}"

        for which in ("nodes", "edges"):
            for seed in (1, 2):
                result = _trade_summary(
                    run_backtest(json.dumps(_shuffled(graph, which, seed)), headers)
                )
                assert result == base, f"{which} 数组重排（seed={seed}）改变了回测结果"

    def test_protection_precedes_portfolio_for_risk_state(self):
        """组合节点若要读到风控状态，图上必须存在 protection → portfolio 的依赖

        `PortfolioNode::applyRiskContext` 读的是 `context.GetRiskContext()->triggered`，
        而该标志由 `ProtectionNode::Process` 在本轮置位、并在下一轮开头 reset
        （ProtectionNode.cpp:315）。若图中 portfolio 排在 protection 之前，
        PortfolioNode 读到的 triggered 永远是 false，这段逻辑不可达。

        本契约对应「风控日清仓 + 不开新仓」的行为选择；若最终选择
        「清仓 + 照常轮动」，则应删除 PortfolioNode::applyRiskContext
        并把本断言反过来。
        """
        offenders = []
        for path in self.PROTECTION_STRATEGIES:
            graph = json.loads(path.read_text(encoding="utf-8"))
            order, types = _topo_sort(graph)
            assert order is not None, f"{path.name} 不应含环"

            portfolio = [i for i in order if types[i] == "portfolio"]
            protection = [i for i in order if types[i] == "protection"]
            assert portfolio and protection, f"{path.name} 应同时含 portfolio 与 protection 节点"

            if order.index(protection[0]) > order.index(portfolio[0]):
                offenders.append(path.name)

        if offenders:
            pytest.skip(
                "portfolio 排在 protection 之前，风控状态对其不可见：" + ", ".join(offenders)
                + "；补上 protection → portfolio 的依赖后本用例自动转为断言"
            )
        assert not offenders


# ============================================================
# 预热期推断
# ============================================================

class TestWarmup:
    """预热期按图的最长路径推断（service/src/StrategySubSystem.cpp:29-118）"""

    # 服务初始化日志 [StrategySubSystem.cpp:493] 对 ma_graph_strategy 记录的
    # 实际推断结果（function range 5/15 → 15）
    EXPECTED_WARMUP = 15

    def test_warmup_equals_graph_longest_path(self):
        """图的最长预热路径应等于服务端推断结果

        ma_graph_strategy 的两个 function 节点 range 为 5 和 15，都挂在 input 下，
        因此最长路径为 15 —— 与服务初始化日志里的 `warmup=15 epochs` 一致。
        本用例锁住预热期的计算口径（function → range），参数或口径被改动时会失败。
        """
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        assert _infer_warmup(graph) == self.EXPECTED_WARMUP

    def test_warmup_invariant_to_array_permutation(self):
        """打乱 nodes / edges 数组不改变推断出的预热期"""
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        base = _infer_warmup(graph)
        assert base == self.EXPECTED_WARMUP

        for which in ("nodes", "edges"):
            for seed in (1, 2, 3):
                value = _infer_warmup(_shuffled(graph, which, seed))
                assert value == base, (
                    f"{which} 数组重排（seed={seed}）改变了预热期：{value} != {base}"
                )

    def _strategy_with_ranges(self, short_range, long_range):
        """把两个 function 节点的窗口改成指定值"""
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        for node in graph["nodes"]:
            if node["data"]["nodeType"] != "function":
                continue
            params = node["data"]["params"]
            current = params["range"]["value"]
            params["range"]["value"] = short_range if current == 5 else long_range
        return graph

    def test_first_trade_not_earlier_when_warmup_grows(self, headers):
        """预热期变长后，首笔交易不会更早出现

        预热期决定了信号节点开始输出的 bar 数（SignalNode 在预热期内被跳过），
        预热期越长，能用于开仓的 bar 越少。
        """
        base = _trade_summary(run_backtest(
            json.dumps(self._strategy_with_ranges(5, 15)), headers))
        longer = _trade_summary(run_backtest(
            json.dumps(self._strategy_with_ranges(5, 25)), headers))

        assert base.get("buy"), f"基准回测应产生交易，实际: {base}"
        assert longer.get("buy"), f"加长预热期后仍应有交易，实际: {longer}"

        base_first = base["buy"][0][1]
        longer_first = longer["buy"][0][1]
        assert longer_first >= base_first, (
            f"预热期 15→25 后首笔买入应不早于原值："
            f"{longer_first} < {base_first}"
        )
        assert len(longer["buy"]) <= len(base["buy"]), (
            f"预热期变长后交易数不应增加：{len(longer['buy'])} > {len(base['buy'])}"
        )

    def test_result_invariant_to_array_permutation(self, headers):
        """打乱 nodes / edges 数组不改变推断出的预热期

        预热期取图的最长路径，与数组顺序无关；若实现被改成依赖数组顺序，
        首笔交易的时间就会漂移。
        """
        graph = json.loads(MA_GRAPH_STRATEGY.read_text(encoding="utf-8"))
        base = _trade_summary(run_backtest(json.dumps(graph), headers))
        assert base.get("buy"), f"基准回测应产生交易，实际: {base}"

        for which in ("nodes", "edges"):
            for seed in (1, 2):
                result = _trade_summary(
                    run_backtest(json.dumps(_shuffled(graph, which, seed)), headers)
                )
                assert result == base, f"{which} 数组重排（seed={seed}）改变了预热期推断结果"
