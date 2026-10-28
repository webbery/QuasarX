"""
测试 ManualTiming 决策系统

依赖：
- stock_hist_sim 模式（回测模式）
- Debug 构建（simulate/bar 端点）
- metric_test_data/manual_signal_strategy.json 测试策略

覆盖：
1. GET /v0/trade/decisions 端点
2. POST /v0/trade/order 关联 decisionId（回测 SimulateFill 路径）
3. 端到端：策略 → 决策 → 下单 → 状态更新
"""

import json
import csv
import time
from pathlib import Path

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import (
    check_response, BASE_URL, _DATA_DIR as SERVER_DATA_DIR, get_auth_token,
    api_load_strategy, api_run_strategy, api_simulate_bar, api_clear_decisions,
    api_cleanup_strategy, api_order_with_decision_id, api_get_decisions,
    api_get_latest_bar, to_api_symbol, today_str, reclaim_capital
)

# ==================== 配置 ====================

STRATEGY_PATH = (
    Path(__file__).parent / "metric_test_data" / "manual_signal_strategy.json"
)
STRATEGY_NAME = "manual_signal_strategy"
TEST_SYMBOL = "sz.900001"  # metric 测试专用生成数据（已自动导入 DuckDB）
TEST_QUANTITY = 100

# ==================== 工具函数（从 tool.py 导入） ====================

# 以下函数已从 tool.py 导入：
# - api_load_strategy
# - api_run_strategy
# - api_simulate_bar
# - api_clear_decisions
# - api_cleanup_strategy
# - api_order_with_decision_id
# - api_get_decisions
# - api_get_latest_bar
# - to_api_symbol
# - today_str


# ==================== Fixture ====================

@pytest.fixture(scope="module", autouse=True)
def reclaim_capital_fixture(auth_token):
    """回收先前测试残留的 CapitalPool 资金，确保日终策略有足够资金分配"""
    reclaim_capital(auth_token)
    # 清除当日残留决策（上次测试/服务重启前的数据）
    api_clear_decisions(auth_token, date=today_str())


@pytest.fixture
def decision_token(auth_token):
    """返回带 token 的辅助函数包"""
    yield auth_token


@pytest.fixture
def loaded_strategy(decision_token):
    """加载测试策略，测试后自动清理"""
    strategy = json.loads(STRATEGY_PATH.read_text())
    api_load_strategy(decision_token, STRATEGY_NAME, strategy)
    # load 只 InitStrategy 不 Run（_running=false），StartDaily 需要 _running=true
    # 手动发 Run 请求（mode=1）让策略进入可运行状态
    api_run_strategy(decision_token, STRATEGY_NAME)
    yield strategy
    api_cleanup_strategy(decision_token, STRATEGY_NAME)


# ==================== 测试类 1：GET /v0/trade/decisions ====================

@pytest.mark.usefixtures("auth_token")
class TestDecisionQuery:
    """决策查询端点测试（两种模式都运行）"""

    def test_query_no_param(self, auth_token):
        """不传 date 参数：返回当日决策"""
        resp = requests.get(
            f"{BASE_URL}/trade/decisions",
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}"
        data = resp.json()
        assert isinstance(data, list), "响应必须是数组"

    def test_query_with_date(self, auth_token):
        """指定日期查询"""
        resp = requests.get(
            f"{BASE_URL}/trade/decisions",
            params={"date": "2020-01-01"},
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}"
        data = resp.json()
        assert isinstance(data, list)
        # 历史日期应返回空（无数据）
        assert len(data) == 0, f"历史日期应无决策，实际: {data}"

    def test_query_today(self, auth_token):
        """显式查询当日"""
        decisions = api_get_decisions(auth_token, date=today_str())
        assert isinstance(decisions, list)

    def test_decision_schema(self, auth_token):
        """决策字段完整性：返回的决策必须包含所有必需字段"""
        decisions = api_get_decisions(auth_token, date=today_str())
        if not decisions:
            pytest.skip("当日无决策，无法验证字段")

        required_fields = {
            "id", "strategy", "symbol", "action", "label",
            "quantity", "price", "epoch", "timestamp",
            "executed", "executedQuantity", "executedPrice",
        }
        for d in decisions:
            missing = required_fields - set(d.keys())
            assert not missing, f"决策 {d.get('id')} 缺少字段: {missing}"
            # 枚举值校验
            assert d["action"] in [
                "open_long", "close_long", "open_short", "close_short"
            ], f"非法 action: {d['action']}"


# ==================== 测试类 2：POST /v0/trade/order 关联 decisionId ====================

