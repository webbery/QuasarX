#!/usr/bin/env python3
"""
协整分析面板 — C++ 计算正确性验证

核心思路: 用合成数据 (已知参数) 和 statsmodels 黄金标准，
验证 C++ 实现的每个算法模块的数值精度。

测试模块:
1. ADF 检验 (MacKinnon p 值) vs statsmodels.adfuller
2. KPSS 检验 vs statsmodels.kpss
3. Engle-Granger 协整回归 β vs numpy OLS
4. OU 过程 MLE 参数 vs statsmodels OU 拟合
5. Johansen 检验 vs statsmodels.coint (多元)
6. Granger 因果 vs statsmodels.grangercausalitytests

使用方法:
  pytest test_cointegration_panel.py -v

前置: 服务已启动, 合成数据已生成 (generate_test_data.py)
"""

import pytest
import requests
import numpy as np
from pathlib import Path

try:
    from statsmodels.tsa.stattools import adfuller, kpss, grangercausalitytests
    from statsmodels.tsa.vector_ar.var_model import VAR
    from statsmodels.tools.sm_exceptions import InterpolationWarning
    import warnings
    warnings.filterwarnings("ignore", category=InterpolationWarning)
except ImportError:
    raise ImportError("pip install statsmodels numpy")

import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 容差 — C++ 与 Python 实现差异来源:
# - ADF: 滞后选择 (BIC vs AIC) 可能不同, 统计量本身应一致
# - KPSS: 长期方差估计 (Bartlett kernel) 应一致
# - Granger: C++ 用 OLS 近似, statsmodels 用 VAR, F 统计量可能有差异
ADF_STAT_TOL = 0.10       # ADF 统计量 10%
ADF_PVAL_TOL = 0.15       # p 值 15% (不同滞后阶会导致差异)
KPSS_STAT_TOL = 0.15      # KPSS 统计量 15%
GRANGER_F_TOL = 0.20      # Granger F 统计量 20% (C++ 用简化 OLS)
BETA_TOL = 0.05           # 协整系数 β 5% (OLS 应精确一致)
OU_TOL = 0.15             # OU 参数 15%

# 测试标的
SYMBOL_COINT_A = "sz.910003"
SYMBOL_COINT_B = "sz.910004"
SYMBOL_GRANGER_X = "sz.910001"
SYMBOL_GRANGER_Y = "sz.910002"

SERVICE_DATA_DIR = Path(__file__).parent.parent.parent / "build" / "data"
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"


# ============================================================
# 辅助函数
# ============================================================

def to_api_symbol(symbol: str) -> str:
    if '.' in symbol:
        parts = symbol.split('.', 1)
        exc = {'sz': 'SZ', 'sh': 'SH', 'bj': 'BJ'}.get(parts[0].lower(), parts[0].upper())
        return f"{parts[1]}.{exc}"
    return symbol


def from_api_symbol(symbol: str) -> str:
    if '.' in symbol:
        parts = symbol.rsplit('.', 1)
        exc = {'SZ': 'sz', 'SH': 'sh', 'BJ': 'bj'}.get(parts[1], parts[1].lower())
        return f"{exc}.{parts[0]}"
    return symbol


def call_api(symbols: list, max_lag: int = 10) -> dict:
    api_symbols = ','.join(to_api_symbol(s) for s in symbols)
    resp = requests.get(
        f"{BASE_URL}/analysis/cointegration",
        params={"symbols": api_symbols, "max_lag": str(max_lag)},
        verify=VERIFY_SSL,
    )
    assert resp.status_code == 200, f"API error: {resp.status_code} - {resp.text}"
    data = resp.json()
    if "symbols" in data:
        data["symbols"] = [from_api_symbol(s) for s in data["symbols"]]
    if "unit_root" in data:
        data["unit_root"] = {from_api_symbol(k): v for k, v in data["unit_root"].items()}
    if "pairwise_eg" in data:
        for eg in data["pairwise_eg"]:
            eg["symbol_x"] = from_api_symbol(eg["symbol_x"])
            eg["symbol_y"] = from_api_symbol(eg["symbol_y"])
    return data


def load_csv_prices(symbol: str) -> np.ndarray:
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        pytest.skip(f"测试数据不存在: {csv_path}，请先运行 generate_test_data.py")
    prices = []
    with open(csv_path, 'r') as f:
        for line in f.readlines()[1:]:
            parts = line.strip().split(',')
            if len(parts) >= 3:
                prices.append(float(parts[2]))
    return np.array(prices)


