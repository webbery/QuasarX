"""
双策略并发运行集成测试（Backtest 模式 · 推 bar 驱动）

参考 test_decision.py 的做法：策略用 **Manual（决策型）执行节点**，
由 POST /v0/strategy/simulate/bar 逐根推送 bar 驱动日终回放，
策略产出决策（decision）落到 DuckDB，再按策略名取回来比对。

验证两件事：
1. 两个策略**同时跑**时，各自产出的决策与**单独跑**时是否一致
   —— 单独跑一遍留基线，再让两个策略交替推 bar 并发跑，逐条比对
      action / quantity / price / epoch 序列。
2. 总账户金额是否算错
   —— 每个策略在账本里是独立账户、分配额只加一次、available/used/allocated 自洽。

为什么用"推 bar + 决策"而不是"部署后自动回测 + Debug CSV"：
- 部署后自动回测是一次性的，两个策略的生命周期很短，很难确定性地让它们重叠；
- simulate/bar 把节奏握在测试手里，可以一根一根推、也可以两个策略交替推，
  "同时运行"是可控可复现的；
- 决策是结构化数据（action/quantity/price/epoch/strategy/symbol），
  比 CSV 更适合做逐条比对。

引擎事实（决定了本用例怎么写）：
- 带 Manual 执行节点的策略不走回测/实时 worker，由日终管线驱动
  （AgentSubSystem.cpp:241 的 HasManualExecuteNode 分支）。
- simulate/bar 写 bar 进 DuckDB 后调 MarkSymbolReady，某策略的标的全部就绪时
  该策略整体重放一遍历史并产出决策（StrategySubSystem.cpp:688-740）。
- 每条推送会把回放窗口截断到推送日，所以第 N 根 bar 推完得到的就是
  "截至第 N 天的决策"。日期相同则回放窗口相同 —— 这是可比对的前提。
- 决策归属日取日终回放的 as-of 日期（BrokerSubSystem.cpp:1293），
  所以按推送日期能查到对应决策。
"""
import datetime
import json
import time
import uuid
from pathlib import Path

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import (
    BASE_URL,
    VERIFY_SSL,
    api_cleanup_strategy,
    api_get_decisions,
    api_load_strategy,
    api_run_strategy,
    reclaim_capital,
)

# ==================== 配置 ====================

# 复用仓库现成的 MA 策略图（FunctionNode 双均线 + SignalNode 金叉死叉）
FIXTURE_PATH = Path(__file__).parent.parent / "script" / "ma_graph_strategy.json"

# 两个策略跑在不同标的上。用全新的合成代码，保证 DuckDB 里没有历史残留，
# 回放窗口完全由本用例推送的 bar 决定。
SYMBOL_A = "sz.900931"
SYMBOL_B = "sz.900932"

# 两套参数故意不同：顺带验证两个实例的节点参数不会互相串
MA_A = {"ma_short": 5, "ma_long": 15}
MA_B = {"ma_short": 3, "ma_long": 8}

# 两个策略的资金额度故意不同：总额断言才能区分出谁是谁
CAPITAL_A = 500000
CAPITAL_B = 300000

# 回放的起始日与 bar 根数。V 形价格（先跌后涨）保证一定出现金叉、一定产出决策。
START_DATE = datetime.date(2027, 1, 1)
BAR_COUNT = 24
# 逐根推送的间隔：给日终回放留出启动时间，但不等于"等它跑完"，
# 这样两个策略的回放窗口才有机会真正重叠。
#
# 0.5s 是权衡出来的：推得太密（0.25s）时两个回放 + DuckDB 写入会挤在一起，
# 连 GET /trade/decisions 都会超时；太稀则两个策略的回放不再重叠，测不到并发。
PUSH_INTERVAL = 0.5

# 本模块创建过的策略名，统一在 teardown 里清理
_CREATED_NAMES = []


# ==================== 工具函数 ====================

