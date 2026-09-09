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

from tool import check_response, BASE_URL, _DATA_DIR as SERVER_DATA_DIR

# ==================== 配置 ====================

STRATEGY_PATH = (
    Path(__file__).parent / "metric_test_data" / "manual_signal_strategy.json"
)
STRATEGY_NAME = "manual_signal_strategy"
TEST_SYMBOL = "sz.900001"  # metric 测试专用生成数据（已自动导入 DuckDB）
TEST_QUANTITY = 100

# ==================== 工具函数（从 test_simulate_bar.py 复制） ====================

def get_auth_token() -> str:
    """获取认证 token"""
    resp = requests.post(
        f"{BASE_URL}/user/login",
        json={"name": "admin", "pwd": "admin"},
        verify=False,
    )
    resp.raise_for_status()
    return resp.json()["tk"]


def api_load_strategy(token: str, name: str, script: dict) -> dict:
    """POST /v0/strategy action=load — 加载策略到 StrategySubSystem"""
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "load", "name": name, "script": script},
        headers=headers,
        verify=False,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def to_api_symbol(symbol: str) -> str:
    """sz.900001 → 900001.SZ（/stocks/history 端点格式）"""
    # 内部格式: <exchange>.<code>，如 sz.000001, sh.600000, bj.920108
    if "." not in symbol:
        return symbol
    exchange, code = symbol.split(".", 1)
    # exchange 前缀: sz→SZ, sh→SH, bj→BJ
    exchange_upper = exchange.upper()
    return f"{code}.{exchange_upper}"


def get_latest_bar(token: str, symbol: str, date: str = None) -> dict:
    """从 StockHistoryHandler 获取最新 bar"""
    headers = {"Authorization": token}
    api_symbol = to_api_symbol(symbol)
    try:
        resp = requests.get(
            f"{BASE_URL}/stocks/history",
            params={
                "id": api_symbol,
                "type": "1d",
                "start": "1672531200",  # 2023-01-01 (Unix 秒)
                "end": "1767225599",    # 2025-12-31 (Unix 秒)
            },
            headers=headers,
            verify=False,
            timeout=10,
        )
    except requests.RequestException as e:
        raise ValueError(f"Cannot get bar data for {api_symbol}: {e}")

    if not resp.ok:
        raise ValueError(f"Cannot get bar data for {api_symbol}: HTTP {resp.status_code}")

    try:
        data = resp.json()
    except Exception as e:
        raise ValueError(f"Cannot parse bar data for {api_symbol}: {e}")

    if not isinstance(data, list) or not data:
        raise ValueError(f"Empty bar data for {api_symbol}")

    last = data[-1]
    # /stocks/history 的 datetime 是 Unix 时间戳（数字），需转成 "YYYY-MM-DD HH:MM:SS" 字符串
    raw_dt = last.get("datetime", "")
    try:
        dt_int = int(raw_dt)
        dt_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(dt_int))
    except (ValueError, TypeError):
        dt_str = str(raw_dt)

    return {
        "symbol": symbol,
        "open": float(last.get("open", 0)),
        "high": float(last.get("high", 0)),
        "low": float(last.get("low", 0)),
        "close": float(last.get("close", 0)),
        "volume": int(float(last.get("volume", 0))),
        "datetime": date or dt_str,
    }


