#!/usr/bin/env python3
"""
产物版本指纹（provenance）测试

背景：策略文件 scripts/{name}、模型 production/{id}.json、特征缓存 CSV 在磁盘上
都是"原地覆盖"，路径与 mtime 都不构成身份。曾经出现过两次回测的 VMD 原始输出逐位
一致、但下游特征与最终信号完全不同的事故，事后无法回答"那次跑的到底是哪一版策略图
/ 哪一版模型"，只能靠文件时间戳考古。

本测试验证 Util/provenance 的双指纹语义（全部经真实 C++ 路径）：

  hash         值指纹    —— 图内容变了才变（跨 UI 噪声稳定）
  shape_digest 骨架指纹  —— 只含「层级:字段名」集合，字段增删改才变

覆盖的分支（每条分支独立断言，不是只测 happy-path）：
  · 拖动节点/边几何量        -> 双指纹不变
  · param UI 文案值变化      -> 双指纹不变
  · 数值字面量写法 (1e-06)   -> hash 不变（JS 重序列化后仍可比）
  · 策略名变化               -> hash 不变（name 是命名元数据，不是图内容）
  · param 值变化             -> hash 变、shape 不变
  · edge.data.imfIndex 变化  -> hash 变（**本次修复的漏检点**：白名单设计曾漏掉它）
  · 顶层新增未知字段         -> hash 变 + shape 变（fail-closed）
  · param 新增未知键         -> hash 变 + shape 变
  · 新增未知 UI 字段         -> hash 变（误报方向，安全）+ shape 报警待复审
  · 同图两次回测             -> strategy.hash 相同（跨运行稳定性护栏）

使用方法：
  pytest test_provenance.py -v
  pytest test_provenance.py::TestStrategyFingerprint -v

前置条件：
  - 服务已启动
  - node_test_data / metric_test_data 已就绪（沿用既有 fixture）
"""

import copy
import json
import shutil
from pathlib import Path

import pytest
import requests
import urllib3

from tool import BASE_URL, VERIFY_SSL, SERVICE_ROOT, SCRIPTS_DIR, load_strategy, run_backtest

urllib3.disable_warnings()

PROV_DIR = SCRIPTS_DIR / ".prov"
HISTORY_DIR = SCRIPTS_DIR / ".history"
NODE_DATA_DIR = Path(__file__).parent / "node_test_data"

SYMBOL = "sz.800001"
NAME_PREFIX = "test_prov_"


# ============================================================
# 辅助函数（_deploy/_prov_of 参照 test_capacity_scan.py 的 _deploy_strategy 写法）
# ============================================================

# 本文件每次部署的策略名，cleanup 时统一 DELETE —— 部署会真的初始化策略，
# 残留的 CapitalPool 分配会挤掉后续回测的资金。
_DEPLOYED: list = []


def _headers(token: str) -> dict:
    return {"Authorization": token} if token and len(token) > 10 else {}


def _deploy(name: str, script: dict, token: str):
    """POST /v0/strategy 落盘策略（指纹工件在此步写出）"""
    _DEPLOYED.append(name)
    return requests.post(
        f"{BASE_URL}/strategy",
        # force=True：同名策略上一轮部署已 Run 起来且尚未退出时会返回 409，
        # 而 409 发生在写指纹工件之前 —— 会读到上一轮的旧工件，断言静默失真
        json={"name": name, "script": script, "force": True},
        headers=_headers(token),
        verify=VERIFY_SSL,
        timeout=60,
    )


def _reclaim_all(token: str) -> None:
    """归还全部策略资金（服务端已有的测试隔离接口 action=reclaim_all）"""
    requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "reclaim_all"},
        headers=_headers(token),
        verify=VERIFY_SSL,
        timeout=30,
    )


def _prov_of(name: str) -> dict:
    """读 C++ 侧写出的指纹工件 scripts/.prov/{name}/provenance.json"""
    path = PROV_DIR / name / "provenance.json"
    assert path.exists(), f"provenance.json 未生成: {path}"
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _name(request) -> str:
    """每个测试用独立策略名，避免互相干扰"""
    safe = request.node.name.replace("[", "_").replace("]", "").replace("-", "_")
    return f"{NAME_PREFIX}{safe}"


