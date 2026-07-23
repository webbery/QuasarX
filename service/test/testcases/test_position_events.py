"""
持仓事件（公司行为）测试 — 验证回测引擎中分红/送股对持仓和权益的影响

测试分层:
  L2: 集成测试 — 分红导入 + 复权计算 + adj_price 正确性
  L3: 端到端回测 — 双均线策略穿越除权日，验证权益连续性

前置: 服务以 stock_hist_sim 模式启动

使用方法:
  pytest test_position_events.py -v
  pytest test_position_events.py::TestDividendRecalc -v       # L2
  pytest test_position_events.py::TestBacktestDividend -v      # L3
"""
import csv
import json
import math
import requests
import pytest
from pathlib import Path
from tool import check_response, BASE_URL


# 服务从 service/build/ 运行，数据目录相对于此
SERVER_DATA_DIR = Path(__file__).resolve().parent.parent.parent / "build" / "data"


# ═══════════════════════════════════════════════════════════
# 共享测试数据
# ═══════════════════════════════════════════════════════════

SYMBOL = 'sh.999997'
TABLE = 'stock_1d'

# 15 根 bar，除权日在 bar 11 (2024-01-16)
# 价格模式: 平稳 → 上涨(MA 交叉) → 除权下跌 → 恢复
#
# MA3/MA5 交叉分析:
#   bar 0-4: 价格平稳 10.00, MA3=MA5=10.00, 无交叉
#   bar 5-8: 价格上涨, MA3 快速跟随, MA5 滞后
#   bar 9-10: MA3 > MA5, 金叉触发买入
#   bar 11: 除权日, 价格从 11.00 跌至 10.80 (扣减 0.20 股息)
STOCK_CSV_LINES = [
    "datetime,open,close,high,low,volume,turnover",
    "2024-01-02 00:00:00,10.00,10.00,10.10,9.90,100000,1000000",   # bar 0
    "2024-01-03 00:00:00,10.00,10.00,10.10,9.90,100000,1000000",   # bar 1
    "2024-01-04 00:00:00,10.00,10.00,10.10,9.90,100000,1000000",   # bar 2
    "2024-01-05 00:00:00,10.00,10.00,10.10,9.90,100000,1000000",   # bar 3
    "2024-01-08 00:00:00,10.00,10.00,10.10,9.90,100000,1000000",   # bar 4
    "2024-01-09 00:00:00,10.00,10.20,10.30,9.90,120000,1224000",   # bar 5 ↑
    "2024-01-10 00:00:00,10.20,10.40,10.50,10.10,130000,1352000",  # bar 6 ↑
    "2024-01-11 00:00:00,10.40,10.60,10.70,10.30,140000,1484000",  # bar 7 ↑
    "2024-01-12 00:00:00,10.60,10.80,10.90,10.50,150000,1620000",  # bar 8 ↑
    "2024-01-15 00:00:00,10.80,11.00,11.10,10.70,160000,1760000",  # bar 9 ↑ (MA交叉)
    "2024-01-16 00:00:00,10.80,11.00,11.10,10.70,160000,1760000",  # bar 10 (确认)
    # ── 除权除息日: 前收 11.00, 10派2元, 参考价 10.80 ──
    "2024-01-17 00:00:00,10.80,10.80,10.90,10.70,170000,1836000",  # bar 11 除权
    "2024-01-18 00:00:00,10.80,11.00,11.10,10.70,150000,1650000",  # bar 12
    "2024-01-19 00:00:00,11.00,11.20,11.30,10.90,140000,1568000",  # bar 13
    "2024-01-22 00:00:00,11.20,11.40,11.50,11.10,130000,1482000",  # bar 14
]

# 分红事件: 2024-01-17 除权除息, 10派2元
DIVIDEND_HEADER = [
    "symbol", "announce_date", "report_year", "ex_dividend_date",
    "record_date", "implement_date", "bonus_per_10", "transfer_per_10",
    "cash_per_10", "allot_per_10", "allot_price", "ex_div_price", "action_type"
]
DIVIDEND_ROW = [
    SYMBOL, "2024-01-15", "2023", "2024-01-17",
    "2024-01-16", "2024-01-17", "0", "0",
    "2.0", "0", "0", "10.80", "1"
]

# 后复权因子: 11.00 / 10.80 ≈ 1.01852
ADJ_FACTOR = 11.00 / 10.80


# ═══════════════════════════════════════════════════════════
# 辅助函数
# ═══════════════════════════════════════════════════════════

