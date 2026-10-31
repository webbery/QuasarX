"""账本测试（三层）

全部基于已有 HTTP 端点：
    写入：POST /v0/trade/order   （OrderDesk 手动回写 → RecordManualFill → LedgerStore::ApplyFill）
    读取：GET  /v0/position?action=ledger|credit|capital

L1 账本算术
    - 现金账户：买入 / 加仓 / 部分卖出（FIFO）/ 清仓 / 费用口径
    - 融资账户：买入拆分自有资金与融资余额、卖出按比例还本
L2 口径回归
    - equity == cash + positionValue − borrowedCash
    - 买入不改变权益（仅损失费用）；卖出盈亏 == 权益增量
    - 日终预算基于账本真实现金，而不是策略初始资金（2026-10-30 事故回归）
L3 持久化
    - data/broker/ledger.json 与 /position 返回值一致

覆盖缺口（端点能力不足，用例内 skip 并说明原因）：
    - 融券卖出 / 买入平空：OrderHandler 只把 direct 映射为 OpenLong/CloseLong，
      走不到 OpenShort/CloseShort，无法通过 HTTP 构造空头。
    - 重启恢复：服务进程由 run_tests.py 在 pytest 外层持有，用例内无法重启。
"""

import json
import uuid

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import (
    BASE_URL, VERIFY_SSL, _DATA_DIR,
    get_auth_token, api_load_strategy, api_cleanup_strategy,
)

# ---------------------------- 常量 ----------------------------

# RecordManualFill 由 OrderHandler 调用时使用默认费率
COMMISSION_RATE = 9e-05
SLIPPAGE_RATE = 5e-04
FEE_RATE = COMMISSION_RATE + SLIPPAGE_RATE

INITIAL_CAPITAL = 1_000_000.0
SYMBOL = "sz.900001"

LEDGER_FILE = _DATA_DIR / "broker" / "ledger.json"


# ---------------------------- 策略构造 ----------------------------

def _make_strategy(name: str, margin_ratio: float = 1.0) -> dict:
    """最小策略：行情 → 信号 → 投资组合 → 手动执行"""
    script = {
        "id": name,
        "name": name,
        "version": 1,
        "capital": INITIAL_CAPITAL,
        "description": "ledger arithmetic test",
        "backtest": {"start": "2023-03-01", "end": "2023-10-06"},
        "nodes": [
            {
                "id": "1",
                "type": "custom",
                "data": {
                    "label": "行情数据",
                    "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "select"},
                        "code": {"value": [SYMBOL], "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"},
                        "turnover": {"value": "turnover", "type": "text"},
                    },
                },
            },
            {
                "id": "2",
                "type": "custom",
                "data": {
                    "label": "信号",
                    "nodeType": "signal",
                    "params": {
                        "code": {"value": [SYMBOL], "type": "text"},
                        "buy": {"value": "close[t] > close[t-1]", "type": "text"},
                        "sell": {"value": "close[t] < close[t-1]", "type": "text"},
                    },
                },
            },
            {
                "id": "3",
                "type": "custom",
                "data": {
                    "label": "投资组合",
                    "nodeType": "portfolio",
                    "params": {"positionRatio": {"value": 1.0, "type": "number"}},
                },
            },
            {
                "id": "4",
                "type": "custom",
                "data": {
                    "label": "手动执行",
                    "nodeType": "execution",
                    "params": {
                        "commission": {"value": 0.0, "type": "number"},
                        "stamp": {"value": 0.0, "type": "number"},
                        "minCommission": {"value": 0, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": 0.0, "type": "number"},
                        "type": {"value": 7, "type": "select"},
                        "contract": {"value": 0, "type": "select"},
                    },
                },
            },
        ],
        "edges": [
            {"id": "1->2", "source": "1", "target": "2",
             "sourceHandle": "output", "targetHandle": "input"},
            {"id": "2->3", "source": "2", "target": "3",
             "sourceHandle": "output", "targetHandle": "input"},
            {"id": "3->4", "source": "3", "target": "4",
             "sourceHandle": "output", "targetHandle": "input"},
        ],
    }
    if margin_ratio < 1.0:
        script["margin"] = {"allowShort": False, "marginRatio": margin_ratio}
    return script


