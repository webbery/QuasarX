#!/usr/bin/env python3
"""
波动率分析 API 完整测试（历史回测模式）

验证策略：
1. 波动率指标（年化波动率/最大回撤/VaR/CVaR）→ empyrical 黄金标准对比
2. AR(p) 自回归预测（系数/预测值/残差方差）→ statsmodels 黄金标准对比
3. 数学属性（置信区间包含/对称性/正定性）→ 精确验证
4. API 结构/参数校验 → 基础测试

黄金标准库：
- empyrical: Quantopian 金融指标库（年化波动率/最大回撤/VaR/CVaR）
- statsmodels: 学术级时序分析库（Yule-Walker/AR 预测/置信区间）

使用方法：
  pytest test_volatility_forecast.py -v

前置准备：
  pip install empyrical statsmodels
  python generate_test_data.py  # 生成基础测试数据
  服务已启动
"""

import pytest
import requests
import csv
import numpy as np
from pathlib import Path
from typing import Dict, List, Tuple, Optional

# 黄金标准库
try:
    import empyrical
except ImportError:
    raise ImportError("需要 empyrical 库，请运行: pip install empyrical")

try:
    from statsmodels.tsa.stattools import yule_walker, acf as sm_acf
    from statsmodels.tsa.ar_model import AutoReg
except ImportError:
    raise ImportError("需要 statsmodels 库，请运行: pip install statsmodels")

# 抑制 SSL 警告
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 测试数据目录
SERVICE_DATA_DIR = Path(__file__).parent.parent.parent / "build" / "data"
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"
ORG_DIR = SERVICE_DATA_DIR / "AStock"

# 固定随机种子保证可重复
np.random.seed(42)

# 指标对比容差
VOL_TOLERANCE = 0.05  # 波动率指标 5% 容差（empyrical 与 C++ 计算方法可能有微小差异）
AR_TOLERANCE = 0.01   # AR 系数 1% 容差（纯数学计算，应高度一致）


# ============================================================
# 辅助函数
# ============================================================

def simple_returns(prices: List[float]) -> np.ndarray:
    """简单收益率"""
    return np.array([(prices[i] - prices[i-1]) / prices[i-1] for i in range(1, len(prices))])


def compute_empyrical_metrics(returns: np.ndarray) -> Dict:
    """使用 empyrical 计算波动率指标（黄金标准）"""
    return {
        "annual_volatility": empyrical.annual_volatility(returns, period='daily'),
        "max_drawdown": abs(empyrical.max_drawdown(returns)),
        "cvar": abs(empyrical.conditional_value_at_risk(returns)),
    }


def find_significant_lag_golden(returns: np.ndarray, max_p: int = 10) -> int:
    """使用 statsmodels ACF 找到显著 lag"""
    N = len(returns)
    if N < 30:
        return 0
    acf_values = sm_acf(returns, nlags=max_p, fft=False)
    threshold = 1.96 / np.sqrt(N)
    result = 0
    for k in range(1, len(acf_values)):
        if abs(acf_values[k]) > threshold:
            result = k
    return result


def compute_ar_golden(returns: np.ndarray, p: int) -> Dict:
    """使用 statsmodels Yule-Walker 计算 AR(p)"""
    if p <= 0:
        return {"ar_coeffs": [], "residual_var": 0.0, "sigma": 0.0}
    ar_coeffs, sigma = yule_walker(returns, order=p, method="mle")
    return {
        "ar_coeffs": ar_coeffs.tolist(),
        "residual_var": float(sigma ** 2),
        "sigma": float(sigma)
    }


def compute_forecast_golden(returns: np.ndarray, prices: List[float],
                            max_p: int = 10) -> Optional[Dict]:
    """使用 statsmodels AutoReg 做完整预测"""
    p = find_significant_lag_golden(returns, max_p)
    if p == 0:
        return None

    model = AutoReg(returns, lags=p, old_names=False)
    fit_result = model.fit()
    forecast = fit_result.forecast(steps=p)
    forecast_mean = forecast.mean.values
    forecast_se = forecast.se_mean.values

    last_price = prices[-1]
    return {
        "order_p": p,
        "ar_coeffs": fit_result.params[1:].tolist(),
        "residual_var": float(fit_result.sigma2),
        "forecast_values": forecast_mean.tolist(),
        "forecast_std": forecast_se.tolist(),
        "forecast_upper_1sigma": (last_price + forecast_mean + forecast_se).tolist(),
        "forecast_lower_1sigma": (last_price + forecast_mean - forecast_se).tolist(),
        "forecast_upper_2sigma": (last_price + forecast_mean + 2 * forecast_se).tolist(),
        "forecast_lower_2sigma": (last_price + forecast_mean - 2 * forecast_se).tolist(),
    }


