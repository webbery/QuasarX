"""
波动率分析 API 测试

测试 /v0/analysis/volatility GET 接口：
- 单标的波动率指标
- 多标的关联分析
- 参数校验
- 历史回测模式下与 Python 预期数据对比

前置条件：
1. 服务已启动并处于回测模式
2. 已运行 generate_test_data.py 生成测试数据
"""
import pytest
import requests
import os
import csv
import numpy as np
from pathlib import Path
from tool import check_response, BASE_URL


@pytest.mark.usefixtures("auth_token")
class TestVolatilityAPI:
    """波动率分析 API 基础测试"""

    def test_volatility_single_symbol(self, auth_token):
        """单标的波动率分析"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.900001",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01",
                "windows": "20,60"
            },
            **kwargs
        )
        data = check_response(resp)
        assert "symbols" in data
        assert "single" in data
        assert "sz.900001" in data["single"]

        single = data["single"]["sz.900001"]
        assert "prices" in single
        assert "returns" in single
        assert "annual_volatility" in single
        assert "max_drawdown" in single
        assert "skewness" in single
        assert "kurtosis" in single
        assert "var_95" in single
        assert "cvar_95" in single
        assert "rolling_vol" in single
        assert "returns_acf" in single
        assert "abs_returns_acf" in single

        # 验证数据类型
        assert single["annual_volatility"] >= 0
        assert single["max_drawdown"] >= 0

    def test_volatility_multiple_symbols(self, auth_token):
        """多标的关联分析"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.900001,sz.900002",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01"
            },
            **kwargs
        )
        data = check_response(resp)
        assert "multi" in data
        multi = data["multi"]
        assert "correlation_matrix" in multi
        assert "covariance_matrix" in multi
        assert "condition_number" in multi
        assert "is_positive_definite" in multi
        assert "annual_volatility" in multi

        # 验证矩阵维度
        assert len(multi["correlation_matrix"]) == 2
        assert len(multi["correlation_matrix"][0]) == 2
        assert len(multi["annual_volatility"]) == 2

        # 对角线应为 1
        assert abs(multi["correlation_matrix"][0][0] - 1.0) < 0.01

    def test_volatility_missing_symbols(self, auth_token):
        """缺少标的参数应返回 400"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={"start_date": "2023-01-01"},
            **kwargs
        )
        assert resp.status_code == 400
        assert "error" in resp.json()

    def test_volatility_no_data(self, auth_token):
        """不存在的标的数据应返回空结果"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.999999",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01"
            },
            **kwargs
        )
        # 不存在的标的：返回 200 且 single 中无该标的
        assert resp.status_code == 200
        try:
            data = resp.json()
        except Exception:
            pytest.fail(f"响应不是有效 JSON: {resp.text}")
        
        assert isinstance(data, dict), f"响应不是字典: {type(data)}"
        single = data.get("single")
        # single 为 None 或空字典都表示无数据
        assert single is None or (isinstance(single, dict) and "sz.999999" not in single)