def _headers(auth_token):
    return {"Authorization": auth_token} if auth_token else {}


def _unique_name(tag):
    """生成唯一策略名。

    必须唯一：账本 Allocate 只在首次建户时生效（StrategySubSystem.cpp:453），
    同名复用会拿到上一次遗留的余额，资金断言会假失败。
    """
    name = f"test_ms_{tag}_{uuid.uuid4().hex[:10]}"
    _CREATED_NAMES.append(name)
    return name


def _build_strategy(name, symbol, ma_short, ma_long, capital):
    """基于 ma_graph_strategy.json 构造 Manual（决策型）策略图。

    相对夹具改写四处：id/name、capital、input+signal 节点的标的、两个 MA 窗口；
    并把 ExecutionNode 的 type 改成 7（ExecuteType::Manual），
    让策略只产决策不下单，从而可以被 simulate/bar 驱动。
    """
    with open(FIXTURE_PATH, "r", encoding="utf-8") as f:
        script = json.load(f)

    script["id"] = name
    script["name"] = name
    script["capital"] = capital

    for node in script["nodes"]:
        params = node["data"]["params"]
        node_type = node["data"]["nodeType"]
        label = node["data"].get("label")
        if node_type in ("input", "signal"):
            params["code"]["value"] = [symbol]
        elif label == "ma_short":
            params["range"]["value"] = ma_short
        elif label == "ma_long":
            params["range"]["value"] = ma_long
        elif node_type == "execution":
            # ExecuteType 枚举：ImmediatlyMarket=0 ... Manual=7
            params["type"]["value"] = 7

    return script


def _deploy(auth_token, name, script):
    """加载并启动策略（load + run，等价于 test_decision.py 的两步）。"""
    api_load_strategy(auth_token, name, script)
    return api_run_strategy(auth_token, name)


def _bar(symbol, day, close):
    return {
        "symbol": symbol,
        "datetime": f"{day.isoformat()} 00:00:00",
        "open": close * 0.995,
        "high": close * 1.01,
        "low": close * 0.99,
        "close": close,
        "volume": 1000000,
        "turnover": close * 1000000,
    }


def _price_path(count=BAR_COUNT):
    """V 形价格：前 15 根线性下跌，之后线性上涨 —— 必然产生一次金叉。"""
    prices = []
    for i in range(count):
        if i < 15:
            prices.append(100.0 - i * 0.5)
        else:
            prices.append(92.5 + (i - 14) * 0.6)
    return prices


def _sim_days(count=BAR_COUNT):
    return [START_DATE + datetime.timedelta(days=i) for i in range(count)]


def _push_bar(auth_token, bar, timeout=120):
    resp = requests.post(
        f"{BASE_URL}/strategy/simulate/bar",
        json=bar,
        headers=_headers(auth_token),
        verify=VERIFY_SSL,
        timeout=timeout,
    )
    assert resp.status_code == 200, (
        f"推送 bar 失败: HTTP {resp.status_code} {resp.text[:300]}"
    )
    body = resp.json()
    assert body.get("status") == "ok", f"推送 bar 未成功: {body}"
    return body


def _decisions_on(auth_token, date_str, strategy_name):
    """取某个仿真日、某个策略的决策（接口不支持按策略过滤，客户端筛）。"""
    rows = api_get_decisions(auth_token, date=date_str) or []
    return [d for d in rows if d.get("strategy") == strategy_name]


def _collect_decisions(auth_token, strategy_name, days):
    """按仿真日逐天收集某策略的决策，按决策 id 排序返回。"""
    out = []
    for day in days:
        out.extend(_decisions_on(auth_token, day.isoformat(), strategy_name))
    out.sort(key=lambda d: d["id"])
    return out