def to_api_symbol(symbol: str) -> str:
    """sz.900001 → 900001.SZ（API 期望的格式）"""
    if '.' in symbol:
        parts = symbol.split('.', 1)
        exchange_map = {'sz': 'SZ', 'sh': 'SH', 'bj': 'BJ'}
        exc = exchange_map.get(parts[0].lower(), parts[0].upper())
        return f"{parts[1]}.{exc}"
    return symbol


def from_api_symbol(symbol: str) -> str:
    """900001.SZ → sz.900001（测试使用的格式）"""
    if '.' in symbol:
        parts = symbol.rsplit('.', 1)
        exchange_map = {'SZ': 'sz', 'SH': 'sh', 'BJ': 'bj'}
        exc = exchange_map.get(parts[1], parts[1].lower())
        return f"{exc}.{parts[0]}"
    return symbol


def call_volatility_api(symbol: str, start_date: str, end_date: str,
                        windows: str = "20", auth_token: str = None) -> Dict:
    """调用波动率分析 API"""
    kwargs = {'verify': VERIFY_SSL}
    if auth_token and len(auth_token) > 10:
        kwargs['headers'] = {'Authorization': auth_token}
    # 转换 symbol 格式：sz.900001 → 900001.SZ
    api_symbols = ','.join(to_api_symbol(s.strip()) for s in symbol.split(','))
    resp = requests.get(
        f"{BASE_URL}/analysis/volatility",
        params={"symbols": api_symbols, "start_date": start_date, "end_date": end_date, "windows": windows},
        **kwargs
    )
    assert resp.status_code == 200, f"API 请求失败: {resp.status_code} - {resp.text}"
    data = resp.json()
    # 将响应中的 symbol key 转回原始格式
    if "single" in data and isinstance(data["single"], dict):
        data["single"] = {from_api_symbol(k): v for k, v in data["single"].items()}
    if "symbols" in data and isinstance(data["symbols"], list):
        data["symbols"] = [from_api_symbol(s) for s in data["symbols"]]
    return data


def load_csv_prices(symbol: str) -> Tuple[List[float], List[str]]:
    """从 CSV 加载收盘价和日期"""
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        csv_path = ORG_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        pytest.skip(f"测试数据不存在: {csv_path}")
    closes, dates = [], []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            closes.append(float(row['close']))
            dates.append(row['datetime'])
    return closes, dates