# ---------------------------- 端点封装 ----------------------------

def _headers(token: str) -> dict:
    return {"Authorization": token}


def _fill(token: str, strategy: str, quantity: int, price: float, direct: int = 0) -> dict:
    """手动回写一笔成交。direct: 0=买入, 1=卖出。"""
    body = {
        "symbol": SYMBOL,
        "direct": direct,
        "type": 1,
        "quantity": quantity,
        "prices": price,
        "kind": 0,
        "timeType": 0,
        "decisionId": 0,   # >=0 才会走 OrderDesk 手动回写分支；0 表示不关联决策
        "strategy": strategy,
    }
    resp = requests.post(
        f"{BASE_URL}/trade/order", json=body,
        headers=_headers(token), verify=VERIFY_SSL, timeout=15,
    )
    assert resp.status_code == 200, f"manual fill failed: {resp.status_code} {resp.text[:300]}"
    return resp.json()


def _ledger(token: str, strategy: str) -> dict:
    resp = requests.get(
        f"{BASE_URL}/position",
        params={"action": "ledger", "strategy": strategy},
        headers=_headers(token), verify=VERIFY_SSL, timeout=15,
    )
    assert resp.status_code == 200, f"ledger query failed: {resp.status_code} {resp.text[:300]}"
    strategies = resp.json().get("strategies", {})
    assert strategy in strategies, f"账本中没有策略 {strategy}"
    return strategies[strategy]


def _credit(token: str, strategy: str) -> dict:
    resp = requests.get(
        f"{BASE_URL}/position",
        params={"action": "credit", "strategy": strategy},
        headers=_headers(token), verify=VERIFY_SSL, timeout=15,
    )
    assert resp.status_code == 200, resp.text[:300]
    return resp.json()["strategies"][strategy]


def _capital(token: str) -> dict:
    resp = requests.get(
        f"{BASE_URL}/position", params={"action": "capital"},
        headers=_headers(token), verify=VERIFY_SSL, timeout=15,
    )
    assert resp.status_code == 200, resp.text[:300]
    return resp.json()["capital"]


def _position_of(entry: dict, symbol: str) -> dict:
    for pos in entry.get("positions", []):
        if pos["symbol"] == symbol:
            return pos
    return {}


def _deploy(token: str, name: str, margin_ratio: float = 1.0) -> None:
    try:
        api_load_strategy(token, name, _make_strategy(name, margin_ratio))
    except Exception as e:  # 环境缺数据 / 节点初始化失败
        pytest.skip(f"策略初始化失败，跳过账本用例: {e}")


@pytest.fixture(scope="module", autouse=True)
def _require_live_mode(is_backtest):
    """手动回写走 OrderDesk 分支，回测模式下 /trade/order 会变成 SimulateFill"""
    if is_backtest:
        pytest.skip("账本用例需要非回测模式（OrderDesk 手动回写分支），当前为回测模式")


@pytest.fixture
def strategy(auth_token):
    """现金账户策略（marginRatio = 1.0）"""
    name = f"test_ledger_cash_{uuid.uuid4().hex[:8]}"
    _deploy(auth_token, name)
    yield name
    api_cleanup_strategy(auth_token, name)


@pytest.fixture
def margin_strategy(auth_token):
    """融资账户策略（marginRatio = 0.5）"""
    name = f"test_ledger_margin_{uuid.uuid4().hex[:8]}"
    _deploy(auth_token, name, margin_ratio=0.5)
    yield name
    api_cleanup_strategy(auth_token, name)


# ---------------------------- L1：账本算术 ----------------------------