def _wait_decisions_settled(auth_token, strategy_name, days, timeout=90):
    """等决策数不再增长（日终回放是异步的，推完最后一根要给它时间落库）。

    轮询间隔取 1s：每次轮询都要按天扫一遍决策表，扫得太勤会给正在跑
    日终回放的服务端额外压力（回放本身在密集读写 DuckDB）。
    """
    deadline = time.time() + timeout
    last_count = -1
    stable_rounds = 0
    while time.time() < deadline:
        count = len(_collect_decisions(auth_token, strategy_name, days))
        if count == last_count:
            stable_rounds += 1
            if stable_rounds >= 2:      # 连续两次读到同一个数才认为稳定
                break
        else:
            stable_rounds = 0
            last_count = count
        time.sleep(1.0)
    return _collect_decisions(auth_token, strategy_name, days)


def _decision_signature(decisions):
    """比对用的签名：去掉策略名/标的/自增 id，只留可比的业务字段。

    标的故意不参与比对 —— 单跑和并发跑用的是不同标的（同样的价格路径），
    要验证的是"同样的输入产生同样的决策"，不是 symbol 字段本身。
    """
    return [
        (
            d["action"],
            d["quantity"],
            round(float(d["price"]), 4),
            d["epoch"],
        )
        for d in decisions
    ]


def _capital_summary(auth_token):
    resp = requests.get(
        f"{BASE_URL}/position",
        params={"action": "capital"},
        headers=_headers(auth_token),
        verify=VERIFY_SSL,
        timeout=30,
    )
    assert resp.status_code == 200, f"查询资金汇总失败: HTTP {resp.status_code}"
    return resp.json()["capital"]


def _ledger(auth_token):
    resp = requests.get(
        f"{BASE_URL}/position",
        params={"action": "ledger"},
        headers=_headers(auth_token),
        verify=VERIFY_SSL,
        timeout=30,
    )
    assert resp.status_code == 200, f"查询账本失败: HTTP {resp.status_code}"
    return resp.json()["strategies"]


def _status_map(auth_token):
    resp = requests.get(
        f"{BASE_URL}/strategy",
        headers=_headers(auth_token),
        verify=VERIFY_SSL,
        timeout=30,
    )
    assert resp.status_code == 200, f"查询策略列表失败: HTTP {resp.status_code}"
    data = resp.json()
    return {item["name"]: item for item in data.get("strategies", [])}


# ==================== 夹具 ====================

@pytest.fixture(scope="module")
def solo_decisions(auth_token):
    """单独跑 A / B 各一轮，作为并发运行的对照基线。

    两轮是串行推的：先推完 A 的 30 根，再推 B 的 30 根。
    同一时间只有一个策略在回放，这就是"单独运行时"的参照系。
    """
    baselines = {}
    prices = _price_path()
    days = _sim_days()

    for tag, symbol, ma in (
        ("a", SYMBOL_A, MA_A),
        ("b", SYMBOL_B, MA_B),
    ):
        name = _unique_name(f"solo_{tag}")
        _deploy(
            auth_token, name,
            _build_strategy(name, symbol, ma["ma_short"], ma["ma_long"],
                            CAPITAL_A if tag == "a" else CAPITAL_B),
        )
        for day, price in zip(days, prices):
            _push_bar(auth_token, _bar(symbol, day, price))
            time.sleep(PUSH_INTERVAL)
        baselines[tag] = _wait_decisions_settled(auth_token, name, days)

    return baselines


@pytest.fixture(autouse=True)
def _cleanup_strategies(auth_token):
    """每个用例结束清掉它创建的策略（停止 + 删除 + 归还资金）。"""
    yield
    for name in list(_CREATED_NAMES):
        api_cleanup_strategy(auth_token, name)
        reclaim_capital(auth_token, name)
        _CREATED_NAMES.remove(name)


# ==================== 用例 ====================

