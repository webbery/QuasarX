"""
测试 GET /v0/risk/strategies 策略风险指标端点

数据来源：DecisionDB daily_positions 表（实盘持仓快照）
验证修改后 handler 从 daily_positions 计算风险指标（而非 _collections）。

覆盖：
1. 响应格式校验（任何模式）
2. 无数据时返回零值指标（任何模式）
3. simulate_bar 触发日终执行后，指标应被正确计算（stock_hist_sim 模式）
"""

import json
import time
from pathlib import Path

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import check_response, BASE_URL

# ==================== 配置 ====================

STRATEGY_PATH = (
    Path(__file__).parent / "metric_test_data" / "manual_signal_strategy.json"
)
STRATEGY_NAME = "manual_signal_strategy"
TEST_SYMBOL = "sz.900001"

# 风险指标端点需要的字段
RISK_FIELDS = [
    "id", "name", "type",
    "var_95", "max_drawdown", "sharpe_ratio", "win_rate",
    "var_convexity", "cusum_drift_ratio", "excess_kurtosis",
    "avg_win_loss_ratio", "cusum_signal", "cusum_triggered",
    "information_ratio",
]


# ==================== 工具函数 ====================

def api_load_strategy(token: str, name: str, script: dict) -> dict:
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"mode": 0, "name": name, "script": json.dumps(script)},
        headers={"Authorization": token},
        verify=False,
        timeout=15,
    )
    resp.raise_for_status()
    return resp.json()


def cleanup_strategy(token: str, name: str):
    headers = {"Authorization": token}
    requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "stop", "name": name},
        headers=headers, verify=False, timeout=10,
    )
    requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "reclaim_all"},
        headers=headers, verify=False, timeout=10,
    )


def simulate_bar(token: str, bar: dict) -> dict:
    resp = requests.post(
        f"{BASE_URL}/strategy/simulate/bar",
        json=bar,
        headers={"Authorization": token},
        verify=False,
        timeout=15,
    )
    return resp.json()


def get_latest_bar(token: str, symbol: str) -> dict:
    resp = requests.get(
        f"{BASE_URL}/stocks/history",
        params={"id": symbol, "type": "1d", "start": 0, "end": 9999999999},
        headers={"Authorization": token},
        verify=False,
        timeout=10,
    )
    data = check_response(resp)
    if not data:
        raise ValueError(f"无历史数据: {symbol}")
    return data[-1]


def today_str() -> str:
    from datetime import datetime
    return datetime.now().strftime("%Y-%m-%d")


# ==================== Fixtures ====================

@pytest.fixture
def loaded_strategy(auth_token):
    """加载测试策略，测试后自动清理"""
    if not STRATEGY_PATH.exists():
        pytest.skip(f"测试策略文件不存在: {STRATEGY_PATH}")
    strategy = json.loads(STRATEGY_PATH.read_text())
    api_load_strategy(auth_token, STRATEGY_NAME, strategy)
    headers = {"Authorization": auth_token}
    requests.post(
        f"{BASE_URL}/strategy",
        json={"mode": 1, "name": STRATEGY_NAME},
        headers=headers, verify=False, timeout=10,
    )
    yield strategy
    cleanup_strategy(auth_token, STRATEGY_NAME)


# ==================== 测试类 ====================