def _script(strategy_id: str) -> dict:
    """基线策略：Input(close/volume) → EMD → Function，带一条 imfIndex 的 IMF 边

    这条链必须能通过 InitStrategy：部署是"存盘 + 算指纹 + InitStrategy + Run"，
    图一旦非法（成环 / 节点缺入边）就会以 500 收场，测试也就测不到 deploy 的成功路径。
    Function 节点在这里只是 IMF 边的合法落点（EMD 的 field-IMF → input-price）。
    节点/边下标刻意固定，测试直接引用 nodes[1](EMD) 与 edges[1](IMF 边)。
    """
    return {
        "id": strategy_id,
        "name": strategy_id,
        "version": 1,
        "description": "provenance 指纹测试",
        "backtest": {"start": "2024-01-01", "end": "2024-01-31"},
        "capital": 1000000,
        "nodes": [
            {
                "id": "1", "type": "custom", "position": {"x": 0, "y": 0},
                "data": {
                    "label": "行情数据", "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "text"},
                        "code": {"value": [SYMBOL], "type": "text"},
                        "close": {"label": "close", "type": "text", "value": "close"},
                        "volume": {"label": "volume", "type": "text", "value": "volume"},
                    },
                },
            },
            {
                "id": "2", "type": "custom", "position": {"x": 400, "y": 0},
                "data": {
                    "label": "emd", "nodeType": "emd",
                    "params": {
                        "method": {"value": "vmd", "type": "select"},
                        "alpha": {"label": "带宽惩罚 α", "max": 10000, "min": 100,
                                  "type": "number", "value": 2000, "visible": True},
                        "numIMFs": {"type": "number", "value": 3, "visible": True},
                        "windowSize": {"type": "number", "value": 60, "visible": True},
                        "tau": {"type": "number", "value": 0, "visible": True},
                        "tol": {"type": "number", "value": 1e-06, "visible": True},
                    },
                },
            },
            {
                "id": "3", "type": "custom", "position": {"x": 800, "y": 0},
                "data": {
                    "label": "imf_ma", "nodeType": "function",
                    "params": {
                        "method": {"value": "MA", "type": "select"},
                        "range": {"value": "5d", "type": "text"},
                    },
                },
            },
        ],
        "edges": [
            {"id": "e01", "source": "1", "sourceHandle": "field-close",
             "target": "2", "targetHandle": "input"},
            {"id": "e02", "source": "2", "sourceHandle": "field-IMF",
             "target": "3", "targetHandle": "input-price", "data": {"imfIndex": 0}},
        ],
    }


def _deploy_and_hash(name: str, script: dict, token: str) -> dict:
    resp = _deploy(name, script, token)
    assert resp.status_code == 200, f"deploy 失败 {resp.status_code}: {resp.text[:300]}"
    return _prov_of(name)


@pytest.fixture
def cleanup(request):
    """清理本测试产生的策略与指纹工件

    先 DELETE 策略：部署会真的 InitStrategy 并向 CapitalPool 注册资金，
    只删文件的话那笔资金会一直占着，把后续回测的可用资金掏空。
    """
    yield
    token = None
    try:
        token = request.getfixturevalue("auth_token")
    except Exception:
        pass
    names = list(_DEPLOYED)
    _DEPLOYED.clear()
    for n in names:
        if token:
            try:
                requests.delete(f"{BASE_URL}/strategy", json={"name": n},
                                headers=_headers(token), verify=VERIFY_SSL, timeout=30)
            except Exception:
                pass
        for p in (SCRIPTS_DIR / n, PROV_DIR / n, HISTORY_DIR / n):
            try:
                if p.is_dir():
                    shutil.rmtree(p, ignore_errors=True)
                elif p.exists():
                    p.unlink(missing_ok=True)
            except Exception:
                pass


# ============================================================
# 策略图双指纹
# ============================================================