@pytest.mark.timeout(600)
def test_concurrent_decisions_match_solo_baseline(auth_token, solo_decisions):
    """核心用例：两个策略同时跑，各自产出的决策必须与单独跑时完全一致。

    并发阶段把两个标的的 bar **交替推进**（推完 A 的当天再推 B 的当天），
    让两个策略的日终回放真正重叠 —— 这正是共享行情缓存/共享上下文最容易串台的地方。
    """
    prices = _price_path()
    days = _sim_days()

    name_a = _unique_name("conc_a")
    name_b = _unique_name("conc_b")
    _deploy(auth_token, name_a,
            _build_strategy(name_a, SYMBOL_A, MA_A["ma_short"], MA_A["ma_long"], CAPITAL_A))
    _deploy(auth_token, name_b,
            _build_strategy(name_b, SYMBOL_B, MA_B["ma_short"], MA_B["ma_long"], CAPITAL_B))

    # 交替推送：同一天先推 A 再推 B，让两个策略的回放窗口重叠
    for day, price in zip(days, prices):
        _push_bar(auth_token, _bar(SYMBOL_A, day, price))
        _push_bar(auth_token, _bar(SYMBOL_B, day, price))
        time.sleep(PUSH_INTERVAL)

    conc_a = _wait_decisions_settled(auth_token, name_a, days)
    conc_b = _wait_decisions_settled(auth_token, name_b, days)
    solo_a = solo_decisions["a"]
    solo_b = solo_decisions["b"]

    # 反退化守卫：基线本身必须真的产出了决策，
    # 否则"两份空列表相等"是什么都没验证的空通过。
    assert len(solo_a) > 0, (
        f"基线策略 {SYMBOL_A} 没产出任何决策，用例退化为空通过"
    )
    assert len(solo_b) > 0, (
        f"基线策略 {SYMBOL_B} 没产出任何决策，用例退化为空通过"
    )

    # 1) 决策条数一致
    assert len(conc_a) == len(solo_a), (
        f"并发运行时 {SYMBOL_A} 产出 {len(conc_a)} 条决策，单独运行时 {len(solo_a)} 条"
    )
    assert len(conc_b) == len(solo_b), (
        f"并发运行时 {SYMBOL_B} 产出 {len(conc_b)} 条决策，单独运行时 {len(solo_b)} 条"
    )

    # 2) 逐条内容一致（action / quantity / price / epoch）
    assert _decision_signature(conc_a) == _decision_signature(solo_a), (
        "并发运行时策略 A 的决策与单独运行不一致\n"
        f"  并发: {_decision_signature(conc_a)[:5]}\n"
        f"  单独: {_decision_signature(solo_a)[:5]}"
    )
    assert _decision_signature(conc_b) == _decision_signature(solo_b), (
        "并发运行时策略 B 的决策与单独运行不一致\n"
        f"  并发: {_decision_signature(conc_b)[:5]}\n"
        f"  单独: {_decision_signature(solo_b)[:5]}"
    )

    # 3) 决策必须只引用自己的标的 —— 两个实例没有串数据
    for name, symbol in ((name_a, SYMBOL_A), (name_b, SYMBOL_B)):
        decisions = _collect_decisions(auth_token, name, days)
        for d in decisions:
            assert d["symbol"] == symbol, (
                f"策略 {name} 的决策里出现了别人的标的 {d['symbol']}（应为 {symbol}）"
            )


