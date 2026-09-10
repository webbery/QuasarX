#!/usr/bin/env python3
"""
回测指标正确性验证测试

测试方案：
1. 标准数据集 + 已知答案测试（确定性用例）
2. Python 金融库（empyrical）作为黄金标准对比
3. 通过 HTTP API 提交回测请求，对比 C++ 输出与预期值

使用方法：
  pytest test_backtest_metrics.py -v
  pytest test_backtest_metrics.py::TestStandardCases::test_up_trend -v
  pytest test_backtest_metrics.py::TestEmpyricalComparison -v
  
前置准备：
  python generate_test_data.py  # 生成测试数据
"""

import pytest
import requests
import json
import math
import numpy as np
import pandas as pd
import backtrader as bt
from pathlib import Path
from typing import Dict, List, Optional

# 抑制 SSL 警告（本地测试不需要证书验证）
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 回测配置
INITIAL_CAPITAL = 500000.0
COMMISSION = 0.0003
STAMP_TAX = 0.001
YEAR_DAYS = 252
RISK_FREE_RATE = 0.0

# 测试数据目录
# 策略脚本和汇总文件在 testcases/metric_test_data/
METRIC_TEST_DIR = Path(__file__).parent / "metric_test_data"
TEST_CASES_SUMMARY = METRIC_TEST_DIR / "test_cases_summary.json"


# ============================================================
# 辅助函数：手动计算指标（用于对比）
# ============================================================

def compute_total_return(values: List[float]) -> float:
    """总收益率 = (最终值 - 初始值) / 初始值"""
    if not values or values[0] == 0:
        return 0.0
    return (values[-1] - values[0]) / values[0]


def compute_daily_returns(values: List[float]) -> List[float]:
    """日收益率序列"""
    returns = []
    for i in range(1, len(values)):
        if values[i-1] != 0:
            returns.append((values[i] - values[i-1]) / values[i-1])
    return returns


def compute_annual_return(values: List[float]) -> float:
    """年化收益率"""
    total = compute_total_return(values)
    n = len(values)
    if total <= -1 or n < 2:
        return 0.0
    return (1 + total) ** (YEAR_DAYS / n) - 1


def compute_annual_volatility(daily_returns: List[float]) -> float:
    """年化波动率 = std(daily_returns) * sqrt(252)"""
    if len(daily_returns) < 2:
        return 0.0
    return float(np.std(daily_returns, ddof=1) * math.sqrt(YEAR_DAYS))


def compute_sharpe_ratio(values: List[float], daily_returns: List[float]) -> float:
    """夏普比率 = (年化收益 - 无风险利率) / 年化波动率"""
    ann_ret = compute_annual_return(values)
    ann_vol = compute_annual_volatility(daily_returns)
    if ann_vol < 1e-6:  # 防止除以极小数
        return 0.0  # 无波动率时夏普无意义
    return (ann_ret - RISK_FREE_RATE) / ann_vol


def compute_max_drawdown(values: List[float]) -> float:
    """最大回撤（正值）"""
    peak = values[0]
    max_dd = 0.0
    for v in values:
        if v > peak:
            peak = v
        dd = (peak - v) / peak if peak > 0 else 0
        if dd > max_dd:
            max_dd = dd
    return max_dd


def compute_win_rate(daily_returns: List[float]) -> float:
    """胜率 = 正收益天数 / 总天数"""
    if not daily_returns:
        return 0.0
    wins = sum(1 for r in daily_returns if r > 0)
    return wins / len(daily_returns)


def compute_calmar_ratio(values: List[float], max_dd: float) -> float:
    """卡玛比率 = 年化收益 / 最大回撤"""
    if max_dd == 0:
        return 0.0
    return compute_annual_return(values) / max_dd