def simulate_bar(token: str, bar: dict) -> dict:
    """POST /v0/strategy/simulate/bar（仅 Debug 构建可用）

    失败时返回 {"status": "error", "error": ...}，调用方应据此 pytest.skip。
    Release 构建下路由未注册（HTTP 404），同样走 error 分支。
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy/simulate/bar",
        json=bar,
        headers=headers,
        verify=False,
        timeout=30,
    )
    if resp.ok:
        return resp.json()
    return {
        "status": "error",
        "error": f"HTTP {resp.status_code}: {resp.text[:200]}",
    }


def get_decisions(token: str, date: str = None) -> list:
    """GET /v0/trade/decisions"""
    headers = {"Authorization": token}
    params = {}
    if date:
        params["date"] = date
    resp = requests.get(
        f"{BASE_URL}/trade/decisions",
        params=params,
        headers=headers,
        verify=False,
        timeout=10,
    )
    return check_response(resp) or []


def today_str() -> str:
    return time.strftime("%Y-%m-%d")


def order_with_decision_id(
    token: str, symbol: str, decision_id: int,
    quantity: int, price: float, direct: int = 0,
) -> requests.Response:
    """POST /v0/trade/order 关联 decisionId，返回原始 Response"""
    headers = {"Authorization": token}
    body = {
        "symbol": symbol,
        "direct": direct,
        "type": 1,  # 限价单
        "quantity": quantity,
        "prices": price,
        "kind": 0,
        "timeType": 0,
        "decisionId": decision_id,
    }
    return requests.post(
        f"{BASE_URL}/trade/order",
        json=body,
        headers=headers,
        verify=False,
        timeout=10,
    )


def cleanup_strategy(token: str, name: str):
    """清理测试策略"""
    headers = {"Authorization": token}
    try:
        # 先停止
        requests.post(
            f"{BASE_URL}/strategy",
            json={"mode": 2, "name": name},
            headers=headers,
            verify=False,
            timeout=5,
        )
        # 再删除
        requests.delete(
            f"{BASE_URL}/strategy",
            json={"name": name},
            headers=headers,
            verify=False,
            timeout=5,
        )
    except Exception:
        pass


# ==================== Fixture ====================

@pytest.fixture(scope="module", autouse=True)
def reclaim_capital(auth_token):
    """回收先前测试残留的 CapitalPool 资金，确保日终策略有足够资金分配"""
    headers = {"Authorization": auth_token}
    requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "reclaim_all"},
        headers=headers,
        verify=False,
        timeout=10,
    )


@pytest.fixture
def decision_token(auth_token):
    """返回带 token 的辅助函数包"""
    yield auth_token


@pytest.fixture
def loaded_strategy(decision_token):
    """加载测试策略，测试后自动清理"""
    strategy = json.loads(STRATEGY_PATH.read_text())
    api_load_strategy(decision_token, STRATEGY_NAME, strategy)
    yield strategy
    cleanup_strategy(decision_token, STRATEGY_NAME)


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
        decisions = get_decisions(auth_token, date=today_str())
        assert isinstance(decisions, list)

    def test_decision_schema(self, auth_token):
        """决策字段完整性：返回的决策必须包含所有必需字段"""
        decisions = get_decisions(auth_token, date=today_str())
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
        resp = order_with_decision_id(
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
            bar = get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

        # close > open 才触发买入，找一个这样的 bar
        if not bar["close"] > bar["open"]:
            # 强制构造一个 close > open 的 bar
            bar["open"] = bar["close"] * 0.99
            bar["high"] = max(bar["open"], bar["high"])
            bar["low"] = min(bar["open"], bar["low"])

        result = simulate_bar(auth_token, bar)
        # 如果 simulate/bar 不可用（Release 构建），跳过
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用（可能是 Release 构建）: {result}")

        # 等待决策写入
        deadline = time.time() + 10
        found = False
        while time.time() < deadline:
            decisions = get_decisions(auth_token, date=today_str())
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
            bar = get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError:
            pytest.skip("无测试数据")

        if not bar["close"] > bar["open"]:
            bar["open"] = bar["close"] * 0.99

        result = simulate_bar(auth_token, bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")

        # 等待决策出现
        deadline = time.time() + 10
        decision = None
        while time.time() < deadline:
            decisions = get_decisions(auth_token, date=today_str())
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
        resp = order_with_decision_id(
            auth_token, TEST_SYMBOL, decision["id"],
            quantity=decision["quantity"], price=decision["price"],
            direct=0 if decision["action"] in ["open_long", "close_short"] else 1,
        )
        assert resp.status_code == 200, f"下单失败: {resp.text}"

        # 验证决策状态更新
        time.sleep(1)
        updated = [
            d for d in get_decisions(auth_token, date=today_str())
            if d["id"] == decision["id"]
        ]
        assert updated, "决策消失"
        assert updated[0]["executed"] is True, "executed 标志未更新"
        assert updated[0]["executedQuantity"] > 0, "executedQuantity 未更新"

    def test_executed_decision_blocks_reorder(self, auth_token, loaded_strategy, is_backtest):
        """已执行决策不能再下单（前端禁用，C++ 端不应崩溃）"""
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        decisions = get_decisions(auth_token, date=today_str())
        executed = [
            d for d in decisions
            if d.get("strategy") == STRATEGY_NAME and d["executed"]
        ]
        if not executed:
            pytest.skip("无已执行决策，跳过测试")

        decision = executed[0]
        # 重复下单不应崩溃（业务上应被前端阻止，但 C++ 端需要健壮）
        resp = order_with_decision_id(
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
            bar = get_latest_bar(auth_token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")

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
            if get_decisions(auth_token, date=today_str()):
                break
            time.sleep(0.5)

        # Day 2（不同日期 → 不同 daily_positions 记录）
        bar2 = dict(bar, datetime="2025-01-16 00:00:00")
        simulate_bar(auth_token, bar2)

        deadline = time.time() + 10
        while time.time() < deadline:
            if get_decisions(auth_token, date=today_str()):
                break
            time.sleep(0.5)

        resp = requests.get(
            f"{BASE_URL}/strategy/performance",
            params={"name": STRATEGY_NAME},
            headers={"Authorization": auth_token},
            verify=False,
            timeout=10,
        )
        assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
        data = resp.json()

        assert data["strategy"] == STRATEGY_NAME
        assert data["trading_days"] >= 1
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
        self, auth_token, loaded_strategy, is_backtest
    ):
        """集成测试：dividend + recalc 后，simulate_bar 触发日终管线，performance 端点返回有效指标

        验证：当 stock_1d 包含分红 + adj_* 计算结果时，日终管线 (ManualTiming → SendSummaryEmail
        → daily_positions 写入) 不被 dividend 流程破坏。
        """
        if not is_backtest:
            pytest.skip("仅在 stock_hist_sim 模式下运行")
        kwargs = {"headers": {"Authorization": auth_token}, "verify": False, "timeout": 10}

        try:
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

            # 2. 触发日终管线：simulate_bar 推送一根 bar
            try:
                bar = get_latest_bar(auth_token, INTEG_SYMBOL,
                                     date="2024-07-02 00:00:00")
            except ValueError as e:
                pytest.skip(f"无测试数据: {e}")

            result = simulate_bar(auth_token, bar)
            if "status" in result and result.get("status") == "error":
                pytest.skip(f"simulate/bar 不可用: {result}")

            # 3. 等 ManualTiming 决策完成
            deadline = time.time() + 10
            while time.time() < deadline:
                if get_decisions(auth_token, date=today_str()):
                    break
                time.sleep(0.5)

            # 4. 验证 daily_positions + 绩效指标
            resp = requests.get(
                f"{BASE_URL}/strategy/performance",
                params={"name": STRATEGY_NAME},
                **kwargs,
            )
            assert resp.ok, f"HTTP {resp.status_code}: {resp.text}"
            perf_data = resp.json()

            assert perf_data["strategy"] == STRATEGY_NAME
            assert "metrics" in perf_data
            for key in ["total_return", "sharpe_ratio", "max_drawdown"]:
                assert key in perf_data["metrics"], \
                    f"dividend recalc 后绩效缺少指标: {key}"

        finally:
            _cleanup_integ_data(auth_token)


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
            result = simulate_bar(auth_token, bar_buy)
            if "status" in result and result.get("status") == "error":
                pytest.skip(f"simulate_bar 不可用: {result}")

            # 等待决策生成
            deadline = time.time() + 10
            buy_decision = None
            while time.time() < deadline:
                decisions = get_decisions(auth_token, date=today_str())
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
            simulate_bar(auth_token, bar_sell)

            # 等待卖出决策
            deadline = time.time() + 10
            sell_decision = None
            while time.time() < deadline:
                decisions = get_decisions(auth_token, date=today_str())
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