@pytest.mark.timeout(300)
def test_two_strategies_are_registered_and_run_concurrently(auth_token):
    """两个策略必须同时处于运行态、同时被日终管线驱动。"""
    name_a = _unique_name("sim_a")
    name_b = _unique_name("sim_b")
    _deploy(auth_token, name_a,
            _build_strategy(name_a, SYMBOL_A, MA_A["ma_short"], MA_A["ma_long"], CAPITAL_A))
    _deploy(auth_token, name_b,
            _build_strategy(name_b, SYMBOL_B, MA_B["ma_short"], MA_B["ma_long"], CAPITAL_B))

    status = _status_map(auth_token)
    for name in (name_a, name_b):
        assert name in status, f"策略 {name} 部署后不在列表里"
        item = status[name]
        assert item["running"] is True, f"策略 {name} 没有在运行"
        assert item["executionMode"] == "manual", (
            f"策略 {name} 的执行模式是 {item['executionMode']}，应为 manual"
        )

    # 推一根 bar：simulate/bar 内部先调 EnsureDailyReady 完成日终注册，
    # 再 MarkSymbolReady 触发执行。所以"注册"和"执行"都要在这之后才可见。
    day = START_DATE
    _push_bar(auth_token, _bar(SYMBOL_A, day, 100.0))
    _push_bar(auth_token, _bar(SYMBOL_B, day, 100.0))
    time.sleep(1.5)

    status = _status_map(auth_token)
    for name in (name_a, name_b):
        assert name in status, f"推送 bar 之后策略 {name} 消失了"
        assert status[name]["dailyRegistered"] is True, (
            f"策略 {name} 没有注册到日终管线"
        )
        assert status[name]["dailyExecuted"] is True, (
            f"策略 {name} 没有在同一个仿真日被执行"
        )


@pytest.mark.timeout(300)
def test_capital_accounting_isolated_per_strategy(auth_token):
    """总账户金额不能算错：两个策略必须各分各的账。

    - 部署一个策略，全局已分配额度恰好增加它自己的 capital（没有重复分配）
    - 两个策略在账本里是两个独立账户，各自 allocated 等于自己的 capital
    - 每个策略 available + used == allocated（账目自洽）、available 不为负
    - 两个策略合计不超过资金池初始额度
    """
    name_a = _unique_name("cat_a")
    name_b = _unique_name("cat_b")

    before = _capital_summary(auth_token)["totalAllocated"]

    _deploy(auth_token, name_a,
            _build_strategy(name_a, SYMBOL_A, MA_A["ma_short"], MA_A["ma_long"], CAPITAL_A))
    after_a = _capital_summary(auth_token)["totalAllocated"]
    assert after_a - before == pytest.approx(CAPITAL_A, abs=0.01), (
        f"部署策略 A 后全局已分配额度增加 {after_a - before}，"
        f"期望恰好 {CAPITAL_A}（重复分配或漏记）"
    )

    _deploy(auth_token, name_b,
            _build_strategy(name_b, SYMBOL_B, MA_B["ma_short"], MA_B["ma_long"], CAPITAL_B))
    after_b = _capital_summary(auth_token)["totalAllocated"]
    assert after_b - after_a == pytest.approx(CAPITAL_B, abs=0.01), (
        f"部署策略 B 后全局已分配额度增加 {after_b - after_a}，"
        f"期望恰好 {CAPITAL_B}（重复分配或漏记）"
    )
    assert after_b - before == pytest.approx(CAPITAL_A + CAPITAL_B, abs=0.01), (
        "两个策略合计分配额不等于两者之和"
    )

    # 策略列表里的资金视图
    status = _status_map(auth_token)
    for name, capital in ((name_a, CAPITAL_A), (name_b, CAPITAL_B)):
        item = status[name]
        assert item["allocatedCapital"] == pytest.approx(capital, abs=0.01), (
            f"{name} 的 allocatedCapital 是 {item['allocatedCapital']}，期望 {capital}"
        )
        assert item["availableCapital"] + item["usedCapital"] == pytest.approx(
            item["allocatedCapital"], abs=0.01
        ), f"{name} 的 available + used 与 allocated 不自洽"
        assert item["availableCapital"] >= 0, (
            f"{name} 的可用资金是负数 {item['availableCapital']} —— 资金被算错了"
        )

    # 账本里必须是两个独立账户，各自 allocated 是自己的 capital
    ledger = _ledger(auth_token)
    assert name_a in ledger, f"账本里没有 {name_a} 的账户"
    assert name_b in ledger, f"账本里没有 {name_b} 的账户"
    assert ledger[name_a]["allocated"] == pytest.approx(CAPITAL_A, abs=0.01)
    assert ledger[name_b]["allocated"] == pytest.approx(CAPITAL_B, abs=0.01)

    pool = _capital_summary(auth_token)
    assert CAPITAL_A + CAPITAL_B <= pool["initialCapital"] + 0.01, (
        f"两个策略合计 {CAPITAL_A + CAPITAL_B} 超过资金池 {pool['initialCapital']}"
    )