# ============================================================
# 测试类 1：完整波动率分析 API 基础测试
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestVolatilityAPI:
    """波动率分析 API 基础测试"""

    def test_single_symbol_fields(self, auth_token):
        """单标的应返回所有必要字段"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        assert "symbols" in resp
        assert "single" in resp
        assert symbol in resp["single"]
        single = resp["single"][symbol]

        required = ["prices", "returns", "annual_volatility", "max_drawdown",
                    "skewness", "kurtosis", "var_95", "cvar_95", "rolling_vol",
                    "returns_acf", "abs_returns_acf"]
        for field in required:
            assert field in single, f"缺少字段: {field}"

    def test_multiple_symbols_correlation(self, auth_token):
        """多标的应返回相关系数矩阵"""
        closes, dates = load_csv_prices("sz.900001")
        resp = call_volatility_api("sz.900001,sz.900002", dates[0], dates[-1], auth_token=auth_token)

        assert "multi" in resp
        multi = resp["multi"]
        assert "correlation_matrix" in multi
        assert len(multi["correlation_matrix"]) == 2
        assert abs(multi["correlation_matrix"][0][0] - 1.0) < 0.01

    def test_missing_symbols_400(self, auth_token):
        """缺少标的参数应返回 400"""
        kwargs = {'verify': VERIFY_SSL}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        resp = requests.get(f"{BASE_URL}/analysis/volatility", params={"start_date": "2023-01-01"}, **kwargs)
        assert resp.status_code == 400

    def test_nonexistent_symbol_empty(self, auth_token):
        """不存在的标的数据应返回空结果"""
        closes, dates = load_csv_prices("sz.900001")
        resp = call_volatility_api("sz.999999", dates[0], dates[-1], auth_token=auth_token)
        # call_volatility_api 已断言 200，这里直接检查数据
        single = resp.get("single")
        assert single is None or (isinstance(single, dict) and "sz.999999" not in single)


# ============================================================
# 测试类 2：波动率指标 vs empyrical 黄金标准
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestVolatilityEmpyrical:
    """波动率指标 vs empyrical 黄金标准对比"""

    def test_annual_volatility_up_trend(self, auth_token):
        """验证单边上涨用例的年化波动率与 empyrical 一致"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        returns = simple_returns(closes)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_vol = resp["single"][symbol]["annual_volatility"]
        emp_vol = empyrical.annual_volatility(returns, period='daily')

        if emp_vol > 0:
            rel_err = abs(cpp_vol - emp_vol) / emp_vol
            assert rel_err < VOL_TOLERANCE, \
                f"年化波动率差异: C++={cpp_vol:.6f}, empyrical={emp_vol:.6f}, 误差={rel_err:.4f}"

    def test_annual_volatility_high_vol(self, auth_token):
        """验证高波动用例的年化波动率"""
        symbol = "sz.900005"
        closes, dates = load_csv_prices(symbol)
        returns = simple_returns(closes)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_vol = resp["single"][symbol]["annual_volatility"]
        emp_vol = empyrical.annual_volatility(returns, period='daily')

        if emp_vol > 0:
            rel_err = abs(cpp_vol - emp_vol) / emp_vol
            assert rel_err < VOL_TOLERANCE, \
                f"年化波动率差异: C++={cpp_vol:.6f}, empyrical={emp_vol:.6f}, 误差={rel_err:.4f}"

    def test_max_drawdown_up_trend(self, auth_token):
        """验证最大回撤与 empyrical 一致"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_dd = resp["single"][symbol]["max_drawdown"]
        emp_dd = abs(empyrical.max_drawdown(simple_returns(closes)))

        assert abs(cpp_dd - emp_dd) < 0.01, \
            f"最大回撤差异: C++={cpp_dd:.6f}, empyrical={emp_dd:.6f}"

    def test_max_drawdown_down_trend(self, auth_token):
        """验证单边下跌用例的最大回撤"""
        symbol = "sz.900002"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_dd = resp["single"][symbol]["max_drawdown"]
        emp_dd = abs(empyrical.max_drawdown(simple_returns(closes)))

        assert abs(cpp_dd - emp_dd) < 0.01, \
            f"最大回撤差异: C++={cpp_dd:.6f}, empyrical={emp_dd:.6f}"

    def test_cvar_vs_empyrical(self, auth_token):
        """验证 CVaR 与 empyrical 一致"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        returns = simple_returns(closes)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_cvar = resp["single"][symbol]["cvar_95"]
        emp_cvar = abs(empyrical.conditional_value_at_risk(returns))

        # CVaR 方向：C++ 返回正值（亏损），empyrical 返回负值
        if emp_cvar > 0 and cpp_cvar > 0:
            rel_err = abs(cpp_cvar - emp_cvar) / emp_cvar
            assert rel_err < VOL_TOLERANCE, \
                f"CVaR 差异: C++={cpp_cvar:.6f}, empyrical={emp_cvar:.6f}, 误差={rel_err:.4f}"


