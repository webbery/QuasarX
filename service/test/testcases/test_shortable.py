#!/usr/bin/env python3
"""
融资融券标的过滤测试

覆盖 SignalNode 的两段逻辑：
  - Init()   → ensureMarginStockData()：加载 margin_stock，空表时拉取兜底
  - Process()→ 用 _shortableSymbols 过滤开空仓的 SELL 信号

标的池 5 只，prepare.py 只把前 MARGIN_PRICES 的两只灌进 margin_stock，
其余天然是"不可做空"对照组 —— 不依赖真实名单，也不需要 mock。

依赖：CI 在启动服务前跑 `python3 test/prepare.py`，把本文件的
      MARGIN_SYMBOLS 预灌进 data/finance/finance.db。

注意 capital：策略 JSON 里写 capital=0（自动分配），不要写死金额。
CapitalPool 从 data/capital_pool.json 恢复，而 InitStrategy 会把实盘
策略以 active=true 持久化且永不回收（只有回测有 CapitalScopeGuard 在内存
里释放）—— 实盘 CTA_v16 占满 100 万后 available=0，任何写死金额的回测
都会 400「资金分配失败」。这是 CapitalPool 的生命周期 bug，不是测试问题。

只断言信号层：融券卖出尚未接通 broker —— StockPositionManager::Sell()
在无持仓时直接返回，成交笔数恒为 0，拿 sell_count 断言测不到过滤本身。

用法:
  pytest test_shortable.py -v


TODO(第二层 — 成交层): 等融券卖出接通 broker 后补，当前不要写。
  阻塞点在 StockPositionManager::Sell()（已就地标注），前置实现：
    1. _allowShort 开关，允许 qty > pos._qty 产生负持仓；
    2. CreditOrderType 真正参与路由（目前只有字段声明，无人读取）；
    3. 借券成本模型，否则做空收益系统性虚高。

  届时应补的用例。可断言的字段来自 backtest result：
    summary.buy_count / summary.sell_count（成交笔数），
    以及顶层 result["buy"] / result["sell"] 数组，元素为
    {symbol, time, quantity, price}。

    a. 空头开仓成交：allowShort=true + 可做空标的 → sell_count >= 1
    b. 不可做空标的开空：allowShort=true + 非名单标的 → sell_count == 0
    c. 平空：先开空再翻多，持仓回到 0（buy_count 与 sell_count 各增 1）
    d. 盈亏方向：空头在下跌行情为正收益、上涨行情为负
       （收益取反，d 的正负号最容易写错，务必断言符号而非绝对值）
    e. 借券成本：同等信号下，计入借券费后收益单调低于不计费版本
    f. 借券费计提天数随持仓拉长而增加

  一个坑：result["sell"] 不区分"平多"和"开空"——都是 order side=1。
  要判断开仓还是平仓，得结合持仓符号（负仓=空头），
  单看 sell_count 无法区分 (a) 和一次普通平多。

  注意 (a) 不能与本文件现有用例合并 —— 现有断言读的是 SignalNode 的
  .signal（信号层），(a) 起改读 broker 成交回报（成交层），两者要分开，
  否则一旦成交链路出问题，分不清是过滤逻辑坏了还是撮合坏了。
"""
import csv
import json
import shutil
from datetime import datetime, timedelta

import pytest
import requests

from tool import BASE_URL, CSV_DATA_DIR, VERIFY_SSL, read_debug_csv

# 前两只由 prepare.py 预灌为可做空，后三只不在 margin_stock 里
MARGIN_PRICES = [
    ("sz.900020", 50.0),
    ("sz.900021", 40.0),
]
NON_MARGIN_PRICES = [
    ("sz.900022", 100.0),
    ("sz.900023", 95.0),
    ("sz.900024", 90.0),
]
ALL_PRICES = MARGIN_PRICES + NON_MARGIN_PRICES
MARGIN_SYMBOLS = {s for s, _ in MARGIN_PRICES}
NON_MARGIN_SYMBOLS = {s for s, _ in NON_MARGIN_PRICES}
SYMBOLS = [s for s, _ in ALL_PRICES]

# 对全池为真的 sell 表达式：信号的唯一变量只剩"在不在 margin_stock 里"
SELL_EXPR_ALL = "close[t-1] <= 200"

START = datetime(2024, 1, 1)
N_BARS = 100


