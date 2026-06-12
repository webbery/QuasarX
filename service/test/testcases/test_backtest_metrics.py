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
# Empyrical 对比（黄金标准）
# ============================================================

def compute_empyrical_sharpe(daily_returns: List[float]) -> float:
    """使用 empyrical 计算夏普比率"""
    try:
        import empyrical
        ret = np.array(daily_returns)
        return float(empyrical.sharpe_ratio(ret, risk_free=RISK_FREE_RATE, period='daily'))
    except ImportError:
        pytest.skip("empyrical not installed")


def compute_empyrical_max_drawdown(daily_returns: List[float]) -> float:
    """使用 empyrical 计算最大回撤"""
    try:
        import empyrical
        ret = np.array(daily_returns)
        return float(abs(empyrical.max_drawdown(ret)))
    except ImportError:
        pytest.skip("empyrical not installed")


def compute_empyrical_annual_return(daily_returns: List[float]) -> float:
    """使用 empyrical 计算年化收益"""
    try:
        import empyrical
        ret = np.array(daily_returns)
        return float(empyrical.annual_return(ret, period='daily'))
    except ImportError:
        pytest.skip("empyrical not installed")


def compute_empyrical_annual_volatility(daily_returns: List[float]) -> float:
    """使用 empyrical 计算年化波动率"""
    try:
        import empyrical
        ret = np.array(daily_returns)
        return float(empyrical.annual_volatility(ret, period='daily'))
    except ImportError:
        pytest.skip("empyrical not installed")


def compute_empyrical_calmar(daily_returns: List[float]) -> float:
    """使用 empyrical 计算卡玛比率"""
    try:
        import empyrical
        ret = np.array(daily_returns)
        return float(empyrical.calmar_ratio(ret))
    except ImportError:
        pytest.skip("empyrical not installed")


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
        metrics[key] = float(value)
    
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
# 测试类：Empyrical 对比
# ============================================================

class TestEmpyricalComparison:
    """使用 empyrical 库作为黄金标准对比"""

    def test_sharpe_ratio_consistency(self):
        """验证夏普比率与 empyrical 一致"""
        # 生成随机收益率序列
        np.random.seed(42)
        daily_returns = list(np.random.normal(0.0005, 0.02, 252))
        values = [100000 * (1 + r) for r in daily_returns]
        values.insert(0, 100000)
        
        # 本地计算
        local_sharpe = compute_sharpe_ratio(values, daily_returns)
        
        # Empyrical 计算
        empyrical_sharpe = compute_empyrical_sharpe(daily_returns)
        
        # 对比（允许5%误差）
        if empyrical_sharpe != 0:
            relative_error = abs(local_sharpe - empyrical_sharpe) / abs(empyrical_sharpe)
            assert relative_error < 0.05, (
                f"夏普比率不一致: 本地={local_sharpe:.4f}, "
                f"empyrical={empyrical_sharpe:.4f}, 误差={relative_error:.4f}"
            )

    def test_max_drawdown_consistency(self):
        """验证最大回撤与 empyrical 一致"""
        np.random.seed(42)
        daily_returns = list(np.random.normal(0.0005, 0.02, 252))
        
        # 生成价值序列
        values = [100000]
        for r in daily_returns:
            values.append(values[-1] * (1 + r))
        
        # 本地计算
        local_max_dd = compute_max_drawdown(values)
        
        # Empyrical 计算
        empyrical_max_dd = compute_empyrical_max_drawdown(daily_returns)
        
        # 对比
        relative_error = abs(local_max_dd - empyrical_max_dd) / empyrical_max_dd
        assert relative_error < 0.01, (
            f"最大回撤不一致: 本地={local_max_dd:.4f}, "
            f"empyrical={empyrical_max_dd:.4f}, 误差={relative_error:.4f}"
        )

    def test_annual_return_consistency(self):
        """验证年化收益与 empyrical 一致"""
        np.random.seed(42)
        daily_returns = list(np.random.normal(0.0005, 0.02, 252))
        
        local_ann_ret = compute_annual_return([100000] + [100000 * (1 + r) for r in daily_returns])
        empyrical_an_ret = compute_empyrical_annual_return(daily_returns)
        
        if empyrical_an_ret != 0:
            relative_error = abs(local_ann_ret - empyrical_an_ret) / abs(empyrical_an_ret)
            assert relative_error < 0.05, (
                f"年化收益不一致: 本地={local_ann_ret:.4f}, "
                f"empyrical={empyrical_an_ret:.4f}, 误差={relative_error:.4f}"
            )

    def test_calmar_ratio_consistency(self):
        """验证卡玛比率与 empyrical 一致"""
        np.random.seed(42)
        daily_returns = list(np.random.normal(0.0005, 0.02, 252))
        
        values = [100000]
        for r in daily_returns:
            values.append(values[-1] * (1 + r))
        
        local_max_dd = compute_max_drawdown(values)
        local_calmar = compute_calmar_ratio(values, local_max_dd)
        empyrical_calmar = compute_empyrical_calmar(daily_returns)
        
        if empyrical_calmar != 0 and local_calmar != 0:
            relative_error = abs(local_calmar - empyrical_calmar) / abs(empyrical_calmar)
            assert relative_error < 0.05, (
                f"卡玛比率不一致: 本地={local_calmar:.4f}, "
                f"empyrical={empyrical_calmar:.4f}, 误差={relative_error:.4f}"
            )


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
# 测试类：C++ 与 Python 对比（需要启动服务）
# ============================================================