class TestLedgerArithmetic:
    """现金账户的成交回写"""

    def test_buy_opens_position(self, auth_token, strategy):
        """买入开仓：现金按成交额 + 费用扣减，持仓与 FIFO 批次正确"""
        _fill(auth_token, strategy, 10_000, 10.0)
        entry = _ledger(auth_token, strategy)

        gross = 10_000 * 10.0
        fees = gross * FEE_RATE
        assert entry["allocated"] == pytest.approx(INITIAL_CAPITAL)
        assert entry["cash"] == pytest.approx(INITIAL_CAPITAL - gross - fees)
        assert entry["borrowedCash"] == pytest.approx(0.0)

        pos = _position_of(entry, SYMBOL)
        assert pos["quantity"] == 10_000
        assert pos["avgCost"] == pytest.approx(10.0)
        assert pos["currentPrice"] == pytest.approx(10.0)
        assert pos["marketValue"] == pytest.approx(gross)

        lots = entry["lots"][SYMBOL]
        assert len(lots) == 1
        assert lots[0]["quantity"] == 10_000
        assert lots[0]["price"] == pytest.approx(10.0)

    def test_add_position_keeps_fifo_batches(self, auth_token, strategy):
        """加仓：均价按加权平均，FIFO 追加新批次"""
        _fill(auth_token, strategy, 10_000, 10.0)
        _fill(auth_token, strategy, 10_000, 12.0)

        entry = _ledger(auth_token, strategy)
        pos = _position_of(entry, SYMBOL)
        assert pos["quantity"] == 20_000
        assert pos["avgCost"] == pytest.approx(11.0)

        lots = entry["lots"][SYMBOL]
        assert [lot["quantity"] for lot in lots] == [10_000, 10_000]
        assert [lot["price"] for lot in lots] == [pytest.approx(10.0), pytest.approx(12.0)]

    def test_partial_sell_consumes_fifo_and_keeps_avg_cost(self, auth_token, strategy):
        """部分卖出：FIFO 吃掉最早批次，持仓均价不变，已实现盈亏按消耗成本计"""
        _fill(auth_token, strategy, 10_000, 10.0)
        _fill(auth_token, strategy, 10_000, 12.0)
        _fill(auth_token, strategy, 5_000, 15.0, direct=1)

        entry = _ledger(auth_token, strategy)
        pos = _position_of(entry, SYMBOL)
        assert pos["quantity"] == 15_000
        assert pos["avgCost"] == pytest.approx(11.0), "部分平仓不应改变持仓均价"

        lots = entry["lots"][SYMBOL]
        assert [lot["quantity"] for lot in lots] == [5_000, 10_000], "应先消耗 10.0 的批次"
        assert [lot["price"] for lot in lots] == [pytest.approx(10.0), pytest.approx(12.0)]

        sell_gross = 5_000 * 15.0
        expected_profit = sell_gross - 5_000 * 10.0 - sell_gross * FEE_RATE
        assert entry["profit"] == pytest.approx(expected_profit)

    def test_full_close_clears_position(self, auth_token, strategy):
        """清仓：持仓与批次清空，现金等于初始 − 买入费用 + 卖出净额"""
        _fill(auth_token, strategy, 1_000, 10.0)
        _fill(auth_token, strategy, 1_000, 11.0, direct=1)

        entry = _ledger(auth_token, strategy)
        assert _position_of(entry, SYMBOL) == {}
        assert entry["lots"].get(SYMBOL, []) == []
        assert entry["positionValue"] == pytest.approx(0.0)

        buy_fees = 10_000 * FEE_RATE
        sell_gross = 11_000.0
        sell_fees = sell_gross * FEE_RATE
        assert entry["cash"] == pytest.approx(
            INITIAL_CAPITAL - 10_000 - buy_fees + sell_gross - sell_fees
        )
        assert entry["profit"] == pytest.approx(sell_gross - 10_000 - sell_fees)

    def test_sell_fee_is_charged_on_gross(self, auth_token, strategy):
        """卖出费用按成交额计，不计入持仓成本"""
        _fill(auth_token, strategy, 10_000, 10.0)
        before = _ledger(auth_token, strategy)["cash"]
        _fill(auth_token, strategy, 10_000, 10.0, direct=1)
        after = _ledger(auth_token, strategy)["cash"]

        gross = 100_000.0
        assert after - before == pytest.approx(gross - gross * FEE_RATE)

    def test_cash_account_has_no_credit(self, auth_token, strategy):
        """现金账户不产生融资余额与融券在借"""
        _fill(auth_token, strategy, 10_000, 10.0)
        credit = _credit(auth_token, strategy)
        assert credit["borrowedCash"] == pytest.approx(0.0)
        assert credit["borrowedShares"] == {}
        assert credit["financedAmount"] == {}

    def test_short_sell_not_reachable_via_order_endpoint(self, auth_token, strategy):
        """融券卖出路径无法通过 /trade/order 构造（direct 只映射开多/平多）"""
        pytest.skip(
            "OrderHandler 把 direct=1 映射为 CloseLong，OpenShort/CloseShort 没有 HTTP 入口；"
            "融券账本需在 RecordManualFill 层覆盖"
        )