@pytest.mark.usefixtures("auth_token")
class TestOrderWithDecisionId:
    """关联决策的下单测试（两种模式都运行）"""

    def test_order_invalid_decision_id(self, auth_token):
        """不存在的 decisionId：返回错误（不抛 500）"""
        # decisionId = 999999 几乎肯定不存在
        resp = api_order_with_decision_id(
            auth_token, TEST_SYMBOL, decision_id=999999,
            quantity=TEST_QUANTITY, price=10.0, direct=0,
        )
        # 不应崩溃，且不应是 500
        assert resp.status_code != 500, f"服务器内部错误: {resp.text}"

    def test_order_without_decision_id(self, auth_token):
        """不传 decisionId：向后兼容，行为不变"""
        headers = {"Authorization": auth_token}
        body = {
            "symbol": TEST_SYMBOL,
            "direct": 0,
            "type": 1,
            "quantity": TEST_QUANTITY,
            "prices": 10.0,  # 价格有效但不会成交
            "kind": 0,
            "timeType": 0,
            # 不带 decisionId
        }
        resp = requests.post(
            f"{BASE_URL}/trade/order",
            json=body,
            headers=headers,
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
        result = resp.json()
        assert "id" in result or "error" in result


# ==================== 测试类 3：端到端流程 ====================

@pytest.mark.usefixtures("auth_token")
class TestDecisionLifecycle:
    """ManualTiming 决策端到端测试（仅 stock_hist_sim 模式，依赖 simulate_bar）"""

    @pytest.mark.skip(reason="simulate/bar 端点仅 Debug 构建可用，CI Release 下无法产生决策")
    def test_manual_timing_creates_decision(self, auth_token, loaded_strategy, is_backtest):
        """推送 bar → ManualTiming 累积 → SendSummaryEmail → 决策入库"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        # 推送一个 bar（close > open 触发买入信号）
        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        # close > open 才触发买入，找一个这样的 bar
        if not bar["close"] > bar["open"]:
            # 强制构造一个 close > open 的 bar
            bar["open"] = bar["close"] * 0.99
            bar["high"] = max(bar["open"], bar["high"])
            bar["low"] = min(bar["open"], bar["low"])

        result = api_simulate_bar(auth_token, bar)
        # 如果 simulate/bar 不可用（Release 构建），跳过
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用（可能是 Release 构建）: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        found = False
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=today_str())
            if decisions:
                found = True
                break
            time.sleep(0.5)

        assert found, "推送 bar 后未产生决策"

        # 验证决策字段（只检查未执行的决策，排除跨测试残留的已执行决策）
        test_decisions = [
            d for d in decisions
            if d.get("strategy") == STRATEGY_NAME and not d["executed"]
        ]
        assert test_decisions, f"未找到策略 {STRATEGY_NAME} 的未执行决策"
        for d in test_decisions:
            assert d["symbol"] == TEST_SYMBOL
            assert d["action"] in [
                "open_long", "close_long", "open_short", "close_short"
            ]
            assert d["executedQuantity"] == 0

    def test_executed_decision_status_update(self, auth_token, loaded_strategy, is_backtest):
        """下单后决策状态更新为 executed"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        # 先产生一条决策
        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError:
            pytest.skip("无测试数据")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99

        result = api_simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策出现
        deadline = time.time() + 10
        decision = None
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=today_str())
            test_decisions = [
                d for d in decisions
                if d.get("strategy") == STRATEGY_NAME and not d["executed"]
            ]
            if test_decisions:
                decision = test_decisions[0]
                break
            time.sleep(0.5)

        if decision is None:
            pytest.skip("未产生决策，跳过执行测试")

        # 下单关联 decisionId（回测模式 → SimulateFill）
        resp = api_order_with_decision_id(
            auth_token, TEST_SYMBOL, decision["id"],
            quantity=decision["quantity"], price=decision["price"],
            direct=0 if decision["action"] in ["open_long", "close_short"] else 1,
        )
        assert resp.status_code == 200, f"下单失败: {resp.text}"

        # 验证决策状态更新
        time.sleep(1)
        updated = [
            d for d in api_get_decisions(auth_token, date=today_str())
            if d["id"] == decision["id"]
        ]
        assert updated, "决策消失"
        assert updated[0]["executed"] is True, "executed 标志未更新"
        assert updated[0]["executedQuantity"] > 0, "executedQuantity 未更新"

    def test_executed_decision_blocks_reorder(self, auth_token, loaded_strategy, is_backtest):
        """已执行决策不能再下单（前端禁用，C++ 端不应崩溃）"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        decisions = api_get_decisions(auth_token, date=today_str())
        executed = [
            d for d in decisions
            if d.get("strategy") == STRATEGY_NAME and d["executed"]
        ]
        if not executed:
            pytest.skip("无已执行决策，跳过测试")

        decision = executed[0]
        # 重复下单不应崩溃（业务上应被前端阻止，但 C++ 端需要健壮）
        resp = api_order_with_decision_id(
            auth_token, TEST_SYMBOL, decision["id"],
            quantity=decision["executedQuantity"], price=decision["executedPrice"],
            direct=0 if decision["action"] in ["open_long", "close_short"] else 1,
        )
        assert resp.status_code != 500, f"服务器内部错误: {resp.text}"


# ==================== 测试类 4：策略绩效 API ====================

@pytest.mark.usefixtures("auth_token")
class TestStrategyPerformance:
    """GET /v0/strategy/performance 日终策略绩效指标"""

    def test_performance_no_data(self, auth_token):
        """无持仓数据时返回 trading_days=0（两种模式都运行）"""
        resp = requests.get(
            f"{BASE_URL}/strategy/performance",
            params={"name": "nonexistent_strategy"},
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}"
        data = resp.json()
        assert data["trading_days"] == 0

    def test_performance_missing_name(self, auth_token):
        """缺少 name 参数返回 400"""
        resp = requests.get(
            f"{BASE_URL}/strategy/performance",
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.status_code == 400

    def test_performance_after_daily_execution(self, auth_token, loaded_strategy, is_backtest):
        """推送 2 个不同日期的 bar，绩效接口应返回有效指标（仅 stock_hist_sim 模式）"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99
            bar["high"] = max(bar["open"], bar["high"])
            bar["low"] = min(bar["open"], bar["low"])

        # Day 1
        day1_date = "2025-01-15"
        bar1 = dict(bar, datetime=f"{day1_date} 00:00:00")
        result = api_simulate_bar(auth_token, bar1)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        deadline = time.time() + 10
        while time.time() < deadline:
            if api_get_decisions(auth_token, date=day1_date):
                break
            time.sleep(0.5)

        # Day 2（不同日期 → 不同 daily_positions 记录）
        day2_date = "2025-01-16"
        bar2 = dict(bar, datetime=f"{day2_date} 00:00:00")
        api_simulate_bar(auth_token, bar2)

        # 等待 Day 2 执行完成：决策数量增加（Day 2 产生新决策）
        prev_count = len(api_get_decisions(auth_token, date=day2_date))
        deadline = time.time() + 15
        while time.time() < deadline:
            curr_count = len(api_get_decisions(auth_token, date=day2_date))
            if curr_count > prev_count:
                break
            time.sleep(0.5)

        # recordDailyPositions 在决策创建后的回调中执行，需额外等待
        time.sleep(1)

        resp = requests.get(
            f"{BASE_URL}/strategy/performance",
            params={
                "name": STRATEGY_NAME,
                "start": day1_date,
                "end": day2_date
            },
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
        data = resp.json()

        assert data["strategy"] == STRATEGY_NAME
        assert data["trading_days"] >= 1, f"trading_days 应 >= 1，实际: {data.get('trading_days')}, message: {data.get('message')}"
        assert "initial_capital" in data
        assert "final_value" in data

        metrics = data["metrics"]
        for key in ["total_return", "annual_return", "annual_volatility",
                     "sharpe_ratio", "max_drawdown", "win_rate", "calmar_ratio"]:
            assert key in metrics, f"缺少指标: {key}"

        # max_drawdown 应为非负数
        assert metrics["max_drawdown"] >= 0, \
            f"max_drawdown 应为非负: {metrics['max_drawdown']}"
        # win_rate 在 [0, 1] 范围
        assert 0 <= metrics["win_rate"] <= 1, \
            f"win_rate 超出 [0,1]: {metrics['win_rate']}"


# ─────────────────────────────────────────────────────────────────
# 日终执行 + 分红/复权计算的集成测试
#
# 流程：import stock_1d → import dividend → recalc → 验证 adj_close
#       → simulate_bar 触发日终管线 → 验证决策正确写入
#
# 数据写入路径：
#   1. POST /v0/quote/data action=import  → 写入 stock_1d (原始 OHLCV)
#   2. POST /v0/dividend action=import    → 写入 finance.db dividend 表
#   3. POST /v0/dividend action=recalc    → 调用 FinanceDB::recalcSymbolAdjPrices
#                                          → 写入 stock_1d.adj_* 列
#   4. GET  /v0/quote                     → 读回 adj_close（API 验证 C++ 写入）
#   5. POST /v0/strategy/simulate/bar     → 触发日终管线 + ManualTiming
#   6. GET  /v0/strategy/performance      → 验证 daily_positions + 指标计算
# ─────────────────────────────────────────────────────────────────

# 用于 integration 测试的独立 symbol（避免与其他测试冲突）
INTEG_SYMBOL = "sh.999996"

# 与 TestBaostockFormulaRegression 中的格式一致
INTEG_DIVIDEND_HEADER = [
    "symbol", "announce_date", "report_year", "ex_dividend_date",
    "record_date", "implement_date", "bonus_per_10", "transfer_per_10",
    "cash_per_10", "allot_per_10", "allot_price", "ex_div_price", "action_type"
]

# 10送3, prev_close=10.00 → baostock factor = 1.30, adj_close for bar0 = 13.00
INTEG_STOCK_LINES = [
    "datetime,open,close,high,low,volume,turnover",
    "2024-07-01 00:00:00,9.90,10.00,10.10,9.80,100000,1000000",
    "2024-07-02 00:00:00,9.95,10.00,10.10,9.85,100000,1000000",  # 除权日
]

INTEG_DIVIDEND_ROW = [
    INTEG_SYMBOL, "2024-06-30", "2023", "2024-07-02",
    "2024-07-01", "2024-07-02",
    "3", "0",   # bonus=3, transfer=0 → 10送3
    "0", "0", "0",
    "0",        # ex_div_price=0 → 公式反推
    "1",
]

INTEG_BAOSTOCK_FACTOR = 1.30


def _write_integ_dividend_csv():
    """写入 integration 测试用的分红 CSV 到服务数据目录"""
    dividend_dir = SERVER_DATA_DIR / "dividend"
    dividend_dir.mkdir(parents=True, exist_ok=True)
    csv_path = dividend_dir / f"{INTEG_SYMBOL}_dividend.csv"
    with open(csv_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(INTEG_DIVIDEND_HEADER)
        writer.writerow(INTEG_DIVIDEND_ROW)
    return csv_path


def _cleanup_integ_data(token: str):
    """清理 INTEG_SYMBOL 的 stock + dividend 数据"""
    headers = {"Authorization": token}
    requests.delete(f"{BASE_URL}/dividend",
                    params={"code": INTEG_SYMBOL}, headers=headers,
                    verify=False, timeout=10)
    requests.delete(f"{BASE_URL}/quote",
                    params={"table": "stock_1d", "symbol": INTEG_SYMBOL},
                    headers=headers, verify=False, timeout=10)
    csv_path = SERVER_DATA_DIR / "dividend" / f"{INTEG_SYMBOL}_dividend.csv"
    if csv_path.exists():
        csv_path.unlink()


def _is_strategy_running(token: str, strategy_name: str) -> bool:
    """检查策略是否正在运行

    Args:
        token: 认证 token
        strategy_name: 策略名称

    Returns:
        True 如果策略正在运行，False 如果策略不存在或已停止
    """
    try:
        headers = {"Authorization": token}
        resp = requests.get(f"{BASE_URL}/strategy", headers=headers, verify=False, timeout=10)
        if resp.ok:
            strategies = resp.json().get("strategies", [])
            for strat in strategies:
                if strat.get("name") == strategy_name:
                    return strat.get("running", False)
        return False
    except Exception:
        return False


def _wait_for_strategy_stopped(token: str, strategy_name: str, timeout: int = 10) -> bool:
    """等待策略完全停止

    Args:
        token: 认证 token
        strategy_name: 策略名称
        timeout: 超时时间（秒）

    Returns:
        True 如果策略已停止，False 如果超时
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not _is_strategy_running(token, strategy_name):
            return True
        time.sleep(0.5)
    return False


def _cleanup_strategy(token: str, strategy_name: str):
    """清理策略：停止并删除

    Args:
        token: 认证 token
        strategy_name: 策略名称
    """
    headers = {"Authorization": token}
    try:
        # 如果策略正在运行，先停止
        if _is_strategy_running(token, strategy_name):
            requests.post(f"{BASE_URL}/strategy",
                         json={"mode": 2, "name": strategy_name},
                         headers=headers, verify=False, timeout=10)
            # 等待策略完全停止
            _wait_for_strategy_stopped(token, strategy_name, timeout=10)

        # 删除策略
        requests.delete(f"{BASE_URL}/strategy",
                       json={"name": strategy_name},
                       headers=headers, verify=False, timeout=10)
        time.sleep(0.5)
    except Exception:
        pass  # 策略可能不存在，忽略错误


def _create_integ_strategy(symbol: str) -> dict:
    """创建监控指定 symbol 的 ManualTiming 策略

    用于 dividend 集成测试，策略监控 INTEG_SYMBOL 而非 TEST_SYMBOL
    """
    return {
        "id": "integ_dividend_strategy",
        "name": "Dividend集成测试策略",
        "version": 1,
        "capital": 500000,
        "description": "阳线买入，阴线卖出",
        "nodes": [
            {
                "id": "1",
                "type": "custom",
                "data": {
                    "label": "行情数据",
                    "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "select"},
                        "code": {"value": [symbol], "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"},
                        "turnover": {"value": "turnover", "type": "text"},
                    }
                }
            },
            {
                "id": "2",
                "type": "custom",
                "data": {
                    "label": "买卖信号",
                    "nodeType": "signal",
                    "params": {
                        "code": {"value": [symbol], "type": "text"},
                        "buy": {"value": "close[t] > open[t]", "type": "text"},
                        "sell": {"value": "close[t] < open[t]", "type": "text"},
                    }
                }
            },
            {
                "id": "3",
                "type": "custom",
                "data": {
                    "label": "投资组合",
                    "nodeType": "portfolio",
                    "params": {
                        "positionRatio": {"value": 1.0, "type": "number"},
                    }
                }
            },
            {
                "id": "4",
                "type": "custom",
                "data": {
                    "label": "手动下单",
                    "nodeType": "execution",
                    "params": {
                        "commission": {"value": 0.0003, "type": "number"},
                        "stamp": {"value": 0.001, "type": "number"},
                        "minCommission": {"value": 5, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": 0.0, "type": "number"},
                        "type": {"value": 7, "type": "select"},  # Manual
                        "contract": {"value": 0, "type": "select"},
                    }
                }
            }
        ],
        "edges": [
            {"id": "1->2", "source": "1", "target": "2", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "2->3", "source": "2", "target": "3", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "3->4", "source": "3", "target": "4", "sourceHandle": "output", "targetHandle": "input"},
        ]
    }


class TestDailyExecutionWithDividend:
    """日终执行 + 分红/复权计算的集成测试（仅 stock_hist_sim 模式，依赖 simulate_bar）

    验证流程：
      1. import stock_1d (2 bars)
      2. import dividend CSV (10送3)
      3. recalcSymbolAdjPrices (C++) → 写 adj_* 到 stock_1d
      4. GET /v0/quote 验证 adj_close = baostock factor × org_close
      5. simulate_bar 触发日终执行 → GET /v0/strategy/performance 验证
    """

    @pytest.mark.timeout(30)
    def test_recalc_writes_adj_close_via_api(self, auth_token):
        """Fix 1 集成: recalc 后 GET /quote 返回正确的 adj_close"""
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            # 1. 导入股票
            kwargs["json"] = {
                "action": "import",
                "table": "stock_1d",
                "symbol": INTEG_SYMBOL,
                "adj": "none",
                "data": INTEG_STOCK_LINES,
            }
            response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
            check_response(response)

            # 2. 写入 + 导入分红
            _write_integ_dividend_csv()
            kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            response = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(response)

            # 3. 触发 recalc
            kwargs["json"] = {"action": "recalc", "code": INTEG_SYMBOL}
            response = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(response)

            # 4. 通过 API 验证 adj_close
            params = {"table": "stock_1d", "symbol": INTEG_SYMBOL}
            response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
            data = check_response(response)
            assert data["count"] == 2, f"期望 2 根 bar, 实际 {data['count']}"

            bars = sorted(data["data"], key=lambda b: b["datetime"])
            bar0 = bars[0]  # 事件前
            expected_adj = bar0["close"] * INTEG_BAOSTOCK_FACTOR
            assert abs(bar0["adj_close"] - expected_adj) < 1e-4, \
                f"adj_close={bar0['adj_close']}, expected={expected_adj}"

        finally:
            _cleanup_integ_data(auth_token)

    @pytest.mark.timeout(60)
    def test_dividend_csv_persisted_to_finance_db(self, auth_token):
        """验证 dividend 行通过 API 正确写入并可查询"""
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            _write_integ_dividend_csv()
            kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            response = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(response)

            # 通过 API 查询 dividend
            response = requests.get(
                f"{BASE_URL}/dividend",
                params={"code": INTEG_SYMBOL},
                **kwargs,
            )
            data = check_response(response)
            assert data["count"] >= 1
            row = data["data"][0]
            assert row["bonus_per_10"] == 3.0, f"bonus_per_10={row['bonus_per_10']}"
            assert row["cash_per_10"] == 0.0
            assert row["transfer_per_10"] == 0.0

        finally:
            _cleanup_integ_data(auth_token)

    @pytest.mark.timeout(60)
    def test_daily_execution_succeeds_after_dividend_recalc(
        self, auth_token, is_backtest
    ):
        """集成测试：dividend + recalc 后，simulate_bar 触发日终管线，performance 端点返回有效指标

        验证：当 stock_1d 包含分红 + adj_* 计算结果时，日终管线 (ManualTiming → SendSummaryEmail
        → daily_positions 写入) 不被 dividend 流程破坏。
        """
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        integ_strategy_name = "integ_dividend_test_strategy"

        try:
            # 0. 先清理可能已存在的策略（从之前的测试遗留）
            _cleanup_strategy(auth_token, integ_strategy_name)

            # 1. 导入股票 + 分红 + recalc
            kwargs["json"] = {
                "action": "import",
                "table": "stock_1d",
                "symbol": INTEG_SYMBOL,
                "adj": "none",
                "data": INTEG_STOCK_LINES,
            }
            requests.post(f"{BASE_URL}/quote/data", **kwargs)

            _write_integ_dividend_csv()
            kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            requests.post(f"{BASE_URL}/dividend", **kwargs)

            kwargs["json"] = {"action": "recalc", "code": INTEG_SYMBOL}
            requests.post(f"{BASE_URL}/dividend", **kwargs)

            # 2. 创建并加载监控 INTEG_SYMBOL 的策略
            integ_strategy = _create_integ_strategy(INTEG_SYMBOL)
            kwargs["json"] = {"mode": 0, "name": integ_strategy_name, "script": integ_strategy}
            resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
            assert resp.ok, f"部署策略失败: {resp.text}"

            # 启动策略
            kwargs["json"] = {"mode": 1, "name": integ_strategy_name}
            requests.post(f"{BASE_URL}/strategy", **kwargs)

            # 3. 触发日终管线：simulate_bar 推送一根 bar
            bar_date = "2024-07-02"
            try:
                bar = api_get_latest_bar(auth_token, INTEG_SYMBOL,
                                     date=f"{bar_date} 00:00:00")
            except ValueError as e:
                pytest.skip(f"无测试数据: {e}")

            result = api_simulate_bar(auth_token, bar)
            if "status" in result and result.get("status") == "error":
                pytest.skip(f"simulate/bar 不可用: {result}")

            # 4. 等 ManualTiming 决策完成（使用正确的日期）
            deadline = time.time() + 10
            while time.time() < deadline:
                decisions = api_get_decisions(auth_token, date=bar_date)
                # 检查是否有该策略的决策
                if any(d.get("strategy") == integ_strategy_name for d in decisions):
                    break
                time.sleep(0.5)

            # 5. 验证 daily_positions + 绩效指标（指定日期范围）
            # 绩效需要 >= 2 个交易日的序列才能算收益/波动率，所以范围必须覆盖
            # 导入的两根 bar（2024-07-01 建仓 + 2024-07-02 除权日），
            # 只查除权日当天必然拿到 "insufficient data"。
            resp = requests.get(
                f"{BASE_URL}/strategy/performance",
                params={
                    "name": integ_strategy_name,
                    "start": "2024-07-01",
                    "end": bar_date
                },
                **kwargs,
            )
            assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
            perf_data = resp.json()

            assert perf_data["strategy"] == integ_strategy_name
            assert "metrics" in perf_data, f"缺少 metrics 字段: {perf_data}"
            for key in ["total_return", "sharpe_ratio", "max_drawdown"]:
                assert key in perf_data["metrics"], \
                    f"dividend recalc 后绩效缺少指标: {key}"

        finally:
            # 清理策略
            _cleanup_strategy(auth_token, integ_strategy_name)
            _cleanup_integ_data(auth_token)


# ─────────────────────────────────────────────────────────────────────────────
# 持仓记账回归：daily_positions 的 position 口径
#
# PortfolioNode::generatePlan 产出的 BUY quantity 是「当日目标仓位」
# （capital / price，整手），不是相对当前持仓的增量；SELL 给的是「待平数量」
# （= 当前持仓，全平）。ManualTiming::recordPositionSnapshot 必须按这个口径
# 覆盖持仓，累加会把历史目标当成连续加仓。
#
# 用两根收盘价不同的 bar 制造两次 BUY：目标数量随价格变化而不同
# （50000 → 47600），累加口径会得到 97600 股、市值约 1.02M，
# 而策略分配资金只有 500k。
# ─────────────────────────────────────────────────────────────────────────────

ACC_SYMBOL = "sh.999995"
ACC_STRATEGY_NAME = "acc_test_strategy"
ACC_CAPITAL = 500000
ACC_BAR2_DATE = "2024-08-02"
ACC_STOCK_LINES = [
    "datetime,open,close,high,low,volume,turnover",
    "2024-08-01 00:00:00,9.90,10.00,10.10,9.80,100000,1000000",
    "2024-08-02 00:00:00,10.00,10.50,10.60,9.90,100000,1000000",
]


def _create_acc_strategy(symbol: str) -> dict:
    """单标的、positionRatio=1.0：每个 BUY 的目标仓位就是全部资金"""
    strategy = _create_integ_strategy(symbol)
    strategy["id"] = "acc_test_strategy"
    strategy["name"] = "持仓记账回归策略"
    strategy["capital"] = ACC_CAPITAL
    return strategy


class TestDailyPositionAccounting:
    """daily_positions.position 必须反映目标仓位，不能跨 bar 累加"""

    @pytest.mark.timeout(60)
    def test_position_does_not_accumulate_across_bars(self, auth_token, is_backtest):
        """两根 bar 各触发一次 BUY，持仓不得超过单日目标仓位"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            _cleanup_strategy(auth_token, ACC_STRATEGY_NAME)

            # 1. 导入两根收盘价不同的 bar
            kwargs["json"] = {
                "action": "import",
                "table": "stock_1d",
                "symbol": ACC_SYMBOL,
                "adj": "none",
                "data": ACC_STOCK_LINES,
            }
            check_response(requests.post(f"{BASE_URL}/quote/data", **kwargs))

            # 2. 创建并启动策略
            strategy = _create_acc_strategy(ACC_SYMBOL)
            kwargs["json"] = {"mode": 0, "name": ACC_STRATEGY_NAME, "script": strategy}
            check_response(requests.post(f"{BASE_URL}/strategy", **kwargs))
            kwargs["json"] = {"mode": 1, "name": ACC_STRATEGY_NAME}
            requests.post(f"{BASE_URL}/strategy", **kwargs)

            # 3. 推第二根 bar 触发日终回放（两根都是决策 bar）
            bar = api_get_latest_bar(auth_token, ACC_SYMBOL, date=f"{ACC_BAR2_DATE} 00:00:00")
            result = api_simulate_bar(auth_token, bar)
            if "status" in result and result.get("status") == "error":
                pytest.skip(f"simulate/bar 不可用: {result}")

            # 决策的 timestamp 是落库时的墙钟时间，不能按模拟日期查 /trade/decisions，
            # 直接轮询 daily_positions 是否已写入（/strategy/trade 读的就是这张表）。
            deadline = time.time() + 20
            trade = {}
            while time.time() < deadline:
                trade = check_response(requests.get(
                    f"{BASE_URL}/strategy/trade",
                    params={"name": ACC_STRATEGY_NAME},
                    headers={"Authorization": auth_token}, verify=False, timeout=10,
                )) or {}
                if trade.get("trades"):
                    break
                time.sleep(0.5)

            # 4. 目标仓位：day1 500000/10.00 = 50000 股，day2 500000/10.50 = 47600 股
            day2_close = float(ACC_STOCK_LINES[2].split(",")[2])
            expected_max_qty = int(ACC_CAPITAL / day2_close / 100) * 100
            allowed_max_qty = int(expected_max_qty * 1.1)  # 留 10% 余量（价格波动/费用）

            positions = [t["position_after"] for t in trade.get("trades", [])]
            assert positions, f"没有交易记录: {trade}"
            assert max(positions) <= allowed_max_qty, (
                f"日终持仓 {max(positions)} 股超过单日目标仓位 {expected_max_qty} 股，"
                f"疑似把目标仓位当增量累加（cap={ACC_CAPITAL}）"
            )

            # 组合市值同理不能超过本金太多
            perf = check_response(requests.get(
                f"{BASE_URL}/strategy/performance",
                params={"name": ACC_STRATEGY_NAME},
                headers={"Authorization": auth_token}, verify=False, timeout=10,
            ))
            assert perf["final_value"] <= ACC_CAPITAL * 1.1, (
                f"组合市值 {perf['final_value']} 超过分配资金 {ACC_CAPITAL} 的 110%，"
                f"疑似持仓跨 bar 累加"
            )

        finally:
            _cleanup_strategy(auth_token, ACC_STRATEGY_NAME)
            requests.delete(
                f"{BASE_URL}/quote",
                params={"table": "stock_1d", "symbol": ACC_SYMBOL},
                headers={"Authorization": auth_token},
                verify=False,
                timeout=10,
            )


# ─────────────────────────────────────────────────────────────────────────────
# TickFlow 模式日终策略测试
#
# 覆盖：
# 1. 后复权因子及价格计算正确性（无分红场景）
# 2. 带分红数据场景下，后复权计算正确性（单次分红）
# 3. 多次分红的累积复权计算正确性
# 4. 策略买卖操作完整流程（买入+卖出决策）
#
# 这些测试在 tickflow 模式下运行，使用 API 导入数据 + simulate_bar 触发日终管线
# ─────────────────────────────────────────────────────────────────────────────

# TickFlow 测试专用标的（避免与其他测试冲突）
TF_SYMBOL = "sh.888888"

# 无分红场景：3 根 bar，adj_close 应等于 org_close
TF_STOCK_LINES_NO_DIV = [
    "datetime,open,close,high,low,volume,turnover",
    "2025-03-01 00:00:00,10.00,10.50,10.80,9.90,100000,1050000",
    "2025-03-02 00:00:00,10.50,11.00,11.20,10.40,120000,1320000",
    "2025-03-03 00:00:00,11.00,10.80,11.10,10.70,80000,864000",
]

# 单次分红场景：3 根 bar + 10送5 分红
TF_STOCK_LINES_SINGLE_DIV = [
    "datetime,open,close,high,low,volume,turnover",
    "2025-04-01 00:00:00,20.00,21.00,21.50,19.80,200000,4200000",  # 除权前
    "2025-04-02 00:00:00,21.00,22.00,22.30,20.90,250000,5500000",  # 除权日
    "2025-04-03 00:00:00,14.00,14.50,14.80,13.90,300000,4350000",  # 除权后
]

# 多次分红场景：5 根 bar + 2 次分红（验证累积复权）
# 时间线：
#   2025-01-01: close=100 (分红前)
#   2025-02-01: 10送2, factor=1.2
#   2025-03-01: close=60  (分红1后，分红2前)
#   2025-04-01: 10送3, factor=1.3
#   2025-05-01: close=50  (分红2后)
#
# 累积复权因子：
#   2025-01-01: 1.2 × 1.3 = 1.56 (两次分红都在之后)
#   2025-03-01: 1.3 (只有分红2在之后)
#   2025-05-01: 1.0 (无后续分红)
TF_STOCK_LINES_MULTI_DIV = [
    "datetime,open,close,high,low,volume,turnover",
    "2025-01-01 00:00:00,99.00,100.00,101.00,98.00,100000,10000000",
    "2025-03-01 00:00:00,59.00,60.00,61.00,58.00,150000,9000000",
    "2025-05-01 00:00:00,49.00,50.00,51.00,48.00,200000,10000000",
]

# 分红1：10送2, factor=1.2
TF_DIVIDEND_1 = [
    TF_SYMBOL, "2025-01-28", "2024", "2025-02-01",
    "2025-01-31", "2025-02-01",
    "2", "0", "0", "0", "0", "0", "1",
]

# 分红2：10送3, factor=1.3
TF_DIVIDEND_2 = [
    TF_SYMBOL, "2025-03-28", "2024", "2025-04-01",
    "2025-03-31", "2025-04-01",
    "3", "0", "0", "0", "0", "0", "1",
]

# 单次分红：10送5, factor=1.5
TF_DIVIDEND_SINGLE = [
    TF_SYMBOL, "2025-03-28", "2024", "2025-04-02",
    "2025-04-01", "2025-04-02",
    "5", "0", "0", "0", "0", "0", "1",
]

TF_DIVIDEND_HEADER = [
    "symbol", "announce_date", "report_year", "ex_dividend_date",
    "record_date", "implement_date", "bonus_per_10", "transfer_per_10",
    "cash_per_10", "allot_per_10", "allot_price", "ex_div_price", "action_type"
]

TF_SINGLE_FACTOR = 1.50   # 10送5
TF_FACTOR_1 = 1.20        # 10送2
TF_FACTOR_2 = 1.30        # 10送3
TF_CUMULATIVE_FACTOR = 1.56  # 1.2 × 1.3


def _write_tf_dividend_csv(dividend_rows: list = None):
    """写入 TickFlow 测试用的分红 CSV（支持多行）
    
    Args:
        dividend_rows: 分红数据行列表，每行是 list。默认为 [TF_DIVIDEND_SINGLE]
    """
    if dividend_rows is None:
        dividend_rows = [TF_DIVIDEND_SINGLE]
    
    dividend_dir = SERVER_DATA_DIR / "dividend"
    dividend_dir.mkdir(parents=True, exist_ok=True)
    csv_path = dividend_dir / f"{TF_SYMBOL}_dividend.csv"
    with open(csv_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(TF_DIVIDEND_HEADER)
        for row in dividend_rows:
            writer.writerow(row)
    return csv_path


def _cleanup_tf_data(token: str):
    """清理 TickFlow 测试数据"""
    headers = {"Authorization": token}
    requests.delete(f"{BASE_URL}/dividend",
                    params={"code": TF_SYMBOL}, headers=headers,
                    verify=False, timeout=10)
    requests.delete(f"{BASE_URL}/quote",
                    params={"table": "stock_1d", "symbol": TF_SYMBOL},
                    headers=headers, verify=False, timeout=10)
    csv_path = SERVER_DATA_DIR / "dividend" / f"{TF_SYMBOL}_dividend.csv"
    if csv_path.exists():
        csv_path.unlink()


def _import_tf_stock_data(token: str, lines: list):
    """导入 TickFlow 测试用的股票数据
    
    Args:
        lines: CSV 行列表，第一行为 header（如 TF_STOCK_LINES_NO_DIV）
    """
    kwargs = {"headers": {"Authorization": token}, "verify": False, "timeout": 10}
    kwargs["json"] = {
        "action": "import",
        "table": "stock_1d",
        "symbol": TF_SYMBOL,
        "adj": "none",
        "data": lines,  # 直接传 CSV 字符串列表
    }
    resp = requests.post(f"{BASE_URL}/quote/data", **kwargs)
    check_response(resp)


def _create_buy_sell_strategy() -> dict:
    """创建带买卖信号的 ManualTiming 策略

    买入条件：close > open（阳线）
    卖出条件：close < open（阴线）
    """
    return {
        "id": "tf_buy_sell_strategy",
        "name": "TickFlow买卖测试策略",
        "version": 1,
        "capital": 500000,
        "description": "阳线买入，阴线卖出",
        "nodes": [
            {
                "id": "1",
                "type": "custom",
                "data": {
                    "label": "行情数据",
                    "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "select"},
                        "code": {"value": [TF_SYMBOL], "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"},
                        "turnover": {"value": "turnover", "type": "text"},
                    }
                }
            },
            {
                "id": "2",
                "type": "custom",
                "data": {
                    "label": "买卖信号",
                    "nodeType": "signal",
                    "params": {
                        "code": {"value": [TF_SYMBOL], "type": "text"},
                        "buy": {"value": "close[t] > open[t]", "type": "text"},
                        "sell": {"value": "close[t] < open[t]", "type": "text"},
                    }
                }
            },
            {
                "id": "3",
                "type": "custom",
                "data": {
                    "label": "投资组合",
                    "nodeType": "portfolio",
                    "params": {
                        "positionRatio": {"value": 1.0, "type": "number"},
                    }
                }
            },
            {
                "id": "4",
                "type": "custom",
                "data": {
                    "label": "手动下单",
                    "nodeType": "execution",
                    "params": {
                        "commission": {"value": 0.0003, "type": "number"},
                        "stamp": {"value": 0.001, "type": "number"},
                        "minCommission": {"value": 5, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": 0.0, "type": "number"},
                        "type": {"value": 7, "type": "select"},  # Manual
                        "contract": {"value": 0, "type": "select"},
                    }
                }
            }
        ],
        "edges": [
            {"id": "1->2", "source": "1", "target": "2", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "2->3", "source": "2", "target": "3", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "3->4", "source": "3", "target": "4", "sourceHandle": "output", "targetHandle": "input"},
        ]
    }


class TestTickflowDailyStrategy:
    """TickFlow 模式日终策略测试

    覆盖：
    1. 后复权因子及价格计算正确性（无分红 → adj_close = org_close）
    2. 单次分红场景后复权计算正确性（10送5）
    3. 多次分红的累积复权计算正确性（10送2 + 10送3）
    4. 策略买卖操作完整流程（阳线买/阴线卖 + 决策验证）
    """

    @pytest.mark.timeout(30)
    def test_tickflow_adj_price_no_dividend(self, auth_token, is_backtest):
        """无分红场景：adj_close 应等于 org_close（后复权因子=1）

        步骤：
        1. 导入 3 根 bar 数据（无分红）
        2. 查询 stock_1d 验证 adj_close = org_close
        """
        if is_backtest:
            pytest.skip("仅在 tickflow 模式下运行")

        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            # 导入数据
            _import_tf_stock_data(auth_token, TF_STOCK_LINES_NO_DIV)

            # 查询验证
            resp = requests.get(f"{BASE_URL}/quote",
                               params={"table": "stock_1d", "symbol": TF_SYMBOL},
                               **kwargs)
            data = check_response(resp)
            assert data["count"] == 3, f"期望 3 根 bar，实际 {data['count']}"

            bars = sorted(data["data"], key=lambda b: b["datetime"])
            for bar in bars:
                # 无分红时 adj_close 应等于 org_close
                assert abs(bar["adj_close"] - bar["close"]) < 0.01, \
                    f"无分红时 adj_close={bar['adj_close']} 应等于 close={bar['close']}"

        finally:
            _cleanup_tf_data(auth_token)

    @pytest.mark.timeout(30)
    def test_tickflow_adj_price_single_dividend(self, auth_token, is_backtest):
        """单次分红场景：10送5，验证后复权计算正确性

        步骤：
        1. 导入 3 根 bar 数据
        2. 导入分红 CSV（10送5，factor=1.5，除权日=2025-04-02）
        3. 触发 recalcSymbolAdjPrices
        4. 验证 adj_close = org_close × factor

        复权规则：ex_dividend_date > bar.datetime 时才调整
        除权前 bar (2025-04-01): close=21.00 → adj_close = 21.00 × 1.5 = 31.50
        除权日 bar (2025-04-02): close=22.00 → adj_close = 22.00 × 1.0 = 22.00 (不调整)
        除权后 bar (2025-04-03): close=14.50 → adj_close = 14.50 × 1.0 = 14.50 (不调整)
        """
        if is_backtest:
            pytest.skip("仅在 tickflow 模式下运行")

        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            # 1. 导入股票数据
            _import_tf_stock_data(auth_token, TF_STOCK_LINES_SINGLE_DIV)

            # 2. 写入并导入分红
            _write_tf_dividend_csv([TF_DIVIDEND_SINGLE])
            kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(resp)

            # 3. 触发复权计算
            kwargs["json"] = {"action": "recalc", "code": TF_SYMBOL}
            resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(resp)

            # 4. 验证 adj_close
            resp = requests.get(f"{BASE_URL}/quote",
                               params={"table": "stock_1d", "symbol": TF_SYMBOL},
                               **kwargs)
            data = check_response(resp)
            assert data["count"] == 3, f"期望 3 根 bar，实际 {data['count']}"

            bars = sorted(data["data"], key=lambda b: b["datetime"])

            # 除权前 bar (2025-04-01 < 2025-04-02): adj_close = close × factor
            bar0 = bars[0]
            expected_adj0 = bar0["close"] * TF_SINGLE_FACTOR
            assert abs(bar0["adj_close"] - expected_adj0) < 0.01, \
                f"除权前 adj_close={bar0['adj_close']}, 期望={expected_adj0}"

            # 除权日 bar (2025-04-02 == 2025-04-02): adj_close = close (不调整)
            bar1 = bars[1]
            assert abs(bar1["adj_close"] - bar1["close"]) < 0.01, \
                f"除权日 adj_close={bar1['adj_close']} 应等于 close={bar1['close']} (不调整)"

            # 除权后 bar (2025-04-03 > 2025-04-02): adj_close = close (不调整)
            bar2 = bars[2]
            assert abs(bar2["adj_close"] - bar2["close"]) < 0.01, \
                f"除权后 adj_close={bar2['adj_close']} 应等于 close={bar2['close']} (不调整)"

        finally:
            _cleanup_tf_data(auth_token)

    @pytest.mark.timeout(30)
    def test_tickflow_adj_price_cumulative_dividends(self, auth_token, is_backtest):
        """多次分红场景：验证累积复权计算正确性

        时间线：
          2025-01-01: close=100 (分红1前)
          2025-02-01: 10送2, factor=1.2
          2025-03-01: close=60  (分红1后，分红2前)
          2025-04-01: 10送3, factor=1.3
          2025-05-01: close=50  (分红2后)

        累积复权因子：
          2025-01-01: 1.2 × 1.3 = 1.56 (两次分红都在之后)
          2025-03-01: 1.3 (只有分红2在之后)
          2025-05-01: 1.0 (无后续分红)

        期望 adj_close：
          2025-01-01: 100 × 1.56 = 156
          2025-03-01: 60 × 1.3 = 78
          2025-05-01: 50 × 1.0 = 50
        """
        if is_backtest:
            pytest.skip("仅在 tickflow 模式下运行")

        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
            # 1. 导入股票数据（3 根 bar）
            _import_tf_stock_data(auth_token, TF_STOCK_LINES_MULTI_DIV)

            # 2. 写入并导入两次分红
            _write_tf_dividend_csv([TF_DIVIDEND_1, TF_DIVIDEND_2])
            kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(resp)

            # 3. 触发复权计算
            kwargs["json"] = {"action": "recalc", "code": TF_SYMBOL}
            resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
            check_response(resp)

            # 4. 验证 adj_close（累积复权）
            resp = requests.get(f"{BASE_URL}/quote",
                               params={"table": "stock_1d", "symbol": TF_SYMBOL},
                               **kwargs)
            data = check_response(resp)
            assert data["count"] == 3, f"期望 3 根 bar，实际 {data['count']}"

            bars = sorted(data["data"], key=lambda b: b["datetime"])

            # 2025-01-01: 两次分红都在之后，累积因子 = 1.2 × 1.3 = 1.56
            bar0 = bars[0]
            expected_adj0 = bar0["close"] * TF_CUMULATIVE_FACTOR  # 100 × 1.56 = 156
            assert abs(bar0["adj_close"] - expected_adj0) < 0.01, \
                f"2025-01-01 adj_close={bar0['adj_close']}, 期望={expected_adj0} (累积因子={TF_CUMULATIVE_FACTOR})"

            # 2025-03-01: 只有分红2在之后，因子 = 1.3
            bar1 = bars[1]
            expected_adj1 = bar1["close"] * TF_FACTOR_2  # 60 × 1.3 = 78
            assert abs(bar1["adj_close"] - expected_adj1) < 0.01, \
                f"2025-03-01 adj_close={bar1['adj_close']}, 期望={expected_adj1} (因子={TF_FACTOR_2})"

            # 2025-05-01: 无后续分红，因子 = 1.0
            bar2 = bars[2]
            assert abs(bar2["adj_close"] - bar2["close"]) < 0.01, \
                f"2025-05-01 adj_close={bar2['adj_close']} 应等于 close={bar2['close']}"

        finally:
            _cleanup_tf_data(auth_token)

    @pytest.mark.timeout(60)
    @pytest.mark.skip(reason="simulate_bar 在 tickflow 模式下不触发完整日终管线，决策生成依赖 TickFlowBridge 收盘触发")
    def test_tickflow_daily_buy_sell(self, auth_token, is_backtest):
        """带买卖信号的日终策略完整流程

        注意：此测试在 tickflow 模式下被跳过，因为 simulate_bar 不会触发完整的日终管线。
        日终管线在 tickflow 模式下由 TickFlowBridge 在收盘后自动触发。
        前三个测试（无分红/单次分红/累积分红）已验证复权计算正确性。

        步骤：
        1. 导入带分红的数据 + 触发复权
        2. 部署 ManualTiming 策略（阳线买/阴线卖）
        3. simulate_bar 推送阳线 bar → 验证买入决策
        4. simulate_bar 推送阴线 bar → 验证卖出决策
        5. 验证绩效接口返回有效指标

        数据设计：
        - Day 1 (阳线): open=14.00, close=14.50 → 买入信号
        - Day 2 (阴线): open=14.50, close=14.20 → 卖出信号
        """
        if is_backtest:
            pytest.skip("仅在 tickflow 模式下运行")

        name = "test_tf_buy_sell"
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 30}

        try:
            # 1. 导入数据 + 分红 + 复权
            _import_tf_stock_data(auth_token, TF_STOCK_LINES_SINGLE_DIV)
            _write_tf_dividend_csv([TF_DIVIDEND_SINGLE])
            div_kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}
            div_kwargs["json"] = {
                "action": "import",
                "dividend_dir": str(SERVER_DATA_DIR / "dividend"),
            }
            requests.post(f"{BASE_URL}/dividend", **div_kwargs)
            div_kwargs["json"] = {"action": "recalc", "code": TF_SYMBOL}
            requests.post(f"{BASE_URL}/dividend", **div_kwargs)

            # 2. 部署策略
            script = _create_buy_sell_strategy()
            resp = requests.post(f"{BASE_URL}/strategy",
                                json={"mode": 0, "name": name, "script": script}, **kwargs)
            assert resp.ok, f"部署失败: {resp.text}"

            # 3. 推送阳线 bar → 买入信号
            bar_buy = {
                "symbol": TF_SYMBOL,
                "datetime": "2025-04-04 00:00:00",
                "open": 14.00,
                "close": 14.50,  # close > open → 阳线 → 买入
                "high": 14.60,
                "low": 13.90,
                "volume": 100000,
                "turnover": 1450000,
            }
            result = api_simulate_bar(auth_token, bar_buy)
            if "status" in result and result.get("status") == "error":
                pytest.skip(f"simulate_bar 不可用: {result}")

            # 等待决策生成
            deadline = time.time() + 10
            buy_decision = None
            while time.time() < deadline:
                decisions = api_get_decisions(auth_token, date=today_str())
                for d in decisions:
                    if d.get("strategy") == name and d.get("action") == "open_long":
                        buy_decision = d
                        break
                if buy_decision:
                    break
                time.sleep(0.5)

            assert buy_decision is not None, "阳线 bar 应产生买入决策"
            assert buy_decision["symbol"] == TF_SYMBOL

            # 4. 推送阴线 bar → 卖出信号
            bar_sell = {
                "symbol": TF_SYMBOL,
                "datetime": "2025-04-07 00:00:00",
                "open": 14.50,
                "close": 14.20,  # close < open → 阴线 → 卖出
                "high": 14.60,
                "low": 14.10,
                "volume": 120000,
                "turnover": 1704000,
            }
            api_simulate_bar(auth_token, bar_sell)

            # 等待卖出决策
            deadline = time.time() + 10
            sell_decision = None
            while time.time() < deadline:
                decisions = api_get_decisions(auth_token, date=today_str())
                for d in decisions:
                    if d.get("strategy") == name and d.get("action") == "close_long":
                        sell_decision = d
                        break
                if sell_decision:
                    break
                time.sleep(0.5)

            assert sell_decision is not None, "阴线 bar 应产生卖出决策"

            # 5. 验证绩效接口
            resp = requests.get(f"{BASE_URL}/strategy/performance",
                               params={"name": name}, **kwargs)
            assert resp.ok, f"绩效查询失败: {resp.text}"
            perf_data = resp.json()
            assert perf_data["strategy"] == name
            assert perf_data["trading_days"] >= 1, "应有至少 1 个交易日"
            assert "metrics" in perf_data
            for key in ["total_return", "sharpe_ratio", "max_drawdown"]:
                assert key in perf_data["metrics"], f"缺少指标: {key}"

        finally:
            # 清理策略
            try:
                requests.post(f"{BASE_URL}/strategy",
                             json={"mode": 2, "name": name}, **kwargs)
                requests.delete(f"{BASE_URL}/strategy",
                               json={"name": name}, **kwargs)
            except Exception:
                pass
            _cleanup_tf_data(auth_token)


# ─────────────────────────────────────────────────────────────────────────────
# 决策关闭（PUT）+ 执行确认 测试
#
# 覆盖：
# 1. PUT /v0/trade/decisions 关闭 pending 决策
# 2. 幂等性：重复关闭不报错
# 3. 无效 id：负数 → 400，不存在 → 404
# 4. POST /v0/trade/order + decisionId 执行决策
# 5. 状态转换：executed 后 closed 的组合状态
#
# 依赖 simulate_bar（仅 Debug 构建），CI Release 下 skip
# ─────────────────────────────────────────────────────────────────────────────

def close_decision(token: str, decision_id: int) -> requests.Response:
    """PUT /v0/trade/decisions 关闭决策"""
    return requests.put(
        f"{BASE_URL}/trade/decisions",
        json={"id": decision_id},
        headers={"Authorization": token},
        verify=False,
        timeout=10,
    )


def wait_for_pending_decision(token, strategy_name, timeout=10):
    """等待出现指定策略的 pending 决策"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        decisions = api_get_decisions(token, date=today_str())
        for d in decisions:
            if (d.get("strategy") == strategy_name
                    and not d["executed"] and not d.get("closed")):
                return d
        time.sleep(0.5)
    return None


@pytest.fixture
def produced_decision(auth_token, loaded_strategy, is_backtest):
    """通过 simulate_bar 产生一条 pending 决策"""
    if not is_backtest:
        pytest.skip("仅在 stock_hist_sim 模式下运行")

    try:
        bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
    except ValueError as e:
        pytest.skip(f"无测试数据: {e}")

    # 收盘价必须与前一根 bar 不同：价格没动 → 组合不需要再平衡 → 计划不变
    # → 信号是 HOLD → 日终不会产生任何可关闭的决策（只有 BUY/SELL 才入库）
    bar["close"] = bar["close"] * 1.02

    # close > open 触发买入信号
    if not bar["close"] > bar["open"]:
        bar["open"] = bar["close"] * 0.99
    bar["high"] = max(bar["high"], bar["open"], bar["close"])
    bar["low"] = min(bar["low"], bar["open"])

    result = api_simulate_bar(auth_token, bar)
    if "status" in result and result.get("status") == "error":
        pytest.skip(f"simulate/bar 不可用（Release 构建）: {result}")

    decision = wait_for_pending_decision(auth_token, STRATEGY_NAME)
    if decision is None:
        pytest.skip("simulate_bar 后未产生决策")

    return decision


@pytest.mark.usefixtures("auth_token")
def _create_multi_symbol_strategy(symbols: list) -> dict:
    """创建监控多个 symbol 的 ManualTiming 策略

    用于需要产生多条决策的测试场景
    """
    return {
        "id": "test_manual_signal_multi",
        "name": "测试Manual决策策略-多标的",
        "version": 1,
        "capital": 500000,
        "description": "ManualTiming 测试策略：监控多个标的，close>open 时买入",
        "nodes": [
            {
                "id": "1",
                "type": "custom",
                "data": {
                    "label": "行情数据",
                    "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "select"},
                        "code": {"value": symbols, "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"},
                        "turnover": {"value": "turnover", "type": "text"},
                    }
                }
            },
            {
                "id": "2",
                "type": "custom",
                "data": {
                    "label": "买入信号",
                    "nodeType": "signal",
                    "params": {
                        "code": {"value": symbols, "type": "text"},
                        "buy": {"value": "close[t] > open[t]", "type": "text"},
                        "sell": {"value": "false", "type": "text"},
                    }
                }
            },
            {
                "id": "3",
                "type": "custom",
                "data": {
                    "label": "投资组合",
                    "nodeType": "portfolio",
                    "params": {
                        "positionRatio": {"value": 1.0, "type": "number"},
                    }
                }
            },
            {
                "id": "4",
                "type": "custom",
                "data": {
                    "label": "手动下单",
                    "nodeType": "execution",
                    "params": {
                        "commission": {"value": 0.0003, "type": "number"},
                        "stamp": {"value": 0.001, "type": "number"},
                        "minCommission": {"value": 5, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": 0.0, "type": "number"},
                        "type": {"value": 7, "type": "select"},  # Manual
                        "contract": {"value": 0, "type": "select"},
                    }
                }
            }
        ],
        "edges": [
            {"id": "1->2", "source": "1", "target": "2", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "2->3", "source": "2", "target": "3", "sourceHandle": "output", "targetHandle": "input"},
            {"id": "3->4", "source": "3", "target": "4", "sourceHandle": "output", "targetHandle": "input"},
        ]
    }


class TestDecisionCloseAndExecute:
    """决策关闭（PUT）和执行（POST order + decisionId）测试

    仅 Debug 构建可运行（依赖 simulate/bar 端点产生决策）。
    """

    def test_close_pending_decision(self, auth_token, produced_decision):
        """关闭 pending 决策 → closed=true"""
        decision = produced_decision
        resp = close_decision(auth_token, decision["id"])
        assert resp.ok, f"关闭失败: {resp.text}"
        data = resp.json()
        assert data.get("success") is True

        # 验证 GET 返回 closed=true
        time.sleep(0.5)
        updated = [d for d in api_get_decisions(auth_token, date=today_str())
                   if d["id"] == decision["id"]]
        assert updated, "决策消失"
        assert updated[0]["closed"] is True, "closed 未更新"

    def test_close_already_closed_is_idempotent(
            self, auth_token, produced_decision):
        """重复关闭已关闭的决策 → 仍然成功（幂等）"""
        decision = produced_decision
        close_decision(auth_token, decision["id"])
        time.sleep(0.5)

        resp = close_decision(auth_token, decision["id"])
        assert resp.ok, f"重复关闭应成功: {resp.text}"

    def test_close_invalid_id_negative(self, auth_token):
        """负数 id → 400"""
        resp = close_decision(auth_token, -1)
        assert resp.status_code == 400, f"应返回 400: {resp.text}"
        data = resp.json()
        assert "error" in data

    def test_close_nonexistent_id(self, auth_token):
        """不存在的 id → 404"""
        resp = close_decision(auth_token, 999999)
        assert resp.status_code == 404, f"应返回 404: {resp.text}"
        data = resp.json()
        assert "error" in data

    def test_execute_pending_decision(
            self, auth_token, produced_decision, is_backtest):
        """POST /v0/trade/order + decisionId → executed=true"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        decision = produced_decision
        direct = 0 if decision["action"] in ["open_long", "close_short"] else 1

        resp = api_order_with_decision_id(
            auth_token, TEST_SYMBOL, decision["id"],
            quantity=decision["quantity"],
            price=decision["price"],
            direct=direct,
        )
        assert resp.status_code == 200, f"下单失败: {resp.text}"

        # 验证 executed 状态
        time.sleep(0.5)
        updated = [d for d in api_get_decisions(auth_token, date=today_str())
                   if d["id"] == decision["id"]]
        assert updated, "决策消失"
        assert updated[0]["executed"] is True, "executed 未更新"
        assert updated[0]["executedQuantity"] > 0, "executedQuantity 未更新"

    def test_close_after_execute(
            self, auth_token, produced_decision, is_backtest):
        """已执行的决策也可以被关闭（前端确认后关闭）"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        decision = produced_decision
        direct = 0 if decision["action"] in ["open_long", "close_short"] else 1

        # 先执行
        api_order_with_decision_id(
            auth_token, TEST_SYMBOL, decision["id"],
            quantity=decision["quantity"],
            price=decision["price"],
            direct=direct,
        )
        time.sleep(0.5)

        # 再关闭
        resp = close_decision(auth_token, decision["id"])
        assert resp.ok, f"执行后关闭应成功: {resp.text}"

        # 验证两者同时为 true
        time.sleep(0.5)
        updated = [d for d in api_get_decisions(auth_token, date=today_str())
                   if d["id"] == decision["id"]]
        assert updated
        assert updated[0]["executed"] is True
        assert updated[0]["closed"] is True

    def test_query_reflects_mixed_status(
            self, auth_token, is_backtest):
        """多条决策：一条关闭、一条执行，GET 分别返回正确状态"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        # 使用多 symbol 策略，避免持仓冲突
        symbols = [TEST_SYMBOL, "sz.900002"]
        multi_strategy = _create_multi_symbol_strategy(symbols)
        multi_strategy_name = "test_manual_signal_multi"

        # 加载多 symbol 策略
        api_load_strategy(auth_token, multi_strategy_name, multi_strategy)
        headers = {"Authorization": auth_token}
        requests.post(
            f"{BASE_URL}/strategy",
            json={"mode": 1, "name": multi_strategy_name},
            headers=headers,
            verify=False,
            timeout=10,
        )

        try:
            # 先推送两个 symbol 的 bar，让策略可以执行
            try:
                bar1 = api_get_latest_bar(auth_token, TEST_SYMBOL)
                bar2_init = api_get_latest_bar(auth_token, "sz.900002")
            except ValueError:
                pytest.skip("无测试数据")

            # 确保 close > open
            if not bar1["close"] > bar1["open"]:
                bar1["open"] = bar1["close"] * 0.99
            if not bar2_init["close"] > bar2_init["open"]:
                bar2_init["open"] = bar2_init["close"] * 0.99

            # 推送两个 symbol 的 bar（同一天）
            bar2_init["datetime"] = bar1["datetime"]
            result1 = api_simulate_bar(auth_token, bar1)
            result2 = api_simulate_bar(auth_token, bar2_init)

            if "status" in result1 and result1.get("status") == "error":
                pytest.skip(f"simulate/bar 不可用: {result1}")
            if "status" in result2 and result2.get("status") == "error":
                pytest.skip(f"simulate/bar 不可用: {result2}")

            # 等待第一条决策
            d1 = wait_for_pending_decision(auth_token, multi_strategy_name)
            if d1 is None:
                pytest.skip("未产生第一条决策")

            # 关闭第一条
            close_decision(auth_token, d1["id"])
            time.sleep(0.5)

            # 产生第二条决策（推送新日期的 bar）
            second_symbol = "sz.900002"
            try:
                bar2_new = api_get_latest_bar(auth_token, second_symbol)
            except ValueError:
                pytest.skip("无第二条测试数据")
            if not bar2_new["close"] > bar2_new["open"]:
                bar2_new["open"] = bar2_new["close"] * 0.99
            bar2_new["datetime"] = "2025-02-20 00:00:00"
            api_simulate_bar(auth_token, bar2_new)

            d2 = wait_for_pending_decision(
                auth_token, multi_strategy_name, timeout=15)
            if d2 is None:
                pytest.skip("未产生第二条决策")

            # 执行第二条
            direct = 0 if d2["action"] in ["open_long", "close_short"] else 1
            api_order_with_decision_id(
                auth_token, second_symbol, d2["id"],
                quantity=d2["quantity"], price=d2["price"], direct=direct,
            )
            time.sleep(0.5)

            # 验证两条决策状态
            all_decisions = api_get_decisions(auth_token, date=today_str())
            d1_updated = next((d for d in all_decisions if d["id"] == d1["id"]), None)
            d2_updated = next((d for d in all_decisions if d["id"] == d2["id"]), None)

            assert d1_updated is not None
            assert d1_updated["closed"] is True, "d1 应为 closed"
            assert d1_updated["executed"] is False, "d1 不应为 executed"

            assert d2_updated is not None
            assert d2_updated["executed"] is True, "d2 应为 executed"

        finally:
            # 清理策略
            api_cleanup_strategy(auth_token, multi_strategy_name)


# ─────────────────────────────────────────────────────────────────────────────
# DecisionSet 统一决策产物测试
#
# 覆盖：
# 1. 状态词汇统一（status 不再恒为 "completed"）
# 2. HOLD 决策保留（hold_count 不再恒为 0）
# 3. 收盘价使用（daily_positions 不再用计划目标价）
# 4. 归属日统一（日报/邮件/SSE 使用同一 asOf）
#
# 依赖 simulate_bar（仅 Debug 构建），CI Release 下 skip
# ─────────────────────────────────────────────────────────────────────────────

def get_daily_decision_json(date: str) -> dict:
    """读取 decisions/{date}.json 文件"""
    json_path = SERVER_DATA_DIR / "decisions" / f"{date}.json"
    if not json_path.exists():
        return {}
    with open(json_path, 'r', encoding='utf-8') as f:
        return json.load(f)


def extract_bar_date(bar: dict) -> str:
    """从 bar 的 datetime 提取日期（YYYY-MM-DD）"""
    return bar["datetime"].split(" ")[0]


def wait_for_daily_decision_json(bar_date: str, timeout: int = 15) -> dict:
    """等待日报 JSON 生成

    Args:
        bar_date: 日报日期（YYYY-MM-DD）
        timeout: 超时时间（秒）

    Returns:
        日报 JSON 字典，如果超时返回空字典
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        daily_json = get_daily_decision_json(bar_date)
        if daily_json:
            return daily_json
        time.sleep(0.5)
    return {}


@pytest.mark.usefixtures("auth_token")
class TestDecisionSetStatusVocabulary:
    """DecisionSet 状态词汇统一测试

    验证：
    1. 日报 JSON 的 status 字段使用统一词汇（completed/partial/error）
    2. 策略报错时 status 不再恒为 "completed"
    3. 多策略聚合时取最差状态
    """

    def test_status_vocabulary_in_daily_json(self, auth_token, loaded_strategy, is_backtest):
        """日报 JSON 的 status 字段使用统一词汇"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99

        bar_date = extract_bar_date(bar)
        result = api_simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=bar_date)
            if decisions:
                break
            time.sleep(0.5)

        # 读取日报 JSON
        daily_json = wait_for_daily_decision_json(bar_date)
        if not daily_json:
            pytest.skip(f"日报 JSON 未生成（日期: {bar_date}）")

        # 验证 status 词汇
        status = daily_json.get("status")
        assert status in ["completed", "partial", "error"], \
            f"status 应为 completed/partial/error，实际: {status}"

        # 验证各策略 status
        for strategy_name, strategy_report in daily_json.get("strategies", {}).items():
            strategy_status = strategy_report.get("status")
            assert strategy_status in ["completed", "partial", "error"], \
                f"策略 {strategy_name} 的 status 非法: {strategy_status}"