def compute_r_squared(values: List[float]) -> float:
    """R² = (sxy)² / (sxx * sst)，组合价值对时间线性回归"""
    n = len(values)
    if n < 2:
        return 0.0

    mean_y = sum(values) / n
    mean_x = (n - 1) / 2.0

    sxx = 0.0
    sxy = 0.0
    for i in range(n):
        dx = i - mean_x
        dy = values[i] - mean_y
        sxx += dx * dx
        sxy += dx * dy

    if sxx == 0:
        return 0.0

    sst = sum((v - mean_y) ** 2 for v in values)
    if sst == 0:
        return 0.0

    return (sxy * sxy) / (sxx * sst)


def compute_var(daily_returns: List[float], confidence: float = 0.95) -> float:
    """VaR (历史模拟法)"""
    if not daily_returns:
        return 0.0
    sorted_returns = sorted(daily_returns)
    index = int((1 - confidence) * len(sorted_returns))
    index = min(index, len(sorted_returns) - 1)
    return -sorted_returns[index]


def compute_cvar(daily_returns: List[float], confidence: float = 0.95) -> float:
    """CVaR / ES (尾部平均损失)"""
    if not daily_returns:
        return 0.0
    sorted_returns = sorted(daily_returns)
    index = int((1 - confidence) * len(sorted_returns))
    index = min(index, len(sorted_returns) - 1)
    tail = sorted_returns[:index + 1]
    return -sum(tail) / len(tail) if tail else 0.0


# ============================================================
# 标准测试数据生成器
# ============================================================

def generate_trend_prices(start: float, drift: float, vol: float, days: int, seed: int = 42) -> List[float]:
    """生成带趋势的价格序列"""
    np.random.seed(seed)
    prices = [start]
    for _ in range(days - 1):
        noise = np.random.normal(0, vol)
        new_price = prices[-1] * (1 + drift + noise)
        prices.append(max(new_price, 1.0))  # 价格不低于 1
    return prices


# ============================================================
# 测试用例数据
# ============================================================

STANDARD_CASES = {
    "up_trend": {
        "name": "单边上涨",
        "prices": [100.0, 101.0, 102.0, 103.0, 104.0, 105.0, 106.0, 107.0, 108.0, 109.0, 110.0],
        "expected": {
            "total_return": 0.10,
            "win_rate": 1.0,
            "max_drawdown": 0.0,
        },
        "tolerance": 0.001
    },
    "down_trend": {
        "name": "单边下跌",
        "prices": [100.0, 99.0, 98.0, 97.0, 96.0, 95.0, 94.0, 93.0, 92.0, 91.0, 90.0],
        "expected": {
            "total_return": -0.10,
            "win_rate": 0.0,
            "max_drawdown": 0.10,
        },
        "tolerance": 0.001
    },
    "rise_fall": {
        "name": "先涨后跌",
        "prices": [100.0, 105.0, 110.0, 115.0, 110.0, 105.0, 100.0, 95.0, 100.0, 105.0, 100.0],
        "expected": {
            "total_return": 0.0,
            "max_drawdown": 0.1739,
        },
        "tolerance": 0.01
    },
    "sideways": {
        "name": "横盘震荡",
        "prices": [100.0, 100.5, 99.5, 100.2, 99.8, 100.1, 99.9, 100.3, 99.7, 100.0, 100.0],
        "expected": {
            "total_return": 0.0,
        },
        "tolerance": 0.001
    },
    "high_volatility": {
        "name": "高波动率",
        "prices": [100.0, 110.0, 90.0, 105.0, 95.0, 115.0, 85.0, 100.0, 120.0, 80.0, 100.0],
        "expected": {
            "total_return": 0.0,
        },
        "tolerance": 0.01
    },
    "long_trend": {
        "name": "长期趋势",
        "prices": generate_trend_prices(100, 0.002, 0.005, 50),
        "expected": {
            "total_return_range": [0.08, 0.12],
            "win_rate_range": [0.55, 0.75],
        },
        "tolerance": 0.01
    }
}


# ============================================================
# 回测 API 交互
# ============================================================