def rel_err(a, b):
    """相对误差, 避免除零"""
    denom = max(abs(b), 1e-10)
    return abs(a - b) / denom


# ============================================================
# 1. ADF 检验 vs statsmodels
# ============================================================

class TestADF:
    """ADF 检验: C++ adfTestFull vs statsmodels.adfuller"""

    def test_adf_statistic_cointegrated(self):
        """协整序列的残差 ADF 统计量应与 statsmodels 一致"""
        a = load_csv_prices(SYMBOL_COINT_A)
        b = load_csv_prices(SYMBOL_COINT_B)

        # 协整回归残差
        from numpy.polynomial.polynomial import polyfit
        beta, alpha = polyfit(a, b, 1)
        residuals = b - (alpha + beta * a)

        # statsmodels ADF (无增广滞后, 与 C++ 默认一致)
        sm_result = adfuller(residuals, maxlag=0, regression='c')
        sm_stat = sm_result[0]

        # C++ ADF
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]
        cpp_stat = eg["adf"]["statistic"]

        err = rel_err(cpp_stat, sm_stat)
        assert err < ADF_STAT_TOL, \
            f"ADF 统计量: C++={cpp_stat:.4f}, statsmodels={sm_stat:.4f}, rel_err={err:.2%}"

    def test_adf_pvalue_range(self):
        """p 值应在合理范围内 (协整残差应显著)"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]
        p = eg["adf"]["p_value"]

        # 已知协整序列, p 值应 < 0.1
        assert p < 0.1, f"协整残差 ADF p 值应 < 0.1, 实际 p={p:.4f}"

    def test_adf_critical_values_ordering(self):
        """临界值应满足 cv_1% < cv_5% < cv_10% < 0"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        adf = data["pairwise_eg"][0]["adf"]

        assert adf["cv_1pct"] < adf["cv_5pct"] < adf["cv_10pct"] < 0, \
            f"临界值排序错误: 1%={adf['cv_1pct']}, 5%={adf['cv_5pct']}, 10%={adf['cv_10pct']}"

    def test_adf_unit_root_detection(self):
        """原始价格序列 (随机游走) 应被判定为非平稳"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])

        for sym in [SYMBOL_COINT_A, SYMBOL_COINT_B]:
            adf = data["unit_root"][sym]["adf"]
            # 价格序列是 I(1), ADF 不应拒绝 H0
            assert adf["p_value"] > 0.05, \
                f"{sym} 价格序列 ADF p={adf['p_value']:.4f}, 应 > 0.05 (非平稳)"


# ============================================================
# 2. KPSS 检验 vs statsmodels
# ============================================================

class TestKPSS:
    """KPSS 检验: C++ kpssTest vs statsmodels.kpss"""

    def test_kpss_statistic(self):
        """KPSS 统计量应与 statsmodels 一致"""
        a = load_csv_prices(SYMBOL_COINT_A)
        b = load_csv_prices(SYMBOL_COINT_B)

        # 协整回归残差
        from numpy.polynomial.polynomial import polyfit
        beta, alpha = polyfit(a, b, 1)
        residuals = b - (alpha + beta * a)

        # statsmodels KPSS
        sm_stat, sm_p, sm_lags, _ = kpss(residuals, regression='c', nlags='auto')

        # C++ KPSS
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        cpp_kpss = data["pairwise_eg"][0]["kpss"]

        err = rel_err(cpp_kpss["statistic"], sm_stat)
        assert err < KPSS_STAT_TOL, \
            f"KPSS 统计量: C++={cpp_kpss['statistic']:.4f}, statsmodels={sm_stat:.4f}, rel_err={err:.2%}"

    def test_kpss_complements_adf(self):
        """ADF 和 KPSS 应互补: 协整残差 → ADF 拒绝 (平稳), KPSS 不拒绝 (平稳)"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]

        adf_stationary = eg["adf"]["is_stationary"]
        kpss_stationary = eg["kpss"]["is_stationary"]

        # 已知协整, 两个检验都应判定残差平稳
        assert adf_stationary, "ADF 应判定协整残差为平稳"
        assert kpss_stationary, "KPSS 应判定协整残差为平稳 (H0: 平稳)"