# ============================================================
# 测试类 3：波动率数学属性
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestVolatilityProperties:
    """波动率数学属性验证"""

    def test_rolling_vol_decreases_with_window(self, auth_token):
        """滚动波动率随窗口增大而减小"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        resp = call_volatility_api(symbol, dates[0], dates[-1], "20,60,120", auth_token=auth_token)

        rolling = resp["single"][symbol]["rolling_vol"]
        if "20" in rolling and "60" in rolling:
            vol_20 = np.mean(rolling["20"])
            vol_60_vals = [v for v in rolling["60"] if not np.isnan(v)]
            if vol_60_vals:
                vol_60 = np.mean(vol_60_vals)
                assert vol_20 >= vol_60 * 0.9, \
                    f"短期波动率({vol_20:.4f})应 >= 长期波动率({vol_60:.4f})"

    def test_cvar_gte_var(self, auth_token):
        """CVaR 应 >= VaR（尾部平均亏损 >= 分位数亏损）"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)

        var_95 = resp["single"][symbol]["var_95"]
        cvar_95 = resp["single"][symbol]["cvar_95"]

        if var_95 > 0 and cvar_95 > 0:
            assert cvar_95 >= var_95 * 0.95, \
                f"CVaR({cvar_95:.6f}) 应 >= VaR({var_95:.6f})"

    def test_correlation_symmetric(self, auth_token):
        """相关系数矩阵应对称"""
        closes, dates = load_csv_prices("sz.900001")
        resp = call_volatility_api("sz.900001,sz.900002,sz.900003", dates[0], dates[-1], auth_token=auth_token)

        corr = resp["multi"]["correlation_matrix"]
        n = len(corr)
        for i in range(n):
            for j in range(i + 1, n):
                assert abs(corr[i][j] - corr[j][i]) < 1e-6, \
                    f"相关系数不对称: [{i}][{j}]={corr[i][j]}, [{j}][{i}]={corr[j][i]}"