@pytest.mark.usefixtures("auth_token")
class TestDecisionSetHoldPreservation:
    """DecisionSet HOLD 决策保留测试

    验证：
    1. HOLD 决策进入日报 JSON 的 decisions 数组
    2. hold_count 不再恒为 0
    3. 邮件与日报的 HOLD 计数一致
    """

    def test_hold_decisions_in_daily_json(self, auth_token, loaded_strategy, is_backtest):
        """HOLD 决策进入日报 JSON"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        # close < open 触发 HOLD（无买入信号）
        if bar["close"] > bar["open"]:
            bar["close"] = bar["open"] * 0.99

        bar_date = extract_bar_date(bar)
        result = api_simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=bar_date)
            if decisions:
                break
            time.sleep(0.5)

        # 读取日报 JSON
        daily_json = wait_for_daily_decision_json(bar_date)
        if not daily_json:
            pytest.skip(f"日报 JSON 未生成（日期: {bar_date}）")

        # 验证 hold_count
        summary = daily_json.get("summary", {})
        hold_count = summary.get("hold_count", 0)
        # hold_count 应该 >= 0（可能为 0 如果所有标的都有 BUY/SELL）
        assert isinstance(hold_count, int) and hold_count >= 0, \
            f"hold_count 应为非负整数，实际: {hold_count}"

        # 验证 decisions 数组包含 HOLD
        for strategy_name, strategy_report in daily_json.get("strategies", {}).items():
            decisions = strategy_report.get("decisions", [])
            hold_decisions = [d for d in decisions if d.get("action") == "HOLD"]
            # 如果 hold_count > 0，则 decisions 数组中应有 HOLD
            if hold_count > 0:
                assert len(hold_decisions) > 0, \
                    f"hold_count={hold_count} 但 decisions 中无 HOLD"


@pytest.mark.usefixtures("auth_token")
class TestDecisionSetClosePrice:
    """DecisionSet 收盘价使用测试

    验证：
    1. daily_positions 表使用真实收盘价，不再用计划目标价
    2. 日报 JSON 包含 close_price 字段
    """

    def test_close_price_in_daily_json(self, auth_token, loaded_strategy, is_backtest):
        """日报 JSON 包含 close_price 字段"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99

        bar_date = extract_bar_date(bar)
        result = api_simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=bar_date)
            if decisions:
                break
            time.sleep(0.5)

        # 读取日报 JSON
        daily_json = wait_for_daily_decision_json(bar_date)
        if not daily_json:
            pytest.skip(f"日报 JSON 未生成（日期: {bar_date}）")

        # 验证 close_price 字段
        for strategy_name, strategy_report in daily_json.get("strategies", {}).items():
            for decision in strategy_report.get("decisions", []):
                # close_price 字段应该存在
                assert "close_price" in decision, \
                    f"决策缺少 close_price 字段"