class TestCPPVsPython:
    """通过 HTTP API 对比 C++ 和 Python 指标计算"""

    @pytest.fixture(autouse=True)
    def setup(self):
        """获取认证 token 和测试用例"""
        self.token = get_auth_token()
        self.cases = load_test_cases_summary()

    def _run_comparison(self, case_id: str, tolerance: float = 0.10):
        """
        运行单个用例的 C++ vs Python 对比
        
        参数:
            case_id: 测试用例 ID
            tolerance: 容差（默认 10%）
        """
        case = self.cases[case_id]
        expected = case["expected"]
        
        # 1. 运行 C++ 回测
        strategy_path = METRIC_TEST_DIR / case["strategy_file"]
        if not strategy_path.exists():
            pytest.skip(f"策略文件不存在: {strategy_path}")
        
        print(f"\n=== {case['name']} ({case_id}) ===")
        cpp_result = run_backtest_via_api(str(strategy_path), self.token)
        cpp_metrics = extract_cpp_metrics(cpp_result)
        
        # 2. 打印对比表
        print(f"\n{'指标':<20} {'C++':>12} {'Python':>12} {'误差':>8}")
        print("-" * 55)
        
        metrics_to_compare = [
            ("total_return", "总收益率"),
            ("annual_return", "年化收益"),
            ("sharpe", "夏普比率"),
            ("max_drawdown", "最大回撤"),
            ("win_rate", "胜率"),
            ("calmar", "卡玛比率"),
            ("r_squared", "R²"),
            ("var_95", "VaR(95%)"),
            ("es", "CVaR(95%)"),
        ]
        
        for metric_key, label in metrics_to_compare:
            cpp_val = cpp_metrics.get(metric_key)
            py_val = expected.get(metric_key)

            if cpp_val is not None and py_val is not None:
                abs_diff = abs(cpp_val - py_val)
                if abs(py_val) > 1e-6:
                    error = abs_diff / abs(py_val)
                    print(f"{label:<20} {cpp_val:>12.6f} {py_val:>12.6f} {error:>7.2%}")
                else:
                    print(f"{label:<20} {cpp_val:>12.6f} {py_val:>12.6f} {'N/A':>8}")

                # 特殊处理：极低波动率时夏普比率不稳定，跳过对比
                if metric_key == "sharpe":
                    py_vol = expected.get("annual_volatility", 0)
                    if py_vol < 0.01:
                        print(f"  → 波动率极低 ({py_vol:.6f})，跳过夏普对比")
                        continue

                # 特殊处理：卡玛比率是派生指标（annual_return / max_dd），误差被放大
                # 当 max_dd 本身很小时，微小差异会导致 calmar 差异较大
                if metric_key == "calmar":
                    # 使用 0.10 容差（其他指标 0.05）
                    calmar_tolerance = 0.10
                    assert abs_diff < calmar_tolerance, (
                        f"{label} 差异过大: C++={cpp_val:.6f}, Python={py_val:.6f}, "
                        f"绝对差异={abs_diff:.6f}, 容差={calmar_tolerance:.4f}"
                    )
                    continue

                # 使用绝对差异断言（对小预期值避免相对误差放大）
                assert abs_diff < tolerance, (
                    f"{label} 差异过大: C++={cpp_val:.6f}, Python={py_val:.6f}, "
                    f"绝对差异={abs_diff:.6f}, 容差={tolerance:.4f}"
                )
            else:
                print(f"{label:<20} {'N/A':>12} {py_val if py_val is not None else 'N/A':>12}")

    def test_up_trend_comparison(self):
        """单边上涨：完整指标对比"""
        self._run_comparison("up_trend", tolerance=0.05)

    def test_down_trend_comparison(self):
        """单边下跌：完整指标对比"""
        self._run_comparison("down_trend", tolerance=0.05)

    def test_rise_fall_comparison(self):
        """先涨后跌：完整指标对比"""
        self._run_comparison("rise_fall", tolerance=0.05)

    def test_sideways_comparison(self):
        """横盘震荡：完整指标对比"""
        self._run_comparison("sideways", tolerance=0.05)

    def test_high_volatility_comparison(self):
        """高波动率：完整指标对比"""
        self._run_comparison("high_volatility", tolerance=0.05)

    def test_steady_trend_comparison(self):
        """稳定趋势：完整指标对比"""
        self._run_comparison("steady_trend", tolerance=0.10)

    def test_existing_strategy_cta(self):
        """使用现有 CTA 策略验证指标"""
        strategy_path = Path(__file__).parent.parent / "script" / "CTA.json"
        
        if not strategy_path.exists():
            pytest.skip(f"CTA.json 不存在: {strategy_path}")
        
        print("\n=== CTA 策略回测指标 ===")
        cpp_result = run_backtest_via_api(str(strategy_path), self.token)
        cpp_metrics = extract_cpp_metrics(cpp_result)
        
        # 打印所有指标
        print(f"\n{'指标':<25} {'值':>12}")
        print("-" * 40)
        for key, value in sorted(cpp_metrics.items()):
            print(f"{key:<25} {value:>12.6f}")
        
        # 验证基本合理性
        assert -1.0 <= cpp_metrics.get("total_return", 0) <= 10.0, "总收益率异常"
        assert 0.0 <= cpp_metrics.get("win_rate", 0) <= 1.0, "胜率异常"
        assert 0.0 <= cpp_metrics.get("max_drawdown", 0) <= 1.0, "最大回撤异常"