def make_kwargs(auth_token):
    kwargs = {'verify': False}
    if auth_token and len(auth_token) > 10:
        kwargs['headers'] = {'Authorization': auth_token}
    return kwargs


def write_dividend_csv():
    d = SERVER_DATA_DIR / "dividend"
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{SYMBOL}_dividend.csv"
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(DIVIDEND_HEADER)
        w.writerow(DIVIDEND_ROW)
    return str(d)


def cleanup_dividend_csv():
    p = SERVER_DATA_DIR / "dividend" / f"{SYMBOL}_dividend.csv"
    if p.exists():
        p.unlink()


# ═══════════════════════════════════════════════════════════
# L2: 分红数据导入 + 复权计算验证
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("auth_token")
class TestDividendRecalc:
    """
    验证分红数据导入 → 复权因子计算 → adj_price 写入的完整链路

    测试逻辑:
      导入 15 根 bar → 导入分红(10派2) → recalc → 验证 adj_close
      除权前 bar0-10: adj_close = org_close × ADJ_FACTOR
      除权后 bar11-14: adj_close = org_close (factor=1.0)
    """

    @pytest.mark.timeout(10)
    def test_1_import_stock_data(self, auth_token):
        """导入行情数据（原始 + 后复权两份）"""
        kwargs = make_kwargs(auth_token)
        for adj in ['none', 'hfq']:
            kwargs['json'] = {
                'action': 'import', 'table': TABLE,
                'symbol': SYMBOL, 'adj': adj,
                'data': STOCK_CSV_LINES
            }
            resp = requests.post(f"{BASE_URL}/quote/data", **kwargs)
            data = check_response(resp)
            assert data['imported_rows'] == 15

    @pytest.mark.timeout(10)
    def test_2_import_dividend(self, auth_token):
        """写入分红 CSV 并导入 finance.db"""
        kwargs = make_kwargs(auth_token)
        dividend_dir = write_dividend_csv()
        kwargs['json'] = {'action': 'import', 'dividend_dir': dividend_dir}
        resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
        data = check_response(resp)
        assert data['status'] == 'completed'
        assert data['imported_rows'] >= 1

    @pytest.mark.timeout(10)
    def test_3_query_dividend(self, auth_token):
        """查询确认分红数据已入库"""
        kwargs = make_kwargs(auth_token)
        resp = requests.get(f"{BASE_URL}/dividend",
                            params={'code': SYMBOL}, **kwargs)
        data = check_response(resp)
        assert data['count'] >= 1
        ev = data['data'][0]
        assert ev['cash_per_10'] == 2.0
        assert ev['ex_div_price'] == 10.80

    @pytest.mark.timeout(30)
    def test_4_recalc_adj_prices(self, auth_token):
        """触发后复权重算"""
        kwargs = make_kwargs(auth_token)
        kwargs['json'] = {'action': 'recalc', 'code': SYMBOL}
        resp = requests.post(f"{BASE_URL}/dividend", **kwargs)
        data = check_response(resp)
        assert data['status'] == 'completed'
        assert data['bars'] == 15

    @pytest.mark.timeout(10)
    def test_5_verify_adj_close(self, auth_token):
        """
        验证 adj_close 正确性

        除权前 (bar 0-10): adj_close = org_close × (11.00/10.80)
        除权日及之后 (bar 11-14): adj_close = org_close
        """
        kwargs = make_kwargs(auth_token)
        resp = requests.get(f"{BASE_URL}/quote",
                            params={'table': TABLE, 'symbol': SYMBOL},
                            **kwargs)
        data = check_response(resp)
        bars = sorted(data['data'], key=lambda b: b['datetime'])
        assert len(bars) == 15

        # 除权前: bar 0 (close=10.00)
        expected_0 = 10.00 * ADJ_FACTOR
        assert abs(bars[0]['adj_close'] - expected_0) < 1e-3, \
            f"bar0: {bars[0]['adj_close']:.4f} != {expected_0:.4f}"

        # 除权前: bar 10 (close=11.00, 除权前最后一根)
        expected_10 = 11.00 * ADJ_FACTOR
        assert abs(bars[10]['adj_close'] - expected_10) < 1e-3, \
            f"bar10: {bars[10]['adj_close']:.4f} != {expected_10:.4f}"

        # 除权日: bar 11 (close=10.80, factor=1.0)
        assert abs(bars[11]['adj_close'] - 10.80) < 1e-3, \
            f"bar11: {bars[11]['adj_close']:.4f} != 10.80"

        # 除权后: bar 14 (close=11.40)
        assert abs(bars[14]['adj_close'] - 11.40) < 1e-3, \
            f"bar14: {bars[14]['adj_close']:.4f} != 11.40"

    @pytest.mark.timeout(10)
    def test_6_verify_adj_ohlc(self, auth_token):
        """验证 adj_open/adj_high/adj_low 也按相同因子调整"""
        kwargs = make_kwargs(auth_token)
        resp = requests.get(f"{BASE_URL}/quote",
                            params={'table': TABLE, 'symbol': SYMBOL},
                            **kwargs)
        data = check_response(resp)
        bars = sorted(data['data'], key=lambda b: b['datetime'])

        # bar 0: OHLC 都乘以 ADJ_FACTOR
        bar0 = bars[0]
        assert abs(bar0['adj_open'] - 10.00 * ADJ_FACTOR) < 1e-3
        assert abs(bar0['adj_high'] - 10.10 * ADJ_FACTOR) < 1e-3
        assert abs(bar0['adj_low'] - 9.90 * ADJ_FACTOR) < 1e-3

        # bar 11 (除权日): adj = org (factor=1.0)
        bar11 = bars[11]
        assert abs(bar11['adj_open'] - 10.80) < 1e-3
        assert abs(bar11['adj_high'] - 10.90) < 1e-3
        assert abs(bar11['adj_low'] - 10.70) < 1e-3

    @pytest.mark.timeout(10)
    def test_9_cleanup(self, auth_token):
        kwargs = make_kwargs(auth_token)
        requests.delete(f"{BASE_URL}/quote",
                        params={'table': TABLE, 'symbol': SYMBOL}, **kwargs)
        requests.delete(f"{BASE_URL}/dividend",
                        params={'code': SYMBOL}, **kwargs)
        cleanup_dividend_csv()


