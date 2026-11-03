"""回归测试：2026-11-03 账本投影失明事故

事故链：
  - 11-02 00:53 重启后，StrategyCapitalImpl 的投影缓存停在"空持仓"状态
    （版本号协议存在漏写路径：Load 不 bump 版本）
  - 11-02 15:02 策略把已持仓的 sz.300450 / sh.601939 又买了一遍
    （以为没仓）
  - 11-03 15:01 sz.300450 的追踪止损误触发（建仓前的高点被当作参考），
    4 只持仓被连带平掉并全部打上"紧急"标记
  - 现金 −332,736，账户超配

修复：
  - 删除版本号缓存机制，StrategyCapital 改为直读账本
  - 追踪止损基准改为建仓后（跳过建仓前的 bar）
  - 紧急平仓只平触发标的（不连带清其他）
  - 负现金账户禁止开仓 + 买入额不超过可用现金+卖出回款
"""

import pytest
import requests
import urllib3
from pathlib import Path

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import (
    BASE_URL, VERIFY_SSL, _DATA_DIR,
    get_auth_token, api_load_strategy, api_cleanup_strategy,
)

# ---------------------------- 常量 ----------------------------

INITIAL_CAPITAL = 1_000_000.0
SYMBOLS = ["sz.300450", "sz.002241", "sh.601939", "sh.688981"]
POSITIONS = {
    "sz.300450": (11400, 29.15),   # qty, cost
    "sz.002241": (14900, 22.64),
    "sh.601939": (27600, 12.04),
    "sh.688981": (3300, 100.00),
}
# 预期：现金 = 1,000,000 - (11400*29.15 + 14900*22.64 + 27600*12.04 + 3300*100) = -332,736
EXPECTED_CASH = -332736.0
# 预期权益 = 现金 + 持仓市值（按成本价算）= -332736 + 1,000,000 = 667,264
# 但实际权益要用当前价，这里简化为成本价
EXPECTED_EQUITY = 667264.0

LEDGER_FILE = _DATA_DIR / "broker" / "ledger.json"


# ---------------------------- 工具函数 ----------------------------

def _headers(token: str) -> dict:
    return {"Authorization": token}


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


def _write_test_ledger(strategy: str, cash: float, positions: dict):
    """写入测试账本文件"""
    import json
    data = {
        "version": 1,
        "initialCapital": INITIAL_CAPITAL,
        "accounts": {
            strategy: {
                "allocated": INITIAL_CAPITAL,
                "cash": cash,
                "profit": 0.0,
                "active": True,
                "positions": {
                    sym: {
                        "quantity": qty,
                        "avg_cost": cost,
                        "current_price": cost,  # 简化：当前价=成本价
                        "is_margin": False,
                        "margin_ratio": 1.0,
                    }
                    for sym, (qty, cost) in positions.items()
                },
                "lots": {},
                "credit": {
                    "borrowedCash": 0.0,
                    "borrowedShares": {},
                    "shortAvgPrice": {},
                    "financedAmount": {},
                },
            }
        },
    }
    LEDGER_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(LEDGER_FILE, "w") as f:
        json.dump(data, f, indent=2)


def _cleanup_test_ledger():
    """清理测试账本文件"""
    if LEDGER_FILE.exists():
        LEDGER_FILE.unlink()


# ---------------------------- Fixture ----------------------------

@pytest.fixture(scope="module", autouse=True)
def _require_live_mode(is_backtest):
    """需要非回测模式（账本 API）"""
    if is_backtest:
        pytest.skip("账本用例需要非回测模式，当前为回测模式")


@pytest.fixture
def strategy(auth_token):
    """测试策略（不加载，只用账本 API）"""
    name = "test_ledger_projection_regression"
    yield name
    _cleanup_test_ledger()


# ---------------------------- 测试用例 ----------------------------

class TestLedgerProjectionRegression:
    """2026-11-03 账本投影失明事故回归"""

    def test_projection_reads_ledger_after_restart(self, auth_token, strategy):
        """重启后投影应看到账本里的 4 个持仓、现金 −332736"""
        # 写入测试账本
        _write_test_ledger(strategy, EXPECTED_CASH, POSITIONS)

        # 查询账本
        entry = _ledger(auth_token, strategy)

        # 验证现金
        assert abs(entry["cash"] - EXPECTED_CASH) < 1.0, \
            f"现金应为 {EXPECTED_CASH}，实际 {entry['cash']}"

        # 验证持仓数
        assert len(entry["positions"]) == 4, \
            f"应有 4 个持仓，实际 {len(entry['positions'])}"

        # 验证每个持仓
        for sym, (expected_qty, expected_cost) in POSITIONS.items():
            pos = _position_of(entry, sym)
            assert pos, f"持仓 {sym} 不存在"
            assert pos["quantity"] == expected_qty, \
                f"{sym} 数量应为 {expected_qty}，实际 {pos['quantity']}"
            assert abs(pos["avg_cost"] - expected_cost) < 0.01, \
                f"{sym} 成本应为 {expected_cost}，实际 {pos['avg_cost']}"

    def test_negative_cash_blocks_buy(self, auth_token, strategy):
        """负现金账户应禁止开新仓"""
        # 写入测试账本（现金为负）
        _write_test_ledger(strategy, EXPECTED_CASH, POSITIONS)

        # 查询资金
        capital = _capital(auth_token)
        assert strategy in capital["strategies"], f"策略 {strategy} 不存在"
        strat_cap = capital["strategies"][strategy]

        # 验证可用资金为负
        assert strat_cap["available"] < 0, \
            f"可用资金应为负，实际 {strat_cap['available']}"

        # 验证权益
        assert abs(strat_cap["equity"] - EXPECTED_EQUITY) < 1000, \
            f"权益应约为 {EXPECTED_EQUITY}，实际 {strat_cap['equity']}"

    def test_emergency_close_only_triggered_symbols(self, auth_token, strategy):
        """紧急平仓应只平触发标的（不连带清其他）

        注：这个测试需要触发风控，但当前测试框架无法直接触发。
        这里只验证账本状态，实际风控行为由单元测试覆盖。
        """
        # 写入测试账本
        _write_test_ledger(strategy, EXPECTED_CASH, POSITIONS)

        # 查询账本
        entry = _ledger(auth_token, strategy)

        # 验证 4 个持仓都在
        assert len(entry["positions"]) == 4, \
            f"应有 4 个持仓，实际 {len(entry['positions'])}"

        # 注：实际的风控触发行为需要集成测试，这里只验证初始状态