def _write_csv(symbol, base_price):
    """生成常数价格 CSV（后复权 + 原始各一份）

    价格恒定，避免噪声让边界断言漂移 —— 本文件要的就是"表达式对全池为真"。
    """
    rows = []
    d = START
    for _ in range(N_BARS):
        while d.weekday() >= 5:
            d += timedelta(days=1)
        rows.append([d.strftime("%Y-%m-%d"), base_price, base_price, base_price,
                     base_price, 1000000, round(base_price * 1000000, 2)])
        d += timedelta(days=1)

    # CSV_DATA_DIR 本身就是 .../A_hfq，原始价目录是它的同级 AStock
    hfq_dir = CSV_DATA_DIR
    org_dir = CSV_DATA_DIR.parent / "AStock"
    hfq_dir.mkdir(parents=True, exist_ok=True)
    org_dir.mkdir(parents=True, exist_ok=True)

    hfq_path = hfq_dir / f"{symbol}.csv"
    with open(hfq_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["datetime", "open", "close", "high", "low",
                         "volume", "turnover"])
        writer.writerows(rows)
    shutil.copy2(hfq_path, org_dir / f"{symbol}.csv")
    return hfq_path


def _upload(symbols, headers):
    """上传 org + hfq（必须分开传，见 data_pipeline_org_hfq_split_fix）"""
    for symbol in symbols:
        base = CSV_DATA_DIR / f"{symbol}.csv"
        hfq_lines = base.read_text().strip().split("\n")[1:]
        org_lines = base.read_text().strip().split("\n")[1:]
        resp = requests.post(f"{BASE_URL}/quote/data", json={
            "action": "import", "table": "stock_1d", "symbol": symbol,
            "data": org_lines, "data_hfq": hfq_lines,
        }, headers=headers, verify=VERIFY_SSL, timeout=60)
        if resp.status_code != 200:
            return f"upload {symbol}: HTTP {resp.status_code} {resp.text[:200]}"
    return None


def _cleanup(symbols, headers):
    for symbol in symbols:
        try:
            requests.post(f"{BASE_URL}/quote/data", json={
                "action": "delete", "table": "stock_1d", "symbol": symbol,
            }, headers=headers, verify=VERIFY_SSL, timeout=30)
        except Exception:
            pass
        for p in [CSV_DATA_DIR / f"{symbol}.csv",
                  CSV_DATA_DIR.parent / "AStock" / f"{symbol}.csv"]:
            if p.exists():
                p.unlink()


def _build_strategy(strategy_id, allow_short):
    """Input(5) → Signal → Debug(旁路) → Portfolio → Execution

    DebugNode 挂在 Signal 之后，把 .signal 落盘 —— 信号层断言不经过 broker。
    """
    return {
        "id": strategy_id,
        "name": f"融券标的过滤_{strategy_id}",
        "version": 1,
        "description": "融资融券标的过滤测试",
        # capital=0 走自动分配（CapitalPool::allocate 里 requested<=0 分支），
        # 不会因池子余额不足而 400。本文件只断言 SignalNode 的 .signal，
        # 与资金规模无关；写死 100000 会被 capital_pool.json 里 active 的
        # 实盘策略（如 CTA_v16 占满 100 万）挤成 available=0 而全盘失败。
        "capital": 0,
        "backtest": {
            "start": START.strftime("%Y-%m-%d"),
            "end": (START + timedelta(days=N_BARS * 2)).strftime("%Y-%m-%d"),
        },
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom",
             "data": {"label": "行情", "nodeType": "input",
                      "params": {"source": {"value": "股票", "type": "text"},
                                 "code": {"value": SYMBOLS, "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"}}}},
            {"id": "2", "type": "custom",
             "data": {"label": "信号", "nodeType": "signal",
                      "params": {"code": {"value": SYMBOLS, "type": "text"},
                                 "type": {"value": "股票", "type": "select"},
                                 "allowShort": {"value": allow_short, "type": "boolean"},
                                 "buy": {"value": "false", "type": "text"},
                                 "sell": {"value": SELL_EXPR_ALL, "type": "text"}}}},
            {"id": "3", "type": "custom",
             "data": {"label": "组合", "nodeType": "portfolio",
                      "params": {"positionRatio": {"value": 1.0, "type": "number"},
                                 "allowShort": {"value": allow_short, "type": "boolean"}}}},
            {"id": "4", "type": "custom",
             "data": {"label": "执行", "nodeType": "execution",
                      "params": {"commission": {"value": 0.0, "type": "number"},
                                 "stampDuty": {"value": 0.0, "type": "number"},
                                 "slippage": {"value": 0.0, "type": "number"},
                                 "type": {"value": 1, "type": "select"}}}},
            {"id": "5", "type": "custom",
             "data": {"label": "debug_signal", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e4", "source": "2", "target": "5",
             "sourceHandle": "2", "targetHandle": "5", "type": "default"},
        ],
    }