# ═══════════════════════════════════════════════════════════
# L3: 端到端回测 — 分红/送股/混合 三种场景
# ═══════════════════════════════════════════════════════════

INITIAL_CAPITAL = 500000.0

# 使用真实标的（InitStrategy 需要 _markets 中存在）
BT_SYMBOL = 'sz.000001'

# 基础行情数据（除权前 bar10 close=11.00）
# 每个场景需要覆盖 ex-date bar 的价格
_BASE_BARS = STOCK_CSV_LINES[:11]  # bar 0-10（不含除权日）


def _make_stock_lines(ex_open, ex_close, ex_high, ex_low):
    """生成含除权日 bar 的完整行情"""
    ex_bar = f"2024-01-17 00:00:00,{ex_open},{ex_close},{ex_high},{ex_low},170000,1836000"
    return _BASE_BARS + [ex_bar] + STOCK_CSV_LINES[12:]  # bar 11 + bar 12-14


# 三种分红场景
SCENARIOS = [
    {
        "name": "纯现金分红",
        "dividend_row": [
            BT_SYMBOL, "2024-01-15", "2023", "2024-01-17",
            "2024-01-16", "2024-01-17",
            "0", "0", "2.0",   # 10派2元
            "0", "0", "10.80", "1"
        ],
        "stock_lines": _make_stock_lines(10.80, 10.80, 10.90, 10.70),
        # 除权参考价 = (11.00 - 0.2) / 1.0 = 10.80
    },
    {
        "name": "纯送股",
        "dividend_row": [
            BT_SYMBOL, "2024-01-15", "2023", "2024-01-17",
            "2024-01-16", "2024-01-17",
            "3.0", "0", "0",   # 10送3
            "0", "0", "8.46", "2"
        ],
        "stock_lines": _make_stock_lines(8.46, 8.46, 8.56, 8.36),
        # 除权参考价 = 11.00 / 1.3 ≈ 8.46
    },
    {
        "name": "混合(派+送)",
        "dividend_row": [
            BT_SYMBOL, "2024-01-15", "2023", "2024-01-17",
            "2024-01-16", "2024-01-17",
            "3.0", "0", "2.0",  # 10派2送3
            "0", "0", "8.31", "5"
        ],
        "stock_lines": _make_stock_lines(8.31, 8.31, 8.41, 8.21),
        # 除权参考价 = (11.00 - 0.2) / 1.3 ≈ 8.31
    },
]


def _load_bt_strategy():
    """加载已有策略，替换标的为 BT_SYMBOL"""
    path = Path(__file__).parent.parent / "script" / "ma_graph_strategy.json"
    with open(path, 'r', encoding='utf-8') as f:
        strategy = json.load(f)
    for node in strategy.get("nodes", []):
        params = node.get("data", {}).get("params", {})
        if "code" in params:
            params["code"]["value"] = [BT_SYMBOL]
    return strategy