def get_auth_token() -> str:
    """获取认证 token"""
    resp = requests.post(
        f"{BASE_URL}/user/login",
        json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL
    )
    resp.raise_for_status()
    return resp.json()["tk"]


def run_backtest_via_api(strategy_json_path: str, token: str) -> Dict:
    """
    通过 API 执行回测并获取指标
    
    参数:
        strategy_json_path: 策略脚本 JSON 文件路径
        token: 认证 token（纯 token 值，不需要 "Bearer" 前缀）
    
    返回:
        回测结果字典，包含 features 字段
    """
    # 注意：服务端 JWTMiddleWare 期望 Authorization header 是纯 token
    headers = {"Authorization": token}
    
    # 1. 读取策略脚本
    with open(strategy_json_path, 'r') as f:
        strategy = json.load(f)
    
    # 2. 将策略 JSON 序列化为字符串（服务端要求 script 是字符串）
    strategy_str = json.dumps(strategy, ensure_ascii=False)
    
    # 3. 提交回测请求
    resp = requests.post(
        f"{BASE_URL}/backtest",
        json={"script": strategy_str},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=300  # 5 分钟超时
    )
    resp.raise_for_status()
    
    # 4. 解析结果
    result = resp.json()
    return result


def extract_cpp_metrics(result: Dict) -> Dict:
    """
    从 C++ 回测结果中提取指标

    返回扁平化的指标字典
    """
    metrics = {}

    # 从 features 字段提取
    features = result.get("features", {})
    for key, value in features.items():
        if value is None:
            continue
        try:
            metrics[key] = float(value)
        except (TypeError, ValueError):
            continue
    
    # 从 summary 字段提取（作为备用）
    summary = result.get("summary", {})
    mapping = {
        "sharp": "sharpe",
        "annual_return": "annual_return",
        "annual_volatility": "annual_volatility",
        "total_return": "total_return",
        "max_drawdown": "max_drawdown",
        "win_rate": "win_rate",
        "calmar_ratio": "calmar"
    }
    for cpp_key, metric_key in mapping.items():
        if cpp_key in summary and metric_key not in metrics:
            metrics[metric_key] = float(summary[cpp_key])
    
    return metrics


def load_test_cases_summary() -> Dict:
    """加载测试用例汇总"""
    if not TEST_CASES_SUMMARY.exists():
        pytest.skip(f"测试数据未生成，请先运行: python generate_test_data.py")
    
    with open(TEST_CASES_SUMMARY, 'r') as f:
        return json.load(f)


# ============================================================
# 测试类：标准数据集验证
# ============================================================