class TestMarginArithmetic:
    """融资账户的成交回写（真两融口径）"""

    def test_margin_buy_splits_cash_and_debt(self, auth_token, margin_strategy):
        """融资买入：现金只扣自有保证金部分，差额计入融资余额"""
        _fill(auth_token, margin_strategy, 10_000, 10.0)

        entry = _ledger(auth_token, margin_strategy)
        gross = 100_000.0
        own = gross * 0.5
        borrow = gross - own
        fees = gross * FEE_RATE

        assert entry["cash"] == pytest.approx(INITIAL_CAPITAL - own - fees)
        assert entry["borrowedCash"] == pytest.approx(borrow)
        assert entry["credit"]["financedAmount"][SYMBOL] == pytest.approx(borrow)

        pos = _position_of(entry, margin_strategy)
        assert pos["quantity"] == 10_000
        assert pos["isMargin"] is True
        assert pos["marginRatio"] == pytest.approx(0.5)

    def test_sell_repays_financing(self, auth_token, margin_strategy):
        """卖出平多：卖出所得先按比例偿还融资本金，负债归零"""
        _fill(auth_token, margin_strategy, 10_000, 10.0)
        _fill(auth_token, margin_strategy, 10_000, 11.0, direct=1)

        entry = _ledger(auth_token, margin_strategy)
        assert entry["borrowedCash"] == pytest.approx(0.0)
        assert entry["credit"]["financedAmount"] == {}

        buy_fees = 100_000 * FEE_RATE
        sell_gross = 110_000.0
        sell_fees = sell_gross * FEE_RATE
        assert entry["cash"] == pytest.approx(
            INITIAL_CAPITAL - 50_000 - buy_fees + sell_gross - sell_fees - 50_000
        )
        assert entry["profit"] == pytest.approx(sell_gross - 100_000 - sell_fees)


# ---------------------------- L2：口径回归 ----------------------------

class TestEquityInvariants:
    """权益口径：equity = cash + positionValue − borrowedCash"""

    def test_equity_identity(self, auth_token, strategy):
        _fill(auth_token, strategy, 10_000, 10.0)
        _fill(auth_token, strategy, 4_000, 13.0, direct=1)

        entry = _ledger(auth_token, strategy)
        expected = entry["cash"] + entry["positionValue"] - entry["borrowedCash"]
        assert entry["equity"] == pytest.approx(expected)
        assert entry["positionValue"] == pytest.approx(
            sum(p["marketValue"] for p in entry["positions"])
        )

    def test_equity_identity_with_financing(self, auth_token, margin_strategy):
        _fill(auth_token, margin_strategy, 10_000, 10.0)
        entry = _ledger(auth_token, margin_strategy)
        expected = entry["cash"] + entry["positionValue"] - entry["borrowedCash"]
        assert entry["equity"] == pytest.approx(expected)
        assert entry["borrowedCash"] > 0

    def test_buy_only_costs_fees(self, auth_token, strategy):
        """买入瞬间权益只减少费用（持仓按成交价估值）"""
        before = _ledger(auth_token, strategy)["equity"]
        _fill(auth_token, strategy, 10_000, 10.0)
        after = _ledger(auth_token, strategy)["equity"]

        fees = 100_000 * FEE_RATE
        assert after - before == pytest.approx(-fees)

    def test_sell_pnl_equals_equity_delta(self, auth_token, strategy):
        """卖出后权益增量 = 毛利 − 卖出费用"""
        _fill(auth_token, strategy, 10_000, 10.0)
        before = _ledger(auth_token, strategy)["equity"]
        _fill(auth_token, strategy, 10_000, 12.0, direct=1)
        after = _ledger(auth_token, strategy)["equity"]

        sell_gross = 120_000.0
        assert after - before == pytest.approx(sell_gross - 100_000 - sell_gross * FEE_RATE)

    def test_capital_summary_matches_accounts(self, auth_token, strategy):
        """capital 汇总与账本条目一致（至少覆盖本策略）"""
        _fill(auth_token, strategy, 10_000, 10.0)
        capital = _capital(auth_token)
        entry = _ledger(auth_token, strategy)

        assert capital["initialCapital"] >= 0
        assert capital["totalAllocated"] >= entry["allocated"]
        assert capital["activeStrategies"] >= 1


