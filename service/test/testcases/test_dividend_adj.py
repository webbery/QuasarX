"""
后复权价格计算测试

测试 FinanceDB::recalcSymbolAdjPrices() 和 calcEventAdjFactor()
基于 BaoStock 涨跌幅复权法:
  adj_factor(T) = Close(T-1) / ex_div_ref_price
  adj_price(t)  = org_price(t) × ∏[factor(e) for e where ex_date > t]

测试用例:
  1. 纯现金分红
  2. 纯送股
  3. 分红+送股混合
  4. ex_div_price 缺失时的公式反推
  5. 多个事件累乘
  6. 无事件时 adj_factor = 1.0
  7. 边界: prev_close = 0
"""
import os
import csv
import math
import requests
import pytest
from pathlib import Path
from tool import check_response, BASE_URL, _DATA_DIR as SERVER_DATA_DIR


# ── Python 黄金标准实现 ──

def calc_event_adj_factor(prev_close, ex_div_price, cash_per_10=0,
                          bonus_per_10=0, transfer_per_10=0):
    """
    计算单个分红事件的后复权因子 (BaoStock 涨跌幅复权法)
    
    adj_factor = prev_close / ex_div_ref_price
    
    当 ex_div_price 缺失时反推:
      ex_ref = prev_close * (1 - cash/10/prev_close + (bonus+transfer)/10)
    """
    if prev_close <= 0:
        return 1.0
    
    ex_ref = ex_div_price
    if ex_ref <= 0:
        cash_ratio = cash_per_10 / 10.0 / prev_close
        stock_ratio = (bonus_per_10 + transfer_per_10) / 10.0
        ex_ref = prev_close * (1.0 - cash_ratio + stock_ratio)
    
    if ex_ref <= 0:
        return 1.0
    
    return prev_close / ex_ref


def calc_cumulative_adj_factor(bar_date, events):
    """
    计算某根 bar 的累乘复权因子
    
    adj_factor(t) = ∏[factor(e) for e where ex_date > t]
    """
    factor = 1.0
    for ev in events:
        if ev['ex_dividend_date'] > bar_date:
            factor *= calc_event_adj_factor(
                ev['prev_close'],
                ev.get('ex_div_price', 0),
                ev.get('cash_per_10', 0),
                ev.get('bonus_per_10', 0),
                ev.get('transfer_per_10', 0)
            )
    return factor


# ── 测试数据 ──

TEST_SYMBOL = 'sh.999998'
TEST_TABLE = 'stock_1d'

# 5 根 bar 的原始 OHLCV
# bar0: 2024-01-02, close=10.00
# bar1: 2024-01-03, close=10.50  ← 股权登记日 (T-1)
# bar2: 2024-01-04, close=10.20  ← 除权除息日 (T), preclose=10.30
# bar3: 2024-01-05, close=10.80
# bar4: 2024-01-08, close=11.00
STOCK_CSV_LINES = [
    "datetime,open,close,high,low,volume,turnover",
    "2024-01-02 00:00:00,9.90,10.00,10.10,9.80,100000,1000000",
    "2024-01-03 00:00:00,10.00,10.50,10.60,9.95,120000,1260000",
    "2024-01-04 00:00:00,10.30,10.20,10.40,10.10,150000,1530000",
    "2024-01-05 00:00:00,10.20,10.80,10.90,10.15,110000,1188000",
    "2024-01-08 00:00:00,10.80,11.00,11.10,10.70,130000,1430000",
]

# 分红事件: 2024-01-04 除权除息
# 每10股派2元现金 → cash_per_10 = 2.0
# 除权参考价 = (10.50 - 0.2) / 1.0 = 10.30
DIVIDEND_CSV_HEADER = [
    "symbol", "announce_date", "report_year", "ex_dividend_date",
    "record_date", "implement_date", "bonus_per_10", "transfer_per_10",
    "cash_per_10", "allot_per_10", "allot_price", "ex_div_price", "action_type"
]
DIVIDEND_CSV_ROW = [
    TEST_SYMBOL, "2024-01-03", "2023", "2024-01-04",
    "2024-01-03", "2024-01-04", "0", "0",
    "2.0", "0", "0", "10.30", "1"
]