# ============================================================
# 测试类 4：AR(p) 预测 vs statsmodels 黄金标准
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestARForecastGoldenStandard:
    """AR(p) 预测测试：C++ vs statsmodels 黄金标准"""

    def test_ar_coefficients_up_trend(self, auth_token):
        """验证 AR 系数与 statsmodels 一致"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        returns = simple_returns(closes)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        p = find_significant_lag_golden(returns)
        if p == 0 or not cpp_fc["has_autocorrelation"]:
            pytest.skip("无显著自相关")

        py_ar = compute_ar_golden(returns, p)

        assert cpp_fc["order_p"] == p, \
            f"AR 阶数: C++={cpp_fc['order_p']}, Python={p}"

        for i, (c, py) in enumerate(zip(cpp_fc["ar_coeffs"], py_ar["ar_coeffs"])):
            if abs(py) > 1e-6:
                rel_err = abs(c - py) / abs(py)
                assert rel_err < AR_TOLERANCE, \
                    f"AR 系数[{i}]: C++={c:.6f}, statsmodels={py:.6f}, 误差={rel_err:.4f}"

    def test_ar_residual_variance(self, auth_token):
        """验证残差方差与 statsmodels 一致"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        returns = simple_returns(closes)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        p = find_significant_lag_golden(returns)
        if p == 0 or not cpp_fc["has_autocorrelation"]:
            pytest.skip("无显著自相关")

        py_ar = compute_ar_golden(returns, p)

        if py_ar["residual_var"] > 1e-10:
            rel_err = abs(cpp_fc["residual_var"] - py_ar["residual_var"]) / py_ar["residual_var"]
            assert rel_err < AR_TOLERANCE, \
                f"残差方差: C++={cpp_fc['residual_var']:.10f}, statsmodels={py_ar['residual_var']:.10f}"

    def test_forecast_values(self, auth_token):
        """验证预测值与 statsmodels AutoReg 一致"""
        symbol = "sz.900005"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        py_fc = compute_forecast_golden(simple_returns(closes), closes)
        if py_fc is None or not cpp_fc["has_autocorrelation"]:
            pytest.skip("无显著自相关")

        for i, (c, py) in enumerate(zip(cpp_fc["forecast_values"], py_fc["forecast_values"])):
            if abs(py) > 1e-6:
                rel_err = abs(c - py) / abs(py)
                assert rel_err < AR_TOLERANCE, \
                    f"预测值[{i}]: C++={c:.8f}, statsmodels={py:.8f}"

    def test_forecast_std_accuracy(self, auth_token):
        """验证预测标准差与 statsmodels AutoReg 一致"""
        symbol = "sz.900005"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        py_fc = compute_forecast_golden(simple_returns(closes), closes)
        if py_fc is None or not cpp_fc["has_autocorrelation"]:
            pytest.skip("无显著自相关")

        for i, (c, py) in enumerate(zip(cpp_fc["forecast_std"], py_fc["forecast_std"])):
            if py > 1e-6:
                rel_err = abs(c - py) / py
                assert rel_err < AR_TOLERANCE, \
                    f"预测标准差[{i}]: C++={c:.8f}, statsmodels={py:.8f}"

    def test_forecast_confidence_intervals(self, auth_token):
        """验证置信区间：2σ ⊇ 1σ"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        if cpp_fc["has_autocorrelation"] and len(cpp_fc["forecast_values"]) > 0:
            last = len(cpp_fc["forecast_values"]) - 1
            assert cpp_fc["forecast_upper_2sigma"][last] >= cpp_fc["forecast_upper_1sigma"][last]
            assert cpp_fc["forecast_lower_2sigma"][last] <= cpp_fc["forecast_lower_1sigma"][last]

    def test_forecast_std_increases(self, auth_token):
        """验证预测标准差随步数递增"""
        symbol = "sz.900005"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        if cpp_fc["has_autocorrelation"] and len(cpp_fc["forecast_std"]) >= 2:
            assert cpp_fc["forecast_std"][-1] >= cpp_fc["forecast_std"][0], \
                f"标准差递增: {cpp_fc['forecast_std']}"

    def test_forecast_steps_max_10(self, auth_token):
        """验证预测步数 ≤ 10"""
        symbol = "sz.900006"
        closes, dates = load_csv_prices(symbol)

        resp = call_volatility_api(symbol, dates[0], dates[-1], auth_token=auth_token)
        cpp_fc = resp["single"][symbol]["forecast_returns"]

        if cpp_fc["has_autocorrelation"]:
            assert len(cpp_fc["forecast_values"]) <= 10


# ============================================================
# 测试类 5：多资产协方差外推
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestMultiAssetForecast:
    """多资产预测外推测试"""

    def _get_multi_forecast(self, symbols_str: str, auth_token: str) -> Dict:
        symbols = symbols_str.split(',')
        if not symbols:
            return {}
        closes, dates = load_csv_prices(symbols[0])
        resp = call_volatility_api(symbols_str, dates[0], dates[-1], auth_token=auth_token)
        return resp.get("multi", {}).get("multi_forecast", {})

    def test_multi_forecast_exists(self, auth_token):
        """多标的应返回 multi_forecast"""
        mf = self._get_multi_forecast("sz.900001,sz.900002", auth_token)
        for field in ["horizon", "symbols", "forecast_cov", "forecast_corr", "forecast_volatilities"]:
            assert field in mf, f"缺少字段: {field}"

    def test_multi_forecast_cov_dimensions(self, auth_token):
        """协方差矩阵维度正确"""
        mf = self._get_multi_forecast("sz.900001,sz.900002", auth_token)
        if "forecast_cov" not in mf:
            pytest.skip("无 multi_forecast 数据")
        n = len(mf["symbols"])
        assert len(mf["forecast_cov"]) == n
        for i in range(n):
            assert len(mf["forecast_cov"][i]) == n

    def test_multi_forecast_corr_symmetric(self, auth_token):
        """外推相关系数矩阵对称"""
        mf = self._get_multi_forecast("sz.900001,sz.900002,sz.900003", auth_token)
        if "forecast_corr" not in mf:
            pytest.skip("无 multi_forecast 数据")
        corr = mf["forecast_corr"]
        n = len(corr)
        for i in range(n):
            for j in range(i + 1, n):
                assert abs(corr[i][j] - corr[j][i]) < 1e-6

    def test_multi_forecast_corr_diagonal_one(self, auth_token):
        """外推相关系数对角线为 1"""
        mf = self._get_multi_forecast("sz.900001,sz.900002", auth_token)
        if "forecast_corr" not in mf:
            pytest.skip("无 multi_forecast 数据")
        for i in range(len(mf["forecast_corr"])):
            assert abs(mf["forecast_corr"][i][i] - 1.0) < 0.01

    def test_multi_forecast_cov_positive_diagonal(self, auth_token):
        """外推协方差对角线 > 0"""
        mf = self._get_multi_forecast("sz.900001,sz.900002", auth_token)
        if "forecast_cov" not in mf:
            pytest.skip("无 multi_forecast 数据")
        for i in range(len(mf["forecast_cov"])):
            assert mf["forecast_cov"][i][i] > 0

    def test_multi_forecast_volatilities_count(self, auth_token):
        """外推波动率数量 = 资产数"""
        mf = self._get_multi_forecast("sz.900001,sz.900002,sz.900003", auth_token)
        if "forecast_volatilities" not in mf:
            pytest.skip("无 multi_forecast 数据")
        assert len(mf["forecast_volatilities"]) == len(mf["symbols"])


# ============================================================
# 测试类 6：滚动波动率预测
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestRollingVolForecast:
    """滚动波动率 AR(p) 预测测试"""

    def test_forecast_vol_exists(self, auth_token):
        """forecast_vol 字段存在"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        resp = call_volatility_api(symbol, dates[0], dates[-1], "20", auth_token=auth_token)
        fc = resp["single"][symbol]["forecast_vol"]
        assert fc["source_series"] == "rolling_vol"

    def test_forecast_vol_fields_complete(self, auth_token):
        """forecast_vol 字段完整"""
        symbol = "sz.900001"
        closes, dates = load_csv_prices(symbol)
        resp = call_volatility_api(symbol, dates[0], dates[-1], "20", auth_token=auth_token)
        fc = resp["single"][symbol]["forecast_vol"]
        required = ["source_series", "order_p", "ar_coeffs", "residual_var",
                    "forecast_values", "forecast_upper_1sigma", "forecast_lower_1sigma",
                    "forecast_upper_2sigma", "forecast_lower_2sigma",
                    "forecast_std", "has_autocorrelation", "note"]
        for field in required:
            assert field in fc, f"缺少字段: {field}"