def _run(strategy_id, allow_short, headers):
    strategy = _build_strategy(strategy_id, allow_short)
    r = requests.post(f"{BASE_URL}/backtest",
                      json={"script": json.dumps(strategy), "validate": False},
                      headers=headers, verify=VERIFY_SSL, timeout=1800)
    if r.status_code != 200:
        return None, f"HTTP {r.status_code}: {r.text[:300]}"
    return r.json(), None


def _sold_symbols(strategy_id):
    """读 Debug CSV，返回出现过 SELL 信号（.signal == -1）的标的集合"""
    df = read_debug_csv(strategy_id, "debug_signal")
    hit = set()
    for col in df.columns:
        if not col.endswith(".signal"):
            continue
        if (df[col] == -1).any():
            hit.add(col[: -len(".signal")])
    return hit


# ============================================================
# fixtures
# ============================================================

@pytest.fixture(scope="module", autouse=True)
def _shortable_module_setup():
    """模块级：登录 → 生成 CSV → 上传 → 测试结束清理"""
    headers = {"Authorization": requests.post(
        f"{BASE_URL}/user/login", json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL, timeout=10).json()["tk"]}
    for symbol, price in ALL_PRICES:
        _write_csv(symbol, price)
    err = _upload(SYMBOLS, headers)
    if err:
        pytest.skip(f"setup failed: {err}")
    yield headers
    _cleanup(SYMBOLS, headers)


@pytest.fixture
def shortable_headers(_shortable_module_setup):
    return _shortable_module_setup


# ============================================================
# 测试
# ============================================================

class TestShortableFilter:
    """SignalNode::Process 的 margin_stock 过滤分支"""

    def test_margin_symbols_present_in_table(self, shortable_headers):
        """前置校验（必须最先跑）: 名单里确实有预灌的合成标的

        SignalNode 现在只读 margin_stock，读不到匹配项就禁用做空、不再自行抓取。
        所以本地忘了跑 prepare.py 时，后面两条会以"一条 SELL 都没有"的形式失败，
        但真正的原因在环境而不在被测逻辑。这条专门把那个原因挑明。

        放在最前面是因为它失败时消息最有指向性；顺序在后的话，
        pytest 会先停在下面那条只说"放行为空"的断言上，把线索盖掉。
        """
        _, err = _run("short_probe", True, shortable_headers)
        assert err is None, err
        sold = _sold_symbols("short_probe")
        assert sold & MARGIN_SYMBOLS, (
            f"预灌的可做空标的一个都没放行 —— prepare.py 是否执行成功？"
            f"（CI 中该步骤在 QuantService 启动前；本地需停服后执行 "
            f"python3 test/prepare.py --db-path service/build/data）"
            f"\n卖出的标的: {sorted(sold)}"
        )
        assert not (sold & NON_MARGIN_SYMBOLS), (
            f"不可做空标的却被放行: {sorted(sold & NON_MARGIN_SYMBOLS)}"
        )

    def test_filter_selects_only_margin_symbols(self, shortable_headers):
        """核心: 同一表达式命中全池，最终只有 margin_stock 里的标的放行

        SELL_EXPR_ALL 对 5 只全为 true，所以信号的唯一变量就是标的是否在
        margin_stock 里。断言 sold 恰好等于预灌名单：

        - 相等即证明非名单标的被拦下；
        - 同时排除了"表达式压根没触发 → sold 为空"的假阳性。
        """
        _, err = _run("short_filter_selects", True, shortable_headers)
        assert err is None, err
        sold = _sold_symbols("short_filter_selects")
        assert sold == MARGIN_SYMBOLS, (
            f"期望恰好放行预灌的可做空标的 {sorted(MARGIN_SYMBOLS)}，实际 {sorted(sold)}"
            f"（漏放行={sorted(sold - MARGIN_SYMBOLS)}，"
            f"误拦截={sorted(MARGIN_SYMBOLS - sold)}）"
        )

    def test_allow_short_false_blocks_everything(self, shortable_headers):
        """allowShort=false → 全池无 SELL，与名单无关的一票否决

        用与上一条完全相同的表达式、同一份数据，唯一变量是 allowShort，
        坐实拦截来自 !_allowShort 分支而不是 margin_stock 过滤。
        """
        _, err = _run("short_disabled", False, shortable_headers)
        assert err is None, err
        sold = _sold_symbols("short_disabled")
        assert sold == set(), (
            f"allowShort=false 时不应有任何 SELL 信号，实际 {sorted(sold)}"
        )