# 预期计算:
# 事件: ex_date=2024-01-04, prev_close=10.50 (bar1), ex_div_price=10.30
# factor = 10.50 / 10.30 = 1.019417...
#
# bar0 (2024-01-02): ex_date > bar0 → factor = 1.019417
#   adj_close = 10.00 × 1.019417 = 10.19417
# bar1 (2024-01-03): ex_date > bar1 → factor = 1.019417
#   adj_close = 10.50 × 1.019417 = 10.70388
# bar2 (2024-01-04): ex_date == bar2, NOT > → factor = 1.0
#   adj_close = 10.20 × 1.0 = 10.20
# bar3 (2024-01-05): factor = 1.0
#   adj_close = 10.80
# bar4 (2024-01-08): factor = 1.0
#   adj_close = 11.00

EXPECTED_FACTOR = 10.50 / 10.30  # ≈ 1.019417


@pytest.mark.usefixtures("auth_token")
class TestDividendAdjCalculation:
    """后复权价格计算测试"""

    @staticmethod
    def _kwargs(auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @staticmethod
    def _write_dividend_csv(auth_token):
        """写入分红 CSV 文件到服务数据目录"""
        # 服务运行目录下的 data/dividend/
        dividend_dir = SERVER_DATA_DIR / "dividend"
        dividend_dir.mkdir(parents=True, exist_ok=True)
        csv_path = dividend_dir / f"{TEST_SYMBOL}_dividend.csv"
        
        with open(csv_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(DIVIDEND_CSV_HEADER)
            writer.writerow(DIVIDEND_CSV_ROW)
        
        return str(dividend_dir)

    @staticmethod
    def _cleanup_dividend_csv():
        """清理分红 CSV 文件"""
        csv_path = SERVER_DATA_DIR / "dividend" / f"{TEST_SYMBOL}_dividend.csv"
        if csv_path.exists():
            csv_path.unlink()

    # ── Step 1: 导入股票行情数据 ──

    @pytest.mark.timeout(10)
    def test_1_import_stock_data(self, auth_token):
        """导入测试股票行情到 stock_1d"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'table': TEST_TABLE,
            'symbol': TEST_SYMBOL,
            'adj': 'none',
            'data': STOCK_CSV_LINES
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert data['imported_rows'] == 5

    # ── Step 2: 写入并导入分红数据 ──

    @pytest.mark.timeout(10)
    def test_2_import_dividend_data(self, auth_token):
        """写入分红 CSV 并导入到 finance.db"""
        kwargs = self._kwargs(auth_token)
        
        # 写入 CSV 文件
        dividend_dir = self._write_dividend_csv(auth_token)
        
        # 导入
        kwargs['json'] = {
            'action': 'import',
            'dividend_dir': dividend_dir
        }
        response = requests.post(f"{BASE_URL}/dividend", **kwargs)
        data = check_response(response)
        assert data['status'] == 'completed'
        assert data['imported_rows'] >= 1

    # ── Step 3: 验证分红数据已入库 ──

    @pytest.mark.timeout(10)
    def test_3_query_dividend(self, auth_token):
        """查询分红数据确认已入库"""
        kwargs = self._kwargs(auth_token)
        params = {'code': TEST_SYMBOL}
        response = requests.get(f"{BASE_URL}/dividend", params=params, **kwargs)
        data = check_response(response)
        assert data['count'] >= 1
        assert data['data'][0]['cash_per_10'] == 2.0
        assert data['data'][0]['ex_div_price'] == 10.30

    # ── Step 4: 触发后复权重算 ──

    @pytest.mark.timeout(30)
    def test_4_recalc_adj_prices(self, auth_token):
        """触发后复权价格重算"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'recalc',
            'code': TEST_SYMBOL
        }
        response = requests.post(f"{BASE_URL}/dividend", **kwargs)
        data = check_response(response)
        assert data['status'] == 'completed'
        assert data['bars'] == 5

    # ── Step 5: 验证复权价格 ──

    @pytest.mark.timeout(10)
    def test_5_verify_adj_close(self, auth_token):
        """验证 adj_close 与 Python 黄金标准一致"""
        kwargs = self._kwargs(auth_token)
        params = {'table': TEST_TABLE, 'symbol': TEST_SYMBOL}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert data['count'] == 5

        bars = data['data']
        
        # 按日期排序
        bars.sort(key=lambda b: b['datetime'])

        # bar0 (2024-01-02): 事件在之后 → adj_factor = EXPECTED_FACTOR
        adj_close_0 = bars[0]['adj_close']
        expected_0 = 10.00 * EXPECTED_FACTOR
        assert abs(adj_close_0 - expected_0) < 1e-4, \
            f"bar0: {adj_close_0} != {expected_0}"

        # bar1 (2024-01-03): 事件在之后 → adj_factor = EXPECTED_FACTOR
        adj_close_1 = bars[1]['adj_close']
        expected_1 = 10.50 * EXPECTED_FACTOR
        assert abs(adj_close_1 - expected_1) < 1e-4, \
            f"bar1: {adj_close_1} != {expected_1}"

        # bar2 (2024-01-04): 除权日当天 → adj_factor = 1.0
        adj_close_2 = bars[2]['adj_close']
        assert abs(adj_close_2 - 10.20) < 1e-4, \
            f"bar2: {adj_close_2} != 10.20"

        # bar3 (2024-01-05): 事件在之前 → adj_factor = 1.0
        adj_close_3 = bars[3]['adj_close']
        assert abs(adj_close_3 - 10.80) < 1e-4, \
            f"bar3: {adj_close_3} != 10.80"

        # bar4 (2024-01-08): 事件在之前 → adj_factor = 1.0
        adj_close_4 = bars[4]['adj_close']
        assert abs(adj_close_4 - 11.00) < 1e-4, \
            f"bar4: {adj_close_4} != 11.00"

    # ── Step 6: 验证 adj_open/high/low 也正确 ──

    @pytest.mark.timeout(10)
    def test_6_verify_adj_ohlc(self, auth_token):
        """验证 adj_open/adj_high/adj_low 也按相同因子调整"""
        kwargs = self._kwargs(auth_token)
        params = {'table': TEST_TABLE, 'symbol': TEST_SYMBOL}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        bars = sorted(data['data'], key=lambda b: b['datetime'])

        # bar0: 所有 OHLC 都乘以 EXPECTED_FACTOR
        bar0 = bars[0]
        assert abs(bar0['adj_open'] - 9.90 * EXPECTED_FACTOR) < 1e-4
        assert abs(bar0['adj_high'] - 10.10 * EXPECTED_FACTOR) < 1e-4
        assert abs(bar0['adj_low'] - 9.80 * EXPECTED_FACTOR) < 1e-4

        # bar2: 除权日当天，adj = org (factor=1.0)
        bar2 = bars[2]
        assert abs(bar2['adj_open'] - 10.30) < 1e-4
        assert abs(bar2['adj_high'] - 10.40) < 1e-4
        assert abs(bar2['adj_low'] - 10.10) < 1e-4

    # ── 清理 ──

    @pytest.mark.timeout(10)
    def test_9_cleanup(self, auth_token):
        """清理测试数据"""
        kwargs = self._kwargs(auth_token)
        
        # 删除行情数据
        params = {'table': TEST_TABLE, 'symbol': TEST_SYMBOL}
        requests.delete(f"{BASE_URL}/quote", params=params, **kwargs)
        
        # 删除分红数据
        params = {'code': TEST_SYMBOL}
        requests.delete(f"{BASE_URL}/dividend", params=params, **kwargs)
        
        # 删除 CSV 文件
        self._cleanup_dividend_csv()


@pytest.mark.usefixtures("auth_token")
class TestDividendAdjFactorCalc:
    """calcEventAdjFactor 单元测试 (Python 端验证公式正确性)"""

    def test_cash_dividend(self):
        """纯现金分红: factor = prev_close / (prev_close - div_per_share)"""
        # 每10股派2元, prev_close=10.50, ex_div_price=10.30
        factor = calc_event_adj_factor(10.50, 10.30)
        assert abs(factor - 10.50 / 10.30) < 1e-10

    def test_bonus_shares(self):
        """纯送股: 10送3, prev_close=10.00"""
        # ex_div_price = 10.00 / (1 + 0.3) = 7.6923...
        ex_ref = 10.00 / 1.3
        factor = calc_event_adj_factor(10.00, ex_ref)
        assert abs(factor - 1.3) < 1e-10

    def test_transfer_shares(self):
        """纯转增: 10转5, prev_close=20.00"""
        ex_ref = 20.00 / 1.5
        factor = calc_event_adj_factor(20.00, ex_ref)
        assert abs(factor - 1.5) < 1e-10

    def test_mixed_cash_bonus(self):
        """分红+送股: 10派2送3, prev_close=15.00"""
        # ex_ref = (15.00 - 0.2) / (1 + 0.3) = 14.8 / 1.3 = 11.3846...
        ex_ref = (15.00 - 0.2) / 1.3
        factor = calc_event_adj_factor(15.00, ex_ref)
        expected = 15.00 / ex_ref
        assert abs(factor - expected) < 1e-10

    def test_missing_ex_div_price(self):
        """ex_div_price 缺失时公式反推"""
        # 每10股派2元, prev_close=10.50
        factor = calc_event_adj_factor(10.50, 0, cash_per_10=2.0)
        # ex_ref = 10.50 * (1 - 2/10/10.50) = 10.50 * (1 - 0.019048) = 10.30
        expected_ex_ref = 10.50 * (1.0 - 2.0 / 10.0 / 10.50)
        expected_factor = 10.50 / expected_ex_ref
        assert abs(factor - expected_factor) < 1e-10

    def test_zero_prev_close(self):
        """prev_close = 0 → factor = 1.0"""
        factor = calc_event_adj_factor(0, 10.0)
        assert factor == 1.0

    def test_negative_prev_close(self):
        """prev_close < 0 → factor = 1.0"""
        factor = calc_event_adj_factor(-5.0, 10.0)
        assert factor == 1.0

    def test_cumulative_factor(self):
        """多个事件累乘"""
        events = [
            {'ex_dividend_date': '2024-01-04', 'prev_close': 10.50,
             'ex_div_price': 10.30, 'cash_per_10': 2.0},
            {'ex_dividend_date': '2024-06-15', 'prev_close': 12.00,
             'ex_div_price': 11.50, 'cash_per_10': 1.5},
        ]
        
        # bar at 2024-01-02: both events after → cumulative = f1 * f2
        cum = calc_cumulative_adj_factor('2024-01-02', events)
        f1 = 10.50 / 10.30
        f2 = 12.00 / 11.50
        assert abs(cum - f1 * f2) < 1e-10
        
        # bar at 2024-03-01: only second event after → cumulative = f2
        cum2 = calc_cumulative_adj_factor('2024-03-01', events)
        assert abs(cum2 - f2) < 1e-10
        
        # bar at 2024-07-01: no events after → cumulative = 1.0
        cum3 = calc_cumulative_adj_factor('2024-07-01', events)
        assert cum3 == 1.0


@pytest.mark.usefixtures("auth_token")
class TestBaostockFormulaRegression:
    """Fix 1 回归测试：验证 calcEventAdjFactor 改用 baostock 公式

    每个 case 用独立 symbol (sh.999991~sh.999995)，单独 setup + cleanup。
    关键回归断言：10送3 的预期 factor=1.30，旧公式给出 0.769 → 必然失败以暴露公式回退。
    """

    @staticmethod
    def _kwargs(auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @staticmethod
    def _write_dividend_csv(dividend_dir, symbol, row):
        """写入单条分红事件 CSV"""
        dividend_dir.mkdir(parents=True, exist_ok=True)
        csv_path = dividend_dir / f"{symbol}_dividend.csv"
        with open(csv_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(DIVIDEND_CSV_HEADER)
            writer.writerow(row)
        return str(dividend_dir)

    def _cleanup(self, auth_token, symbol):
        """清理该 symbol 的 stock + dividend 数据"""
        kwargs = self._kwargs(auth_token)
        requests.delete(f"{BASE_URL}/dividend",
                        params={'code': symbol}, **kwargs)
        requests.delete(f"{BASE_URL}/quote",
                        params={'table': TEST_TABLE, 'symbol': symbol}, **kwargs)
        csv_path = SERVER_DATA_DIR / "dividend" / f"{symbol}_dividend.csv"
        if csv_path.exists():
            csv_path.unlink()

    def _verify_baostock_factor(self, auth_token, symbol, stock_lines,
                                dividend_row, expected_factor, tolerance=1e-4):
        """通用流程：import stock → import dividend → recalc → 验证事件前 bar 的 adj_close"""
        kwargs = self._kwargs(auth_token)
        dividend_dir = SERVER_DATA_DIR / "dividend"

        # 1. 导入股票
        kwargs['json'] = {
            'action': 'import',
            'table': TEST_TABLE,
            'symbol': symbol,
            'adj': 'none',
            'data': stock_lines,
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        check_response(response)

        # 2. 写入 + 导入分红
        self._write_dividend_csv(dividend_dir, symbol, dividend_row)
        kwargs['json'] = {
            'action': 'import',
            'dividend_dir': str(dividend_dir),
        }
        response = requests.post(f"{BASE_URL}/dividend", **kwargs)
        check_response(response)

        # 3. 触发 recalc
        kwargs['json'] = {'action': 'recalc', 'code': symbol}
        response = requests.post(f"{BASE_URL}/dividend", **kwargs)
        check_response(response)

        # 4. 验证事件前 bar 的 adj_close = org_close × baostock_factor
        params = {'table': TEST_TABLE, 'symbol': symbol}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        bars = sorted(data['data'], key=lambda b: b['datetime'])

        # 第一根 bar 早于 ex_div_date，应累计因子
        pre_event_bar = bars[0]
        org_close = pre_event_bar['close']
        actual_adj_close = pre_event_bar['adj_close']
        expected_adj_close = org_close * expected_factor

        # 清理
        self._cleanup(auth_token, symbol)

        return org_close, actual_adj_close, expected_adj_close

    @pytest.mark.timeout(30)
    def test_pure_bonus_10sang3(self, auth_token):
        """10送3 (仅送股, 无现金/转增), prev_close=10.00
        baostock: divisor = 1 + 0.3 = 1.3
                  ex_ref = (10 - 0) / 1.3 ≈ 7.6923
                  factor = 10 / 7.6923 = 1.30
        旧公式 (1 - cash_ratio + stock_ratio) = 1.3 看似巧合，但 ex_ref=13 → factor=0.769 → 测试必失败
        """
        symbol = 'sh.999991'
        stock_lines = [
            "datetime,open,close,high,low,volume,turnover",
            "2024-02-01 00:00:00,9.90,10.00,10.10,9.80,100000,1000000",
            "2024-02-02 00:00:00,9.95,10.00,10.10,9.85,100000,1000000",  # 除权日
        ]
        dividend_row = [
            symbol, "2024-01-30", "2023", "2024-02-02",
            "2024-02-01", "2024-02-02",
            "3", "0",   # bonus=3, transfer=0
            "0", "0", "0",  # cash=0, allot=0,0
            "0",        # ex_div_price=0 → 公式反推
            "1",
        ]
        org, actual, expected = self._verify_baostock_factor(
            auth_token, symbol, stock_lines, dividend_row, 1.3)
        assert abs(actual - expected) < 1e-4, \
            f"pure bonus 10送3: org={org}, actual={actual}, expected={expected}"

    @pytest.mark.timeout(30)
    def test_pure_transfer_10zhuan5(self, auth_token):
        """10转5 (仅转增, 无送股/现金), prev_close=20.00
        baostock: divisor = 1 + 0.5 = 1.5
                  ex_ref = (20 - 0) / 1.5 = 13.3333
                  factor = 20 / 13.3333 = 1.50
        """
        symbol = 'sh.999992'
        stock_lines = [
            "datetime,open,close,high,low,volume,turnover",
            "2024-03-01 00:00:00,19.80,20.00,20.20,19.70,100000,2000000",
            "2024-03-04 00:00:00,19.85,20.00,20.20,19.75,100000,2000000",
        ]
        dividend_row = [
            symbol, "2024-02-28", "2023", "2024-03-04",
            "2024-03-01", "2024-03-04",
            "0", "5",   # bonus=0, transfer=5
            "0", "0", "0",
            "0",
            "1",
        ]
        org, actual, expected = self._verify_baostock_factor(
            auth_token, symbol, stock_lines, dividend_row, 1.5)
        assert abs(actual - expected) < 1e-4, \
            f"pure transfer 10转5: org={org}, actual={actual}, expected={expected}"

    @pytest.mark.timeout(30)
    def test_mixed_cash_and_bonus(self, auth_token):
        """10派2送3 (现金+送股混合), prev_close=15.00
        baostock: cash_per_share = 0.2, stock_per_share = 0.3
                  divisor = 1 + 0.3 = 1.3
                  ex_ref = (15 - 0.2) / 1.3 = 14.8/1.3 ≈ 11.3846
                  factor = 15 / 11.3846 ≈ 1.3176
        旧公式 (1 - cash_ratio + stock_ratio) = (1 + 0.3) = 1.3 → ex_ref=19.5
        → factor=15/19.5≈0.769 → 测试必失败
        """
        symbol = 'sh.999993'
        stock_lines = [
            "datetime,open,close,high,low,volume,turnover",
            "2024-04-01 00:00:00,14.80,15.00,15.20,14.70,100000,1500000",
            "2024-04-02 00:00:00,14.85,15.00,15.20,14.75,100000,1500000",
        ]
        dividend_row = [
            symbol, "2024-03-30", "2023", "2024-04-02",
            "2024-04-01", "2024-04-02",
            "3", "0",   # bonus=3
            "2.0", "0", "0",  # cash=2元/10股
            "0",
            "1",
        ]
        expected_factor = 15.0 / ((15.0 - 0.2) / 1.3)
        org, actual, expected = self._verify_baostock_factor(
            auth_token, symbol, stock_lines, dividend_row, expected_factor)
        assert abs(actual - expected) < 1e-4, \
            f"mixed 10派2送3: org={org}, actual={actual}, expected={expected}"

    @pytest.mark.timeout(30)
    def test_pure_cash_10pai2(self, auth_token):
        """10派2 (仅现金, 无送股/转增), prev_close=15.00
        baostock: divisor = 1 + 0 = 1.0
                  ex_ref = (15 - 0.2) / 1.0 = 14.8
                  factor = 15 / 14.8 ≈ 1.01351
        旧公式: (1 - 0.2/15) = 0.9867 → factor=1/0.9867≈1.0135（巧合一致）
        → 此 case 旧/新公式值接近，验证纯派息未破坏
        """
        symbol = 'sh.999994'
        stock_lines = [
            "datetime,open,close,high,low,volume,turnover",
            "2024-05-01 00:00:00,14.80,15.00,15.20,14.70,100000,1500000",
            "2024-05-06 00:00:00,14.85,15.00,15.20,14.75,100000,1500000",
        ]
        dividend_row = [
            symbol, "2024-04-28", "2023", "2024-05-06",
            "2024-05-01", "2024-05-06",
            "0", "0",
            "2.0", "0", "0",
            "0",
            "1",
        ]
        expected_factor = 15.0 / 14.8
        org, actual, expected = self._verify_baostock_factor(
            auth_token, symbol, stock_lines, dividend_row, expected_factor)
        assert abs(actual - expected) < 1e-4, \
            f"pure cash 10派2: org={org}, actual={actual}, expected={expected}"

    @pytest.mark.timeout(30)
    def test_allot_10pei3_price5(self, auth_token):
        """10配3 配股价 5元 (仅配股, 无送转/现金), prev_close=20.00
        baostock: allot_per_share = 0.3, stock_per_share = 0
                  divisor = 1 + 0 + 0.3 = 1.3
                  ex_ref = (20 - 0 + 0.3 × 5) / 1.3 = (20 + 1.5) / 1.3 ≈ 16.538
                  factor = 20 / 16.538 ≈ 1.2093
        旧公式无 allot 项（allot_per_10=0 时分子无变化）→ 退化为纯 cash=0 公式
        → ex_ref = 20, factor = 1.0 → 测试必失败（adj_close=20 而非 24.18）
        """
        symbol = 'sh.999995'
        stock_lines = [
            "datetime,open,close,high,low,volume,turnover",
            "2024-06-01 00:00:00,19.80,20.00,20.20,19.70,100000,2000000",
            "2024-06-03 00:00:00,19.85,20.00,20.20,19.75,100000,2000000",
        ]
        dividend_row = [
            symbol, "2024-05-30", "2023", "2024-06-03",
            "2024-06-01", "2024-06-03",
            "0", "0",   # bonus=0, transfer=0
            "0", "3", "5.0",  # cash=0, allot=3/10, price=5.0
            "0",
            "1",
        ]
        expected_factor = 20.0 / ((20.0 - 0.0 + 0.3 * 5.0) / 1.3)
        org, actual, expected = self._verify_baostock_factor(
            auth_token, symbol, stock_lines, dividend_row, expected_factor)
        assert abs(actual - expected) < 1e-4, \
            f"allot 10配3@5: org={org}, actual={actual}, expected={expected}"