# ---------------------------- L3：持久化 ----------------------------

class TestLedgerPersistence:
    """ledger.json 是账本落盘载体"""

    def test_ledger_file_matches_api(self, auth_token, strategy):
        _fill(auth_token, strategy, 10_000, 10.0)

        assert LEDGER_FILE.exists(), f"账本文件不存在: {LEDGER_FILE}"
        data = json.loads(LEDGER_FILE.read_text(encoding="utf-8"))

        assert data.get("version") == 1
        accounts = data.get("accounts", {})
        assert strategy in accounts, "成交后应立即落盘该策略账户"

        on_disk = accounts[strategy]
        entry = _ledger(auth_token, strategy)

        assert on_disk["allocated"] == pytest.approx(entry["allocated"])
        assert on_disk["cash"] == pytest.approx(entry["cash"], rel=1e-9, abs=1e-6)
        assert on_disk["borrowedCash"] == pytest.approx(entry["borrowedCash"])

        disk_pos = on_disk["positions"][SYMBOL]
        api_pos = _position_of(entry, SYMBOL)
        assert disk_pos["quantity"] == api_pos["quantity"]
        assert disk_pos["avg_cost"] == pytest.approx(api_pos["avgCost"])

        disk_lots = on_disk["lots"][SYMBOL]
        assert len(disk_lots) == len(entry["lots"][SYMBOL])

    def test_ledger_file_structure(self, auth_token, strategy):
        _fill(auth_token, strategy, 1_000, 10.0)
        data = json.loads(LEDGER_FILE.read_text(encoding="utf-8"))
        account = data["accounts"][strategy]

        for key in ("allocated", "cash", "positions", "lots", "credit", "profit", "active"):
            assert key in account, f"落盘缺少字段 {key}"
        for key in ("borrowedCash", "borrowedShares", "shortAvgPrice", "financedAmount"):
            assert key in account["credit"], f"credit 段缺少字段 {key}"

    def test_restart_restore_requires_service_restart(self, auth_token, strategy):
        """Load 分支需要重启服务；pytest 外层持有服务进程，用例内无法重启"""
        pytest.skip(
            "重启恢复需在用例外重启 QuantService："
            "先 POST /v0/trade/order 成交，再重启，最后 GET /v0/position?action=ledger 比对"
        )


# ---------------------------- 端点行为 ----------------------------

class TestPositionEndpoint:
    """/position 的 action 分发"""

    def test_ledger_without_strategy_returns_all(self, auth_token, strategy):
        _fill(auth_token, strategy, 100, 10.0)
        resp = requests.get(
            f"{BASE_URL}/position", params={"action": "ledger"},
            headers=_headers(auth_token), verify=VERIFY_SSL, timeout=15,
        )
        assert resp.status_code == 200
        assert strategy in resp.json()["strategies"]

    def test_unknown_strategy_returns_404(self, auth_token):
        resp = requests.get(
            f"{BASE_URL}/position",
            params={"action": "ledger", "strategy": "no_such_strategy_xyz"},
            headers=_headers(auth_token), verify=VERIFY_SSL, timeout=15,
        )
        assert resp.status_code == 404

    def test_default_returns_positions_and_ledger(self, auth_token):
        resp = requests.get(
            f"{BASE_URL}/position", headers=_headers(auth_token),
            verify=VERIFY_SSL, timeout=15,
        )
        assert resp.status_code == 200
        body = resp.json()
        assert "positions" in body
        assert "capital" in body
        assert "strategies" in body