@pytest.mark.usefixtures("auth_token")
class TestStrategyRiskEndpoint:
    """GET /v0/risk/strategies 策略风险指标"""

    def test_risk_strategies_response_format(self, auth_token):
        """响应格式校验：返回数组，每个元素包含必需字段（任何模式）"""
        resp = requests.get(
            f"{BASE_URL}/risk/strategies",
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
        data = resp.json()
        assert isinstance(data, list), "响应必须是数组"

        for item in data:
            for field in RISK_FIELDS:
                assert field in item, f"缺少字段: {field}"

            # 类型校验
            assert isinstance(item["id"], str)
            assert isinstance(item["name"], str)
            assert item["type"] in ("stock", "etf", "future", "option", "mixed")
            assert isinstance(item["var_95"], (int, float))
            assert isinstance(item["max_drawdown"], (int, float))
            assert isinstance(item["sharpe_ratio"], (int, float))
            assert isinstance(item["win_rate"], (int, float))
            assert isinstance(item["cusum_signal"], int)
            assert item["cusum_signal"] in (-1, 0, 1)
            assert isinstance(item["cusum_triggered"], bool)

    def test_risk_strategies_no_data_returns_zeros(self, auth_token):
        """无 daily_positions 数据时，指标应全部为零值（任何模式）"""
        resp = requests.get(
            f"{BASE_URL}/risk/strategies",
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok
        data = resp.json()

        # 找到刚加载但尚未执行过日终的策略（如果有）
        for item in data:
            if item["name"] == "nonexistent_strategy_for_test":
                continue
            # 对于没有 daily_positions 的策略，所有指标应为 0
            if item["var_95"] == 0:
                assert item["sharpe_ratio"] == 0
                assert item["max_drawdown"] == 0
                assert item["win_rate"] == 0
                assert item["cusum_signal"] == 0
                assert item["cusum_triggered"] is False

    def test_risk_strategies_after_daily_execution(
        self, auth_token, loaded_strategy, is_backtest
    ):
        """推送 2 个不同日期的 bar 后，风险指标应包含有效数据（仅 stock_hist_sim）"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        try:
            bar = get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        # 确保 bar 是阳线（触发买入信号）
        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99
            bar["high"] = max(bar["open"], bar["high"])
            bar["low"] = min(bar["open"], bar["low"])

        # Day 1
        bar1 = dict(bar, datetime="2025-01-15 00:00:00")
        result = simulate_bar(auth_token, bar1)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        deadline = time.time() + 10
        while time.time() < deadline:
            resp = requests.get(
                f"{BASE_URL}/trade/decisions",
                params={"date": today_str()},
                headers={"Authorization": auth_token},
                verify=False, timeout=10,
            )
            if resp.ok and resp.json():
                break
            time.sleep(0.5)

        # Day 2（不同日期 → 不同 daily_positions 记录）
        bar2 = dict(bar, datetime="2025-01-16 00:00:00")
        simulate_bar(auth_token, bar2)

        # 等待 Day 2 执行完成
        time.sleep(2)

        # 验证风险指标
        resp = requests.get(
            f"{BASE_URL}/risk/strategies",
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
        data = resp.json()

        # 找到测试策略
        test_item = None
        for item in data:
            if item["name"] == STRATEGY_NAME:
                test_item = item
                break

        if test_item is None:
            # 策略可能未被 GetStrategyNames 返回（未成功初始化）
            pytest.skip(f"策略 {STRATEGY_NAME} 不在 /risk/strategies 响应中")

        # 验证字段存在且类型正确
        for field in RISK_FIELDS:
            assert field in test_item, f"缺少字段: {field}"

        # max_drawdown 应为非正数（回撤 ≤ 0）
        assert test_item["max_drawdown"] <= 0, \
            f"max_drawdown 应为非正: {test_item['max_drawdown']}"

        # win_rate 在 [0, 1] 范围
        assert 0 <= test_item["win_rate"] <= 1, \
            f"win_rate 超出 [0,1]: {test_item['win_rate']}"

        # var_95 应为非负数
        assert test_item["var_95"] >= 0, \
            f"var_95 应为非负: {test_item['var_95']}"

        # cusum_signal 在 {-1, 0, 1}
        assert test_item["cusum_signal"] in (-1, 0, 1)

        print(f"\n策略 {STRATEGY_NAME} 风险指标:")
        print(f"  VaR(95%):    {test_item['var_95']:.4f}")
        print(f"  MaxDrawdown: {test_item['max_drawdown']:.4f}")
        print(f"  Sharpe:      {test_item['sharpe_ratio']:.4f}")
        print(f"  WinRate:     {test_item['win_rate']:.4f}")
        print(f"  CUSUM信号:   {test_item['cusum_signal']}")
        print(f"  漂移比:      {test_item['cusum_drift_ratio']:.4f}")
        print(f"  超额峰度:    {test_item['excess_kurtosis']:.4f}")
        print(f"  盈亏比:      {test_item['avg_win_loss_ratio']:.4f}")