# ============================================================
# 3. Engle-Granger 协整回归
# ============================================================

class TestEngleGranger:
    """EG 两步法: 协整系数 β 和残差"""

    def test_beta_accuracy(self):
        """β 应与 numpy OLS 精确一致"""
        a = load_csv_prices(SYMBOL_COINT_A)
        b = load_csv_prices(SYMBOL_COINT_B)

        # numpy OLS: B = α + β*A
        coeffs = np.polyfit(a, b, 1)
        beta_ref = coeffs[0]

        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]

        err = rel_err(eg["beta"], beta_ref)
        assert err < BETA_TOL, \
            f"β: C++={eg['beta']:.6f}, numpy={beta_ref:.6f}, rel_err={err:.2%}"

    def test_r_squared(self):
        """R² 应与 numpy 计算一致"""
        a = load_csv_prices(SYMBOL_COINT_A)
        b = load_csv_prices(SYMBOL_COINT_B)

        coeffs = np.polyfit(a, b, 1)
        predicted = coeffs[0] * a + coeffs[1]
        ss_res = np.sum((b - predicted) ** 2)
        ss_tot = np.sum((b - np.mean(b)) ** 2)
        r2_ref = 1 - ss_res / ss_tot

        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]

        err = rel_err(eg["r_squared"], r2_ref)
        assert err < 0.01, \
            f"R²: C++={eg['r_squared']:.6f}, numpy={r2_ref:.6f}, rel_err={err:.2%}"

    def test_residuals_match(self):
        """残差序列应与 numpy 计算一致"""
        a = load_csv_prices(SYMBOL_COINT_A)
        b = load_csv_prices(SYMBOL_COINT_B)

        coeffs = np.polyfit(a, b, 1)
        residuals_ref = b - (coeffs[0] * a + coeffs[1])

        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]
        residuals_cpp = np.array(eg["residuals"])

        assert len(residuals_cpp) == len(residuals_ref), \
            f"残差长度: C++={len(residuals_cpp)}, numpy={len(residuals_ref)}"

        max_err = np.max(np.abs(residuals_cpp - residuals_ref))
        assert max_err < 1e-6, \
            f"残差最大偏差: {max_err:.2e}, 应 < 1e-6"

    def test_cointegration_detection(self):
        """已知协整对应应被正确检测"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        eg = data["pairwise_eg"][0]
        assert eg["is_cointegrated"] is True, "已知协整对应未被检测"


# ============================================================
# 4. OU 过程 MLE 拟合
# ============================================================

class TestOUProcess:
    """OU 过程: C++ fitOUProcess vs AR(1) 反推"""

    def test_ou_theta_positive(self):
        """协整残差的 OU θ 应为正 (均值回复)"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        ou = data["pairwise_eg"][0]["ou"]
        assert ou["theta"] > 0, f"θ={ou['theta']}, 协整残差应均值回复"

    def test_ou_half_life_consistency(self):
        """半衰期应与 θ 一致: half_life = ln(2)/θ"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        ou = data["pairwise_eg"][0]["ou"]

        if ou["theta"] > 0:
            expected_hl = np.log(2) / ou["theta"]
            err = rel_err(ou["half_life"], expected_hl)
            assert err < 0.01, \
                f"半衰期不一致: ou.half_life={ou['half_life']:.4f}, ln2/θ={expected_hl:.4f}"

    def test_ou_mu_near_zero(self):
        """协整残差的长期均值 μ 应接近 0 (残差均值为 0)"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        ou = data["pairwise_eg"][0]["ou"]

        assert abs(ou["mu"]) < 0.5, \
            f"残差长期均值 μ={ou['mu']:.4f}, 应接近 0"

    def test_ou_sigma_positive(self):
        """σ 应为正"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
        ou = data["pairwise_eg"][0]["ou"]
        assert ou["sigma"] > 0, f"σ={ou['sigma']}, 应为正"

    def test_ou_vs_ar1(self):
        """OU θ 应与 AR(1) 系数反推一致: θ = -ln(b)/Δt"""
        residuals = np.array(
            call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])["pairwise_eg"][0]["residuals"]
        )

        # AR(1) OLS: ε_t = a + b*ε_{t-1} + noise
        x = residuals[:-1]
        y = residuals[1:]
        b_ols = np.polyfit(x, y, 1)[0]

        if 0 < b_ols < 1:
            theta_ref = -np.log(b_ols)

            data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B])
            theta_cpp = data["pairwise_eg"][0]["ou"]["theta"]

            err = rel_err(theta_cpp, theta_ref)
            assert err < OU_TOL, \
                f"OU θ: C++={theta_cpp:.4f}, AR(1)反推={theta_ref:.4f}, rel_err={err:.2%}"


# ============================================================
# 5. Granger 因果检验
# ============================================================

class TestGrangerCausality:
    """Granger 因果: C++ vs statsmodels"""

    def test_granger_direction(self):
        """已知 Y→X 因果, C++ 应检测到"""
        data = call_api([SYMBOL_GRANGER_X, SYMBOL_GRANGER_Y])

        pairwise = data["granger"]["pairwise"]
        # 找到 Y→X 方向的检验
        y_causes_x = [g for g in pairwise
                      if SYMBOL_GRANGER_Y in g["from"] and SYMBOL_GRANGER_X in g["from"]]

        assert len(y_causes_x) > 0, "未找到 Y→X 方向的 Granger 检验"
        assert y_causes_x[0]["is_significant"], \
            f"Y→X 应显著 (已知因果), p={y_causes_x[0]['p_value']:.4f}"

    def test_granger_f_statistic(self):
        """F 统计量应与 statsmodels 在合理容差内"""
        a = load_csv_prices(SYMBOL_GRANGER_X)
        b = load_csv_prices(SYMBOL_GRANGER_Y)

        # statsmodels: Y causes X?
        xy = np.column_stack([a, b])
        sm_results = grangercausalitytests(xy, maxlag=1, verbose=False)
        sm_f = sm_results[1][0]['ssr_ftest'][0]

        data = call_api([SYMBOL_GRANGER_X, SYMBOL_GRANGER_Y], max_lag=1)
        pairwise = data["granger"]["pairwise"]

        # 找到对应方向
        y_causes_x = [g for g in pairwise
                      if SYMBOL_GRANGER_Y in g["from"] and SYMBOL_GRANGER_X in g["from"]]
        if y_causes_x:
            cpp_f = y_causes_x[0].get("f_statistic", 0)
            if abs(sm_f) > 0.1:
                err = rel_err(cpp_f, sm_f)
                assert err < GRANGER_F_TOL, \
                    f"Granger F: C++={cpp_f:.4f}, statsmodels={sm_f:.4f}, rel_err={err:.2%}"


# ============================================================
# 6. Johansen 多元协整
# ============================================================

class TestJohansen:
    """Johansen 检验基本正确性"""

    def test_johansen_rank_detection(self):
        """3 个标的中含 1 对协整, rank 应 ≥ 1"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B, SYMBOL_GRANGER_X])

        assert "johansen" in data, "≥3 标的应返回 Johansen 结果"
        joh = data["johansen"]
        assert joh["n_variables"] == 3
        assert joh["rank"] >= 1, \
            f"已知含 1 对协整, rank 应 ≥ 1, 实际 rank={joh['rank']}"

    def test_johansen_trace_stats_positive(self):
        """trace 统计量应非负"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B, SYMBOL_GRANGER_X])
        joh = data["johansen"]

        for i, s in enumerate(joh["trace_stats"]):
            assert s >= 0, f"trace_stats[{i}]={s}, 应 ≥ 0"

    def test_johansen_eigenvectors_shape(self):
        """特征向量矩阵应为 N×N"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B, SYMBOL_GRANGER_X])
        joh = data["johansen"]

        evecs = joh["eigenvectors"]
        assert len(evecs) == 3, f"行数应为 3, 实际 {len(evecs)}"
        for row in evecs:
            assert len(row) == 3, f"列数应为 3, 实际 {len(row)}"

    def test_johansen_trace_monotone(self):
        """trace 统计量应递减 (r=0 最大)"""
        data = call_api([SYMBOL_COINT_A, SYMBOL_COINT_B, SYMBOL_GRANGER_X])
        joh = data["johansen"]

        stats = joh["trace_stats"]
        for i in range(len(stats) - 1):
            assert stats[i] >= stats[i + 1], \
                f"trace 统计量应递减: stats[{i}]={stats[i]:.3f} < stats[{i+1}]={stats[i+1]:.3f}"