# ============================================================
# 测试类 7：相关系数矩阵 vs numpy.corrcoef 黄金标准
# ============================================================

CORR_TOLERANCE = 1e-6  # 纯数学计算，应精确一致


def compute_corr_golden(symbols: List[str]) -> Tuple[np.ndarray, List[str]]:
    """使用 numpy.corrcoef 计算相关系数矩阵（黄金标准）

    Returns:
        (correlation_matrix, symbol_list) — symbol_list 与矩阵索引对应
    """
    all_returns = []
    valid_symbols = []
    for sym in symbols:
        closes, _ = load_csv_prices(sym)
        if len(closes) < 2:
            continue
        rets = simple_returns(closes)
        all_returns.append(rets)
        valid_symbols.append(sym)

    # 对齐到共同长度（取最短，取尾部 — 与 C++ computeMulti 一致）
    min_len = min(len(r) for r in all_returns)
    aligned = np.array([r[-min_len:] for r in all_returns])

    # numpy.corrcoef 是标准 Pearson 相关系数（去均值）
    corr = np.corrcoef(aligned)
    return corr, valid_symbols


@pytest.mark.usefixtures("auth_token")
class TestCorrelationGoldenStandard:
    """相关系数矩阵 vs numpy.corrcoef 黄金标准对比"""

    def test_correlation_2symbols_vs_numpy(self, auth_token):
        """2 标的相关系数 vs numpy.corrcoef"""
        symbols = ["sz.900001", "sz.900002"]
        golden_corr, _ = compute_corr_golden(symbols)

        api_symbols_str = ','.join(symbols)
        closes, dates = load_csv_prices(symbols[0])
        resp = call_volatility_api(api_symbols_str, dates[0], dates[-1], auth_token=auth_token)

        api_corr = np.array(resp["multi"]["correlation_matrix"])
        assert api_corr.shape == golden_corr.shape

        for i in range(api_corr.shape[0]):
            for j in range(api_corr.shape[1]):
                assert abs(api_corr[i][j] - golden_corr[i][j]) < CORR_TOLERANCE, \
                    f"corr[{i}][{j}]: API={api_corr[i][j]:.8f}, numpy={golden_corr[i][j]:.8f}, " \
                    f"diff={abs(api_corr[i][j] - golden_corr[i][j]):.2e}"

    def test_correlation_3symbols_vs_numpy(self, auth_token):
        """3 标的相关系数 vs numpy.corrcoef"""
        symbols = ["sz.900001", "sz.900002", "sz.900003"]
        golden_corr, _ = compute_corr_golden(symbols)

        api_symbols_str = ','.join(symbols)
        closes, dates = load_csv_prices(symbols[0])
        resp = call_volatility_api(api_symbols_str, dates[0], dates[-1], auth_token=auth_token)

        api_corr = np.array(resp["multi"]["correlation_matrix"])
        assert api_corr.shape == golden_corr.shape

        for i in range(api_corr.shape[0]):
            for j in range(api_corr.shape[1]):
                assert abs(api_corr[i][j] - golden_corr[i][j]) < CORR_TOLERANCE, \
                    f"corr[{i}][{j}]: API={api_corr[i][j]:.8f}, numpy={golden_corr[i][j]:.8f}, " \
                    f"diff={abs(api_corr[i][j] - golden_corr[i][j]):.2e}"

    def test_correlation_5symbols_vs_numpy(self, auth_token):
        """5 标的相关系数 vs numpy.corrcoef（更多标的放大排序问题）"""
        symbols = ["sz.900003", "sz.900001", "sz.900005", "sz.900002", "sz.900004"]
        golden_corr, valid_syms = compute_corr_golden(symbols)

        api_symbols_str = ','.join(symbols)
        closes, dates = load_csv_prices(symbols[0])
        resp = call_volatility_api(api_symbols_str, dates[0], dates[-1], auth_token=auth_token)

        # 验证 API 返回的 symbols 顺序与请求一致
        api_sym_list = resp["symbols"]
        assert api_sym_list == valid_syms, \
            f"symbols 顺序不一致: API={api_sym_list}, expected={valid_syms}"

        api_corr = np.array(resp["multi"]["correlation_matrix"])
        assert api_corr.shape == golden_corr.shape

        for i in range(api_corr.shape[0]):
            for j in range(api_corr.shape[1]):
                assert abs(api_corr[i][j] - golden_corr[i][j]) < CORR_TOLERANCE, \
                    f"corr[{i}][{j}] ({api_sym_list[i]} vs {api_sym_list[j]}): " \
                    f"API={api_corr[i][j]:.8f}, numpy={golden_corr[i][j]:.8f}, " \
                    f"diff={abs(api_corr[i][j] - golden_corr[i][j]):.2e}"

    def test_correlation_matrix_matches_symbols_order(self, auth_token):
        """验证 correlation_matrix[i][j] 对应 symbols[i] 和 symbols[j]

        方法：交换两个标的的请求顺序，检查矩阵是否相应交换。
        如果矩阵索引与 symbols 不匹配，交换后值会对不上。
        """
        closes, dates = load_csv_prices("sz.900001")
        date_range = (dates[0], dates[-1])

        # 请求顺序 A: [900001, 900002, 900003]
        resp_a = call_volatility_api("sz.900001,sz.900002,sz.900003",
                                     *date_range, auth_token=auth_token)
        corr_a = np.array(resp_a["multi"]["correlation_matrix"])

        # 请求顺序 B: [900003, 900001, 900002] — 循环移位
        resp_b = call_volatility_api("sz.900003,sz.900001,sz.900002",
                                     *date_range, auth_token=auth_token)
        corr_b = np.array(resp_b["multi"]["correlation_matrix"])

        # corr_a[0][1] = corr(900001, 900002)
        # corr_b[1][2] = corr(900001, 900002) — 在 B 中 900001 是 index 1, 900002 是 index 2
        assert abs(corr_a[0][1] - corr_b[1][2]) < CORR_TOLERANCE, \
            f"矩阵索引与 symbols 不匹配: " \
            f"corr_a[0][1](900001,900002)={corr_a[0][1]:.8f}, " \
            f"corr_b[1][2](900001,900002)={corr_b[1][2]:.8f}"

        # corr_a[1][2] = corr(900002, 900003)
        # corr_b[2][0] = corr(900002, 900003) — 在 B 中 900002 是 index 2, 900003 是 index 0
        assert abs(corr_a[1][2] - corr_b[2][0]) < CORR_TOLERANCE, \
            f"矩阵索引与 symbols 不匹配: " \
            f"corr_a[1][2](900002,900003)={corr_a[1][2]:.8f}, " \
            f"corr_b[2][0](900002,900003)={corr_b[2][0]:.8f}"

        # corr_a[0][2] = corr(900001, 900003)
        # corr_b[1][0] = corr(900001, 900003) — 在 B 中 900001 是 index 1, 900003 是 index 0
        assert abs(corr_a[0][2] - corr_b[1][0]) < CORR_TOLERANCE, \
            f"矩阵索引与 symbols 不匹配: " \
            f"corr_a[0][2](900001,900003)={corr_a[0][2]:.8f}, " \
            f"corr_b[1][0](900001,900003)={corr_b[1][0]:.8f}"