class TestStandardCases:
    """使用标准数据集验证指标计算正确性"""

    @pytest.fixture(autouse=True)
    def setup(self):
        """获取认证 token 和测试用例"""
        self.token = get_auth_token()
        self.cases = load_test_cases_summary()

    def _assert_metric(self, actual: float, expected: float, tolerance: float, name: str):
        """断言指标在容差范围内

        容差解释：
        - tolerance 是**绝对差异**的最大允许值（如 0.05 表示允许 ±0.05 的差异）
        - 对于收益率等指标，0.05 的容差意味着允许 5 个百分点的偏差
        """
        abs_diff = abs(actual - expected)
        assert abs_diff < tolerance, (
            f"{name}: 实际={actual:.6f}, 预期={expected:.6f}, "
            f"绝对差异={abs_diff:.6f}, 容差={tolerance}"
        )

    def _assert_range(self, actual: float, range_val: list, name: str):
        """断言指标在指定范围内"""
        low, high = range_val
        assert low <= actual <= high, (
            f"{name}: 实际={actual:.6f}, 预期范围=[{low}, {high}]"
        )

    def _run_cpp_backtest(self, case_id: str) -> Dict:
        """运行 C++ 回测并提取指标"""
        case = self.cases[case_id]
        strategy_path = METRIC_TEST_DIR / case["strategy_file"]
        
        if not strategy_path.exists():
            pytest.skip(f"策略文件不存在: {strategy_path}")
        
        result = run_backtest_via_api(str(strategy_path), self.token)
        return extract_cpp_metrics(result)

    def test_up_trend(self):
        """测试单边上涨：胜率100%，无回撤"""
        case_id = "up_trend"
        case = self.cases[case_id]
        expected = case["expected"]

        # 运行 C++ 回测
        cpp_metrics = self._run_cpp_backtest(case_id)

        # 对比指标
        tolerance = 0.05  # 5% 容差（考虑佣金和滑点）
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")
        self._assert_metric(cpp_metrics.get("win_rate", 0), expected["win_rate"], tolerance, "胜率")
        self._assert_metric(cpp_metrics.get("max_drawdown", 0), expected["max_drawdown"], tolerance, "最大回撤")
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")

        print(f"\n[UP_TREND] C++: total_return={cpp_metrics.get('total_return')}, "
              f"expected={expected['total_return']}")

    def test_down_trend(self):
        """测试单边下跌：胜率0%，最大回撤10%"""
        case_id = "down_trend"
        case = self.cases[case_id]
        expected = case["expected"]

        cpp_metrics = self._run_cpp_backtest(case_id)

        tolerance = 0.05
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")
        self._assert_metric(cpp_metrics.get("win_rate", 0), expected["win_rate"], tolerance, "胜率")
        self._assert_metric(cpp_metrics.get("max_drawdown", 0), expected["max_drawdown"], tolerance, "最大回撤")
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")

    def test_rise_fall(self):
        """测试先涨后跌：最大回撤约17.39%"""
        case_id = "rise_fall"
        case = self.cases[case_id]
        expected = case["expected"]

        cpp_metrics = self._run_cpp_backtest(case_id)

        tolerance = 0.05
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")
        self._assert_metric(cpp_metrics.get("max_drawdown", 0), expected["max_drawdown"], tolerance, "最大回撤")
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")

    def test_sideways(self):
        """测试横盘震荡：总收益接近0"""
        case_id = "sideways"
        case = self.cases[case_id]
        expected = case["expected"]

        cpp_metrics = self._run_cpp_backtest(case_id)

        tolerance = 0.05
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")

    def test_high_volatility(self):
        """测试高波动率：总收益接近0，但波动率大"""
        case_id = "high_volatility"
        case = self.cases[case_id]
        expected = case["expected"]

        cpp_metrics = self._run_cpp_backtest(case_id)

        tolerance = 0.05
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")

        # 验证波动率不为0
        assert cpp_metrics.get("annual_volatility", 0) > 0.1, "高波动率用例预期波动率>0.1"
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")

    def test_steady_trend(self):
        """测试长期趋势：指标在合理范围内"""
        case_id = "steady_trend"
        case = self.cases[case_id]
        expected = case["expected"]

        cpp_metrics = self._run_cpp_backtest(case_id)

        tolerance = 0.10  # 10% 容差（长期趋势允许更大偏差）
        self._assert_metric(cpp_metrics.get("total_return", 0), expected["total_return"], tolerance, "总收益率")
        self._assert_metric(cpp_metrics.get("win_rate", 0), expected["win_rate"], tolerance, "胜率")
        self._assert_metric(cpp_metrics.get("max_drawdown", 0), expected["max_drawdown"], tolerance, "最大回撤")
        self._assert_metric(cpp_metrics.get("var_95", 0), expected.get("var_95", 0), tolerance, "VaR(95%)")
        self._assert_metric(cpp_metrics.get("es", 0), expected.get("es", 0), tolerance, "CVaR(95%)")


# ============================================================

# ============================================================
# 测试类：数学属性验证
# ============================================================