@pytest.mark.usefixtures("auth_token")
class TestVolatilityAccuracy:
    """波动率计算精度测试：与 Python 预期对比"""

    @staticmethod
    def load_csv_prices(symbol):
        """读取测试 CSV 数据"""
        csv_path = Path(__file__).parent.parent.parent / "build" / "data" / "AStock" / f"{symbol}.csv"
        if not csv_path.exists():
            pytest.skip(f"测试数据不存在: {csv_path}")
        with open(csv_path, 'r') as f:
            rows = list(csv.DictReader(f))
        closes = [float(r['close']) for r in rows]
        volumes = [float(r.get('volume', 0)) for r in rows]
        dates = [r['datetime'] for r in rows]
        return closes, volumes, dates

    @staticmethod
    def compute_python_expected(closes):
        """计算 Python 预期指标"""
        returns = [(closes[i] - closes[i-1]) / closes[i-1] for i in range(1, len(closes))]
        n = len(returns)
        if n < 2:
            return {}

        annual_vol = np.std(returns, ddof=1) * np.sqrt(252)
        max_dd = 0
        peak = closes[0]
        for v in closes:
            if v > peak:
                peak = v
            dd = (peak - v) / peak
            if dd > max_dd:
                max_dd = dd

        m = np.mean(returns)
        s = np.std(returns, ddof=1)
        skew = float((n / ((n-1) * (n-2))) * sum(((r - m) / s) ** 3 for r in returns)) if s > 1e-10 else 0

        sorted_returns = sorted(returns)
        var_idx = int(0.05 * n)
        var_95 = -sorted_returns[var_idx] if var_idx < n else 0

        return {
            "annual_volatility": annual_vol,
            "max_drawdown": max_dd,
            "skewness": skew,
            "var_95": var_95
        }

    def test_up_trend_volatility_accuracy(self, auth_token):
        """验证单边上涨用例的波动率计算精度"""
        symbol = "sz.900001"
        closes, volumes, dates = self.load_csv_prices(symbol)

        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": symbol,
                "start_date": dates[0],
                "end_date": dates[-1]
            },
            **kwargs
        )
        data = check_response(resp)
        cpp_result = data["single"][symbol]

        expected = self.compute_python_expected(closes)

        # 验证波动率（容差 5%）
        cpp_vol = cpp_result["annual_volatility"]
        py_vol = expected["annual_volatility"]
        if py_vol > 0:
            assert abs(cpp_vol - py_vol) / py_vol < 0.05, \
                f"年化波动率差异过大: C++={cpp_vol:.6f}, Python={py_vol:.6f}"

        # 验证最大回撤
        cpp_dd = cpp_result["max_drawdown"]
        py_dd = expected["max_drawdown"]
        assert abs(cpp_dd - py_dd) < 0.01, \
            f"最大回撤差异过大: C++={cpp_dd:.6f}, Python={py_dd:.6f}"

    def test_down_trend_volatility_accuracy(self, auth_token):
        """验证单边下跌用例的波动率计算精度"""
        symbol = "sz.900002"
        closes, volumes, dates = self.load_csv_prices(symbol)

        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": symbol,
                "start_date": dates[0],
                "end_date": dates[-1]
            },
            **kwargs
        )
        data = check_response(resp)
        cpp_result = data["single"][symbol]

        expected = self.compute_python_expected(closes)

        cpp_vol = cpp_result["annual_volatility"]
        py_vol = expected["annual_volatility"]
        if py_vol > 0:
            assert abs(cpp_vol - py_vol) / py_vol < 0.05, \
                f"年化波动率差异过大: C++={cpp_vol:.6f}, Python={py_vol:.6f}"

    def test_high_volatility_accuracy(self, auth_token):
        """验证高波动用例的波动率计算精度"""
        symbol = "sz.900005"
        closes, volumes, dates = self.load_csv_prices(symbol)

        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": symbol,
                "start_date": dates[0],
                "end_date": dates[-1]
            },
            **kwargs
        )
        data = check_response(resp)
        cpp_result = data["single"][symbol]

        expected = self.compute_python_expected(closes)

        cpp_vol = cpp_result["annual_volatility"]
        py_vol = expected["annual_volatility"]
        if py_vol > 0:
            assert abs(cpp_vol - py_vol) / py_vol < 0.05, \
                f"年化波动率差异过大: C++={cpp_vol:.6f}, Python={py_vol:.6f}"


@pytest.mark.usefixtures("auth_token")
class TestVolatilityProperties:
    """波动率数学属性测试"""

    def test_rolling_volatility_decreases_with_window(self, auth_token):
        """滚动波动率应随窗口增大而减小（或持平）"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.900001",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01",
                "windows": "20,60,120"
            },
            **kwargs
        )
        data = check_response(resp)
        single = data["single"]["sz.900001"]
        rolling = single["rolling_vol"]

        if "20" in rolling and "60" in rolling:
            vol_20 = np.mean(rolling["20"])
            vol_60_values = [v for v in rolling["60"] if not np.isnan(v)]
            if len(vol_60_values) == 0:
                pytest.skip("60日滚动波动率无有效数据（数据量不足）")
            vol_60 = np.mean(vol_60_values)
            # 短期波动率通常 >= 长期波动率
            assert vol_20 >= vol_60 * 0.9, \
                f"短期波动率({vol_20:.4f})应 >= 长期波动率({vol_60:.4f})"

    def test_var_cvar_relationship(self, auth_token):
        """CVaR 应 >= VaR（尾部风险更大，即亏损更多）"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.900001",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01"
            },
            **kwargs
        )
        data = check_response(resp)
        single = data["single"]["sz.900001"]

        # VaR/CVaR 在 C++ 中返回的是 -sorted_returns[idx]（正值表示亏损）
        # CVaR 应该 >= VaR（尾部平均亏损 >= 分位数亏损）
        var_95 = single["var_95"]
        cvar_95 = single["cvar_95"]
        
        # 只验证正值场景（负值表示没有亏损，CVaR/VaR 关系无意义）
        if var_95 > 0 and cvar_95 > 0:
            assert cvar_95 >= var_95 * 0.95, \
                f"CVaR({cvar_95:.6f}) 应 >= VaR({var_95:.6f})"

    def test_correlation_symmetric(self, auth_token):
        """相关系数矩阵应对称"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        resp = requests.get(
            f"{BASE_URL}/analysis/volatility",
            params={
                "symbols": "sz.900001,sz.900002,sz.900003",
                "start_date": "2023-01-01",
                "end_date": "2023-04-01"
            },
            **kwargs
        )
        data = check_response(resp)
        corr = data["multi"]["correlation_matrix"]
        n = len(corr)
        for i in range(n):
            for j in range(i + 1, n):
                assert abs(corr[i][j] - corr[j][i]) < 1e-6, \
                    f"相关系数不对称: [{i}][{j}]={corr[i][j]}, [{j}][{i}]={corr[j][i]}"