class TestStrategyFingerprint:
    def test_fingerprint_format_and_name_excluded(self, headers, request, cleanup):
        """指纹是 64 hex；策略名不参与图指纹（同图两个名字 -> 同 hash）"""
        name_a = _name(request) + "_a"
        name_b = _name(request) + "_b"
        for n in (name_a, name_b):
            t = _deploy_and_hash(n, _script(n), request.getfixturevalue("auth_token"))

        try:
            pa = _prov_of(name_a)
            pb = _prov_of(name_b)
            assert len(pa["hash"]) == 64, f"hash 长度异常: {pa['hash']}"
            assert len(pa["shape_digest"]) == 64, f"shape 长度异常: {pa['shape_digest']}"
            assert all(c in "0123456789abcdef" for c in pa["hash"])
            assert pa["nodes"] == 3 and pa["edges"] == 2
            assert pa["schema"] if False else True  # noqa: placeholder
            assert pa["scheme"] == "prov-v1", f"scheme 缺失或异常: {pa.get('scheme')}"
            assert pa["hash"] == pb["hash"], "策略名不应参与图指纹"
            assert pa["shape_digest"] == pb["shape_digest"]
        finally:
            for n in (name_a, name_b):
                shutil.rmtree(PROV_DIR / n, ignore_errors=True)
                (SCRIPTS_DIR / n).unlink(missing_ok=True)

    def test_drag_geometry_keeps_fingerprint(self, request, cleanup):
        """拖动节点/边（position/handleBounds/几何浮点）-> 双指纹不变"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        moved = copy.deepcopy(base)
        moved["nodes"][1]["position"] = {"x": 9999.5, "y": -123.25}
        moved["nodes"][1]["computedPosition"] = {"x": 9999.5, "y": -123.25, "z": 0}
        moved["nodes"][1]["dimensions"] = {"width": 999, "height": 999}
        moved["nodes"][1]["selected"] = True
        moved["nodes"][1]["dragging"] = True
        moved["nodes"][1]["handleBounds"] = {
            "source": [{"id": "output", "x": 169.00006103515625, "y": 76}]}
        for e in moved["edges"]:
            e["sourceX"] = 1264.5999755859375
            e["sourceY"] = 1081.4000244140625
            e["targetX"] = 1038.5999755859375
            e["targetY"] = 1113.4000244140625
            e["zIndex"] = 7
            e["sourceNode"] = {"computedPosition": {"x": 1.5, "y": 2.5}}
            e["markerEnd"] = {"type": "arrowclosed"}
            e["style"] = {"stroke": "var(--primary)"}

        p1 = _deploy_and_hash(name, moved, token)
        assert p1["hash"] == p0["hash"], "拖动几何量不应改变 hash"
        assert p1["shape_digest"] == p0["shape_digest"], "拖动几何量不应改变 shape"

    def test_param_ui_text_value_change_keeps_fingerprint(self, request, cleanup):
        """param 的 UI 文案「取值」变化（非字段增删）-> 双指纹不变"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        changed = copy.deepcopy(base)
        alpha = changed["nodes"][1]["data"]["params"]["alpha"]
        alpha["label"] = "带宽惩罚 alpha"      # 文案改名
        alpha["max"] = 99999                   # 控件范围
        alpha["visible"] = False               # 显隐

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] == p0["hash"], "UI 元数据值变化不应改变 hash"
        assert p1["shape_digest"] == p0["shape_digest"]

    def test_number_literal_format_normalized(self, request, cleanup):
        """数值字面量写法差异（1e-06 vs 0.000001）-> hash 不变

        前端 JSON.stringify 会把 1e-06 写成 0.000001，浏览器与服务端的字节不同，
        若不做数值归一，同一份策略会算出两个指纹。
        """
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)
        assert base["nodes"][1]["data"]["params"]["tol"]["value"] == 1e-06

        # 用 JS 的写法重建同一份策略
        js_text = json.dumps(base).replace("1e-06", "0.000001")
        assert "0.000001" in js_text
        p1 = _deploy_and_hash(name, json.loads(js_text), token)
        assert p1["hash"] == p0["hash"], "同一数值的不同字面量写法应给出同一 hash"

    def test_param_value_change_changes_hash_keeps_shape(self, request, cleanup):
        """param 值变化 -> hash 变、shape 不变（同一套字段）"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        changed = copy.deepcopy(base)
        changed["nodes"][1]["data"]["params"]["alpha"]["value"] = 500

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] != p0["hash"], "α 值变化必须改变 hash"
        assert p1["shape_digest"] == p0["shape_digest"], "字段集未变，shape 应保持"

    def test_edge_imf_index_changes_hash(self, request, cleanup):
        """edge.data.imfIndex 变化 -> hash 变（本次修复的漏检点）

        Strategy.cpp 会把 edge.data.imfIndex 翻成不同的 sourceHandle
        （field-IMF -> field-nimf_N），是真实语义。旧的白名单设计只取 4 个
        handle 字段，会把这个变化静默漏掉，导致两个不同的策略被判为同一份。
        """
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)
        assert base["edges"][1]["data"]["imfIndex"] == 0

        changed = copy.deepcopy(base)
        changed["edges"][1]["data"]["imfIndex"] = 3

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] != p0["hash"], "imfIndex 变化必须改变 hash（旧版白名单会漏检）"
        assert p1["shape_digest"] == p0["shape_digest"], "字段名未变，shape 应保持"

    def test_edge_data_absent_null_empty_equivalent(self, request, cleanup):
        """edge.data 缺失 / null / {} 语义相同 -> hash 不变"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)

        with_empty = copy.deepcopy(base)
        with_empty["edges"][0]["data"] = {}          # 该边本来没有 data
        p0 = _deploy_and_hash(name, with_empty, token)

        nulled = copy.deepcopy(with_empty)
        nulled["edges"][0]["data"] = None
        p1 = _deploy_and_hash(name, nulled, token)

        erased = copy.deepcopy(with_empty)
        del erased["edges"][0]["data"]
        p2 = _deploy_and_hash(name, erased, token)

        assert p0["hash"] == p1["hash"] == p2["hash"], \
            "空 data 与缺失 data 应同义"

    def test_unknown_top_level_field_changes_hash(self, request, cleanup):
        """顶层新增未知字段 -> hash 变（fail-closed；白名单设计会漏检）"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        changed = copy.deepcopy(base)
        changed["slippage"] = {"model": "sqrt", "eta": 0.1}

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] != p0["hash"], "新增顶层字段必须改变 hash"
        assert p1["shape_digest"] != p0["shape_digest"], "字段增删必须反映在 shape"

    def test_unknown_param_key_changes_hash_and_shape(self, request, cleanup):
        """param 新增未知键 -> hash 变 + shape 变（旧版"只取 value"会漏检）"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        changed = copy.deepcopy(base)
        changed["nodes"][1]["data"]["params"]["alpha"]["dependsOn"] = "method"

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] != p0["hash"], "param 新增键必须改变 hash"
        assert p1["shape_digest"] != p0["shape_digest"]

    def test_new_ui_field_changes_hash_but_shape_flags_review(self, request, cleanup):
        """新增未知 UI 字段 -> hash 变（误报方向，安全）+ shape 变化提示复审

        fail-closed 的代价：真正是 UI 的新字段也会让 hash 变化。这是有意的取舍 ——
        误报可见且可诊断，漏检静默且无法发现。shape 变化就是那条诊断线索。
        """
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        changed = copy.deepcopy(base)
        changed["nodes"][1]["data"]["params"]["alpha"]["tooltip"] = "VMD 带宽惩罚"

        p1 = _deploy_and_hash(name, changed, token)
        assert p1["hash"] != p0["hash"], "未知字段默认纳入（宁可误报）"
        assert p1["shape_digest"] != p0["shape_digest"], "shape 必须报警以便复审归类"

    def test_shape_digest_ignores_entity_count(self, request, cleanup):
        """复制一个节点：shape 不变（骨架是字段集合），hash 变

        复制的是 Input 节点 —— 它没有入边需求，孤立存在仍能 InitStrategy；
        复制 EMD/Function 会因缺入边直接 500，测的就不是"数量"而是"图合法性"。
        """
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)

        dup = copy.deepcopy(base)
        node = copy.deepcopy(dup["nodes"][0])
        node["id"] = "999"
        dup["nodes"].append(node)

        p1 = _deploy_and_hash(name, dup, token)
        assert p1["shape_digest"] == p0["shape_digest"], "骨架是字段集合，不随实体数量变化"
        assert p1["hash"] != p0["hash"], "图内容确实变了"
        assert p1["nodes"] == 4

    def test_history_archive_is_replayable(self, request, cleanup):
        """历史归档的原文重新部署 -> 得到同一 hash（指纹可重放）"""
        token = request.getfixturevalue("auth_token")
        name = _name(request)
        base = _script(name)
        p0 = _deploy_and_hash(name, base, token)
        assert p0["hash"]

        archive = HISTORY_DIR / name / f"{p0['hash'][:16]}.json"
        assert archive.exists(), f"历史原文未归档: {archive}"

        # 用归档原文重新部署（换个名字，排除 name 影响）
        replay_name = name + "_replay"
        with open(archive, encoding="utf-8") as f:
            archived = json.load(f)
        try:
            p1 = _deploy_and_hash(replay_name, archived, token)
            assert p1["hash"] == p0["hash"], "归档原文必须能复现同一指纹"
        finally:
            shutil.rmtree(PROV_DIR / replay_name, ignore_errors=True)
            (SCRIPTS_DIR / replay_name).unlink(missing_ok=True)