class TestMathProperties:
    """验证指标的数学性质（属性测试）"""

    def test_max_drawdown_bounds(self):
        """最大回撤必须在 [0, 1] 范围内"""
        np.random.seed(42)
        for _ in range(10):
            returns = list(np.random.normal(0, 0.05, 100))
            values = [1000]
            for r in returns:
                values.append(values[-1] * (1 + r))
            
            max_dd = compute_max_drawdown(values)
            assert 0 <= max_dd <= 1, f"最大回撤越界: {max_dd}"

    def test_sharpe_zero_volatility(self):
        """无波动率时夏普比率为0"""
        values = [100, 101, 102, 103, 104]
        daily_returns = compute_daily_returns(values)
        
        # 修改为无波动（所有收益率相同）
        constant_returns = [0.01] * 10
        constant_values = [100 * (1.01) ** i for i in range(11)]
        
        sharpe = compute_sharpe_ratio(constant_values, constant_returns)
        assert sharpe == 0, f"恒定收益率夏普应为0，实际={sharpe}"

    def test_sharpe_positive_correlation(self):
        """收益率翻倍，夏普比率应增加"""
        np.random.seed(42)
        returns = list(np.random.normal(0.001, 0.02, 100))
        values = [100000]
        for r in returns:
            values.append(values[-1] * (1 + r))
        
        sharpe1 = compute_sharpe_ratio(values, returns)
        
        # 收益率翻倍
        doubled_returns = [r * 2 for r in returns]
        doubled_values = [100000]
        for r in doubled_returns:
            doubled_values.append(doubled_values[-1] * (1 + r))
        
        sharpe2 = compute_sharpe_ratio(doubled_values, doubled_returns)
        
        assert sharpe2 > sharpe1, f"收益率翻倍后夏普应增加: {sharpe1} -> {sharpe2}"

    def test_win_rate_bounds(self):
        """胜率必须在 [0, 1] 范围内"""
        np.random.seed(42)
        for _ in range(10):
            returns = list(np.random.normal(0, 0.05, 50))
            wr = compute_win_rate(returns)
            assert 0 <= wr <= 1, f"胜率越界: {wr}"

    def test_r_squared_bounds(self):
        """R² 必须在 [0, 1] 范围内（对于单调递增序列）"""
        # 完美线性增长
        values = [100 + i * 1 for i in range(50)]
        r2 = compute_r_squared(values)
        assert abs(r2 - 1.0) < 0.001, f"完美线性趋势R²应接近1，实际={r2}"
        
        # 恒定值
        constant_values = [100] * 50
        r2_constant = compute_r_squared(constant_values)
        assert r2_constant == 0, f"恒定值R²应为0，实际={r2_constant}"

    def test_var_cvar_relationship(self):
        """CVaR 应该 >= VaR（尾部平均损失 >= 分位数损失）"""
        np.random.seed(42)
        returns = list(np.random.normal(0, 0.02, 252))
        
        var = compute_var(returns)
        cvar = compute_cvar(returns)
        
        assert cvar >= var, f"CVaR({cvar:.4f}) 应该 >= VaR({var:.4f})"


# ============================================================
# MA 交叉策略端到端测试（backtrader vs C++ 策略图）
# ============================================================