def _write_bt_dividend_csv(dividend_row):
    d = SERVER_DATA_DIR / "dividend"
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{BT_SYMBOL}_dividend.csv"
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(DIVIDEND_HEADER)
        w.writerow(dividend_row)
    return str(d)


def _cleanup_bt():
    p = SERVER_DATA_DIR / "dividend" / f"{BT_SYMBOL}_dividend.csv"
    if p.exists():
        p.unlink()


@pytest.mark.usefixtures("auth_token")
class TestBacktestDividend:
    """
    三种公司行为场景的端到端回测验证

    每个场景:
      1. 导入行情(含除权日价格) + 分红事件 + 重算复权
      2. 运行 MA 交叉策略回测
      3. 验证除权日 daily_return > -0.5%（权益连续）
    """

    @staticmethod
    def _run_scenario(auth_token, scenario):
        """运行单个场景的回测并验证"""
        name = scenario["name"]
        headers = {}
        if auth_token and len(auth_token) > 10:
            headers = {'Authorization': auth_token}

        # 导入行情 (none + hfq)
        for adj in ['none', 'hfq']:
            resp = requests.post(f"{BASE_URL}/quote/data",
                                 json={'action': 'import', 'table': TABLE,
                                       'symbol': BT_SYMBOL, 'adj': adj,
                                       'data': scenario["stock_lines"]},
                                 headers=headers, verify=False)
            check_response(resp)

        # 导入分红 + 重算复权
        _write_bt_dividend_csv(scenario["dividend_row"])
        check_response(requests.post(f"{BASE_URL}/dividend",
                                     json={'action': 'import',
                                           'dividend_dir': str(SERVER_DATA_DIR / "dividend")},
                                     headers=headers, verify=False))

        data = check_response(requests.post(f"{BASE_URL}/dividend",
                                            json={'action': 'recalc', 'code': BT_SYMBOL},
                                            headers=headers, verify=False))
        assert data['status'] == 'completed', f"[{name}] recalc failed"

        # 回测
        strategy = _load_bt_strategy()
        strategy_str = json.dumps(strategy, ensure_ascii=False)
        resp = requests.post(f"{BASE_URL}/backtest",
                             json={'script': strategy_str},
                             headers=headers, verify=False, timeout=60)
        result = check_response(resp)

        # 验证权益连续性
        daily_returns = result.get('daily_returns', [])
        daily_dates = result.get('daily_dates', [])
        assert len(daily_returns) > 0, f"[{name}] 无每日收益率"

        # 找除权日索引 (2024-01-17 → epoch 1705449600)
        ex_idx = None
        for i, d in enumerate(daily_dates):
            if isinstance(d, (int, float)) and 1705392000 <= d <= 1705564800:
                ex_idx = i
                break
            elif isinstance(d, str) and '2024-01-17' in str(d):
                ex_idx = i
                break

        if ex_idx is not None and ex_idx > 0:
            ex_return = daily_returns[ex_idx]
            # 重建权益
            portfolio = [INITIAL_CAPITAL]
            for r in daily_returns:
                portfolio.append(portfolio[-1] * (1 + r))
            equity_pre = portfolio[ex_idx]
            equity_ex = portfolio[ex_idx + 1]

            print(f"\n[{name}] 除权日 idx={ex_idx}")
            print(f"  除权日收益率: {ex_return*100:.4f}%")
            print(f"  除权前权益: {equity_pre:.2f}")
            print(f"  除权后权益: {equity_ex:.2f}")
            print(f"  权益变化: {(equity_ex/equity_pre - 1)*100:.4f}%")

            assert ex_return > -0.005, (
                f"[{name}] 除权日收益率 {ex_return:.4%} 过低，"
                f"分红/送股处理可能未生效（预期 > -0.5%）"
            )
        else:
            print(f"\n[{name}] WARN: 未找到除权日数据点")

        # 清理本场景数据
        requests.delete(f"{BASE_URL}/quote",
                        params={'table': TABLE, 'symbol': BT_SYMBOL},
                        headers=headers, verify=False)
        requests.delete(f"{BASE_URL}/dividend",
                        params={'code': BT_SYMBOL},
                        headers=headers, verify=False)
        _cleanup_bt()

    @pytest.mark.timeout(120)
    @pytest.mark.parametrize("scenario", SCENARIOS, ids=[s["name"] for s in SCENARIOS])
    def test_dividend_scenarios(self, auth_token, scenario):
        """参数化测试：纯现金 / 纯送股 / 混合"""
        self._run_scenario(auth_token, scenario)