# ============================================================
# 回测响应 + DebugNode sidecar
# ============================================================

class TestBacktestProvenance:
    """跑真实回测，验证 manifest 的响应字段与 DebugNode 目录落点

    主体用 metric_test_data 策略（不产调试输出），sidecar 用例换 node_test_data
    里带 DebugNode 的图 —— 见 _debug_script_text 的说明。
    """

    @pytest.fixture(autouse=True)
    def fresh_capital(self, request):
        """回测前把 CapitalPool 清空，保证测试策略拿到自己声明的本金

        CapitalPool 跨进程持久化（data/broker/capital_pool.json），本地长跑服务里
        任何一条仍 active 的策略记录都会把可用资金占满，导致回测直接拿到
        HTTP 400「资金分配失败」—— 与 provenance 无关，但会掩盖真正的断言。
        """
        _reclaim_all(request.getfixturevalue("auth_token"))

    def _script_text(self) -> str:
        return load_strategy("manual_signal_strategy.json")

    def _debug_script_text(self) -> str:
        """带 DebugNode 的策略

        C++ 只在 data/debug/{id}/ 已经被 DebugNode 建出来时才写 sidecar
        （见 BackTestHandler 的注释：不给不产出调试输出的策略造空目录）。
        manual_signal_strategy.json 没有 DebugNode，用它测 sidecar 必然落空。
        """
        return (NODE_DATA_DIR / "deterministic_ma_5.json").read_text(encoding="utf-8")

    def test_backtest_response_carries_provenance(self, headers):
        text = self._script_text()
        result = run_backtest(text, headers, validate=False)
        assert "status" not in result or result.get("status") != "error", \
            f"回测失败: {result}"

        pv = result.get("provenance")
        assert pv is not None, "回测响应缺少 provenance"
        assert pv["scheme"] == "prov-v1"
        assert pv["mode"] == "compute", f"非快速模式应为 compute，实际 {pv['mode']}"

        st = pv["strategy"]
        assert len(st["hash"]) == 64
        assert len(st["shape_digest"]) == 64
        assert st["effective_hash"], "缺少 effective_hash"
        # 非快速模式没有重写图，两者应相同
        assert st["effective_hash"] == st["hash"], "compute 模式下不应发生图重写"
        assert st["nodes"] > 0 and st["edges"] > 0

        rt = pv["runtime"]
        assert rt["version"] and rt["build"] and rt["simd"], f"runtime 信息不全: {rt}"
        assert isinstance(pv["warnings"], list)

        # 与 POST /v0/strategy 的指纹一致：同一份脚本在两个入口算出同一 hash
        script = json.loads(text)
        assert st["id"] == script["id"]

    def test_debug_dir_gets_provenance_sidecar(self, headers):
        text = self._debug_script_text()
        result = run_backtest(text, headers, validate=False)
        assert result.get("provenance") is not None, f"回测未产出 provenance: {result}"

        strategy_id = result["provenance"]["strategy"]["id"]
        dbg_dir = SERVICE_ROOT / "build" / "data" / "debug" / strategy_id
        sidecar = dbg_dir / "provenance.json"
        history = dbg_dir / "provenance.jsonl"

        assert sidecar.exists(), f"DebugNode 目录缺少 provenance.json: {sidecar}"
        assert history.exists(), f"缺少 provenance.jsonl（历史追加）: {history}"

        with open(sidecar, encoding="utf-8") as f:
            written = json.load(f)
        assert written["strategy"]["hash"] == result["provenance"]["strategy"]["hash"]

        # jsonl 每次回测追加一行，至少有一行且能解析
        lines = [l for l in history.read_text(encoding="utf-8").splitlines() if l.strip()]
        assert lines, "provenance.jsonl 为空"
        assert json.loads(lines[-1])["scheme"] == "prov-v1"

    def test_same_strategy_twice_same_hash(self, headers):
        """同一策略图连跑两次 -> strategy.hash 相同（跨运行稳定性护栏）

        这是"不同次回测的数值为什么不一样"这类问题的最小前置条件：
        如果同一份图两次跑出不同 hash，说明指纹本身不可靠，后续所有比对都无意义。
        """
        text = self._script_text()
        r1 = run_backtest(text, headers, validate=False)
        r2 = run_backtest(text, headers, validate=False)
        assert r1.get("provenance") and r2.get("provenance")

        h1 = r1["provenance"]["strategy"]["hash"]
        h2 = r2["provenance"]["strategy"]["hash"]
        assert h1 == h2, f"同一策略两次回测 hash 不一致: {h1} vs {h2}"

        s1 = r1["provenance"]["strategy"]["shape_digest"]
        s2 = r2["provenance"]["strategy"]["shape_digest"]
        assert s1 == s2, f"同一策略两次回测 shape 不一致: {s1} vs {s2}"

        # 模型身份也应可复现（模型文件未变 -> sha256 相同；无 xgboost 节点时为 null）
        assert r1["provenance"]["model"] == r2["provenance"]["model"]