class _MACrossoverStrategy(bt.Strategy):
    """
    MA(5)/MA(15) 金叉死叉策略，对齐 C++ 策略图回测。

    对齐要点：
    - data0 = hfq（后复权）→ MA 指标计算
    - data1 = org（原始价格）→ 交易执行 + 持仓估值
    - 成交价 = org_close（slippageModel=0 时无滑点）
    - 100 股整手取整，与 C++ PortfolioNode 一致
    - 快照在交易前记录（对齐 C++ matchOrders → recordDailySnapshot → RunGraph 时序）
    """
    params = (('ma_short', 5), ('ma_long', 15))

    def __init__(self):
        self.ma_s = bt.indicators.SMA(self.data0.close, period=self.p.ma_short)
        self.ma_l = bt.indicators.SMA(self.data0.close, period=self.p.ma_long)
        self.recorded_values = []
        self._cash = INITIAL_CAPITAL
        self._position = 0

    def prenext(self):
        # 预热期（MA 尚未就绪）：只记录快照，不交易
        # 对齐 C++ recordDailySnapshot 从 bar 0 开始记录的行为
        org_close = self.data1.close[0]
        self.recorded_values.append(self._cash + self._position * org_close)

    def next(self):
        org_close = self.data1.close[0]

        # ① 记录快照（对齐 C++ recordDailySnapshot：matchOrders 后、RunGraph 前）
        self.recorded_values.append(self._cash + self._position * org_close)

        if len(self) < 2:
            return
        if (math.isnan(self.ma_s[0]) or math.isnan(self.ma_l[0]) or
                math.isnan(self.ma_s[-1]) or math.isnan(self.ma_l[-1])):
            return

        # ② 信号检测 + 交易执行（对齐 C++ RunGraph: SignalNode → PortfolioNode → ExecuteNode）
        golden = (self.ma_s[0] > self.ma_l[0] and
                  self.ma_s[-1] <= self.ma_l[-1])
        death = (self.ma_s[0] < self.ma_l[0] and
                 self.ma_s[-1] >= self.ma_l[-1])

        if self._position == 0 and golden:
            size = int(self._cash / org_close / 100) * 100
            if size > 0:
                cost = size * org_close
                comm = max(5.0, cost * 0.0003)
                self._cash -= (cost + comm)
                self._position = size

        elif self._position > 0 and death:
            revenue = self._position * org_close
            comm = max(5.0, revenue * 0.0003)
            stamp = revenue * 0.001
            self._cash += (revenue - comm - stamp)
            self._position = 0


def _run_python_backtrader(hfq_csv_path: str, org_csv_path: str) -> dict:
    """
    用 backtrader 运行 MA(5)/MA(15) 交叉策略，返回与 C++ summary 对齐的指标。

    对齐 C++ 执行模型：
    - 指标计算用后复权价格（hfq），交易执行用原始价格（org）
    - 信号 bar T 挂单 → 成交价 = bar T org close ± 滑点
    - 100 股整手取整
    - nextpost() 记录快照（成交后估值）
    """
    df_hfq = pd.read_csv(hfq_csv_path)
    df_hfq['datetime'] = pd.to_datetime(df_hfq['datetime'])
    df_hfq = df_hfq.set_index('datetime')

    df_org = pd.read_csv(org_csv_path)
    df_org['datetime'] = pd.to_datetime(df_org['datetime'])
    df_org = df_org.set_index('datetime')

    cerebro = bt.Cerebro(runonce=False)

    data0 = bt.feeds.PandasData(
        dataname=df_hfq,
        open='open', high='high', low='low', close='close', volume='volume',
        openinterest=-1,
    )
    data1 = bt.feeds.PandasData(
        dataname=df_org,
        open='open', high='high', low='low', close='close', volume='volume',
        openinterest=-1,
    )
    cerebro.adddata(data0)
    cerebro.adddata(data1)
    cerebro.addstrategy(_MACrossoverStrategy)
    cerebro.broker.setcash(INITIAL_CAPITAL)

    results = cerebro.run()
    values = results[0].recorded_values
    if len(values) < 2:
        return {"total_return": 0, "annual_return": 0, "sharpe": 0,
                "max_drawdown": 0, "win_rate": 0, "calmar_ratio": 0}

    daily_returns = [(values[i] - values[i - 1]) / values[i - 1]
                     for i in range(1, len(values))]

    total_return = (values[-1] - values[0]) / values[0]
    n = len(daily_returns)
    annual_return = (1 + total_return) ** (YEAR_DAYS / n) - 1 if total_return > -1 else 0.0
    annual_vol = float(np.std(daily_returns, ddof=1) * np.sqrt(YEAR_DAYS)) if n > 1 else 0.0
    sharpe = (annual_return - RISK_FREE_RATE) / annual_vol if annual_vol >= 1e-6 else 0.0

    peak = values[0]
    max_dd = 0.0
    for v in values:
        if v > peak:
            peak = v
        dd = (peak - v) / peak if peak > 0 else 0
        max_dd = max(max_dd, dd)

    win_rate = sum(1 for r in daily_returns if r > 0) / n if n > 0 else 0.0
    calmar = annual_return / max_dd if max_dd > 0 else 0.0

    return {
        "total_return": round(total_return, 6),
        "annual_return": round(annual_return, 6),
        "sharpe": round(sharpe, 6),
        "max_drawdown": round(max_dd, 6),
        "win_rate": round(win_rate, 6),
        "calmar_ratio": round(calmar, 6),
    }