@pytest.mark.timeout(300)
def test_failed_second_deploy_does_not_affect_first(auth_token, solo_decisions):
    """第二个策略部署失败，不能影响第一个策略的结果。"""
    name_a = _unique_name("fail_a")
    name_bad = _unique_name("fail_bad")

    _deploy(auth_token, name_a,
            _build_strategy(name_a, SYMBOL_A, MA_A["ma_short"], MA_A["ma_long"], CAPITAL_A))

    # 构造一个必然初始化失败的图：节点类型写错
    bad_script = _build_strategy(
        name_bad, SYMBOL_B, MA_B["ma_short"], MA_B["ma_long"], CAPITAL_B
    )
    for node in bad_script["nodes"]:
        if node["data"]["nodeType"] == "input":
            node["data"]["nodeType"] = "no_such_node_type"
            break

    headers = _headers(auth_token)
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "load", "name": name_bad, "script": bad_script},
        headers=headers, verify=VERIFY_SSL, timeout=60,
    )
    assert resp.status_code >= 400, (
        f"坏图居然加载成功了: HTTP {resp.status_code} {resp.text[:300]}"
    )
    assert "failed_node" in resp.json(), f"失败响应里没有 failed_node: {resp.json()}"

    # 第一个策略照常跑完，结果与单独跑一致
    days = _sim_days()
    for day, price in zip(days, _price_path()):
        _push_bar(auth_token, _bar(SYMBOL_A, day, price))
        time.sleep(PUSH_INTERVAL)

    decisions = _wait_decisions_settled(auth_token, name_a, days)
    assert _decision_signature(decisions) == _decision_signature(solo_decisions["a"]), (
        "第二个策略部署失败之后，第一个策略的决策与单独运行不一致"
    )


@pytest.mark.timeout(300)
def test_stopping_one_strategy_leaves_the_other_intact(auth_token):
    """停掉并删除一个策略，另一个必须毫发无损、还能继续被驱动。"""
    name_a = _unique_name("stp_a")
    name_b = _unique_name("stp_b")
    _deploy(auth_token, name_a,
            _build_strategy(name_a, SYMBOL_A, MA_A["ma_short"], MA_A["ma_long"], CAPITAL_A))
    _deploy(auth_token, name_b,
            _build_strategy(name_b, SYMBOL_B, MA_B["ma_short"], MA_B["ma_long"], CAPITAL_B))

    # 先推一根，确认两个都在跑
    first = START_DATE
    _push_bar(auth_token, _bar(SYMBOL_A, first, 100.0))
    _push_bar(auth_token, _bar(SYMBOL_B, first, 100.0))
    time.sleep(1.0)

    api_cleanup_strategy(auth_token, name_a)
    _CREATED_NAMES.remove(name_a)

    status = _status_map(auth_token)
    assert name_a not in status, f"{name_a} 删除后还在列表里"
    assert name_b in status, f"删除 {name_a} 把 {name_b} 也弄没了"
    assert status[name_b]["running"] is True, f"删除 {name_a} 之后 {name_b} 停了"

    # B 还能继续响应推送（日终管线没被 A 的删除带走）
    second = first + datetime.timedelta(days=1)
    _push_bar(auth_token, _bar(SYMBOL_B, second, 99.5))
    time.sleep(1.5)
    status = _status_map(auth_token)
    assert status[name_b]["dailyExecuted"] is True, (
        f"删除 {name_a} 之后 {name_b} 不能再被 simulate/bar 驱动"
    )