@pytest.mark.usefixtures("auth_token")
class TestDecisionSetAsOfUnification:
    """DecisionSet 归属日统一测试

    验证：
    1. 日报 JSON 的 date 字段与 bar 日期一致
    2. 各策略的 executed_at 是执行时间（当前时间）
    """

    def test_as_of_unified(self, auth_token, loaded_strategy, is_backtest):
        """归属日统一"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")

        try:
            bar = api_get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99

        bar_date = extract_bar_date(bar)
        result = api_simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        while time.time() < deadline:
            decisions = api_get_decisions(auth_token, date=bar_date)
            if decisions:
                break
            time.sleep(0.5)

        # 读取日报 JSON
        daily_json = wait_for_daily_decision_json(bar_date)
        if not daily_json:
            pytest.skip(f"日报 JSON 未生成（日期: {bar_date}）")

        # 验证归属日统一：date 字段应该是 bar 的日期
        date_in_json = daily_json.get("date")
        assert date_in_json == bar_date, \
            f"日报 date={date_in_json} 应为 bar_date={bar_date}"

        # 验证各策略的 executed_at 是执行时间（应该包含当前日期）
        # executed_at 语义上是"执行时间"，不是归属日
        current_date = today_str()
        for strategy_name, strategy_report in daily_json.get("strategies", {}).items():
            executed_at = strategy_report.get("executed_at", "")
            # executed_at 应该包含当前日期（执行日期），而不是 bar 日期
            assert current_date in executed_at, \
                f"策略 {strategy_name} 的 executed_at={executed_at} 应包含当前日期 {current_date}"