class TestMACrossoverE2E:
    """
    MA(5)/MA(15) 交叉策略端到端验证：backtrader Python 回测 vs C++ 策略图回测。

    使用 ma_graph_strategy.json（sz.900005，2023-01-01 ~ 2023-06-30），
    同一份 CSV 数据分别走 backtrader 和 C++ 回测引擎，对比 summary 指标。
    """

    STRATEGY_PATH = Path(__file__).parent.parent / "script" / "ma_graph_strategy.json"

    @staticmethod
    def _find_csv(symbol: str, subdir: str = "A_hfq") -> Path:
        """定位服务数据目录中的 CSV"""
        service_root = Path(__file__).parent.parent.parent
        for sub in ["build/data", "data"]:
            p = service_root / sub / subdir / f"{symbol}.csv"
            if p.exists():
                return p
        raise FileNotFoundError(f"CSV not found for {symbol} in {subdir}")

    def test_ma_crossover_metrics(self, auth_token, is_backtest):
        """backtrader vs C++ 回测指标对比"""
        if not is_backtest:
            pytest.skip("仅在回测模式下运行")
        if not self.STRATEGY_PATH.exists():
            pytest.skip(f"策略文件不存在: {self.STRATEGY_PATH}")

        symbol = "sz.900005"
        hfq_path = self._find_csv(symbol, "A_hfq")
        org_path = self._find_csv(symbol, "AStock")

        # --- Python backtrader 回测 ---
        py_metrics = _run_python_backtrader(str(hfq_path), str(org_path))
        print(f"\n[Python backtrader] trades executed, "
              f"total_return={py_metrics['total_return']:.4f}")

        # --- C++ 策略图回测 ---
        strategy = json.loads(self.STRATEGY_PATH.read_text())
        cpp_result = run_backtest_via_api(str(self.STRATEGY_PATH), auth_token)
        s = cpp_result.get("summary", {})

        # --- 对比 ---
        # C++ summary 字段名映射（C++ 用 "sharp" 而非 "sharpe"）
        cpp_key_map = {"sharpe": "sharp"}
        comparisons = [
            ("total_return", "总收益率", 0.02),
            ("annual_return", "年化收益", 0.05),
            ("sharpe", "夏普比率", 0.10),
            ("max_drawdown", "最大回撤", 0.02),
            ("win_rate", "胜率", 0.05),
            ("calmar_ratio", "卡玛比率", 0.10),
        ]

        print(f"\n{'指标':<16} {'Python':>12} {'C++':>12} {'差值':>10} {'容差':>8}")
        print("-" * 62)

        for key, label, tol in comparisons:
            py_val = py_metrics.get(key, 0)
            cpp_val = s.get(cpp_key_map.get(key, key), 0)
            diff = abs(py_val - cpp_val)
            ok = "✓" if diff < tol else "✗"
            print(f"{label:<16} {py_val:>12.6f} {cpp_val:>12.6f} "
                  f"{diff:>10.6f} {tol:>8.2f} {ok}")
            assert diff < tol, (
                f"{label} 差异过大: Python={py_val:.6f}, C++={cpp_val:.6f}, "
                f"差值={diff:.6f}, 容差={tol}"
            )
