#!/usr/bin/env python3
"""
时间序列计量分析 API 契约测试

只验证 REST 接口本身：状态码、响应结构、参数校验、数据源。
不做数值黄金标准对比（算法正确性由 C++ 侧单测覆盖）。

接口: GET|POST /v0/analysis/time_series
  action = adf | kpss | cointegration | johansen_trace | johansen_max_eigen | granger

使用方法：
  pytest test_time_series.py -v

前置准备：
  python generate_test_data.py   # 生成测试数据
  服务已启动
"""

import numpy as np
import pytest
import requests
from tool import (
    BASE_URL,
    VERIFY_SSL,
    from_api_symbol,
    to_api_symbol,
)

# 抑制 SSL 警告
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ============================================================
# 配置
# ============================================================

# 测试标的（generate_test_data.py 生成）
SYMBOL_A = "sz.910003"
SYMBOL_B = "sz.910004"
SYMBOL_C = "sz.910001"

ALL_ACTIONS = ["adf", "kpss", "cointegration", "johansen_trace", "johansen_max_eigen", "granger"]


# ============================================================
# 辅助函数
# ============================================================

def api_url() -> str:
    return f"{BASE_URL}/analysis/time_series"


def auth_headers(auth_token) -> dict:
    return {"Authorization": auth_token} if auth_token and len(auth_token) > 10 else {}


def post_time_series(body: dict, auth_token=None):
    """POST /analysis/time_series，返回原始 response"""
    return requests.post(api_url(), json=body, headers=auth_headers(auth_token),
                         verify=VERIFY_SSL)


def call_time_series(action: str, symbols=None, max_lag: int = 10,
                     auth_token=None, **extra) -> dict:
    """POST 并断言 200，返回解析后的 JSON（标的转回内部格式）"""
    body = {"action": action, "max_lag": max_lag, **extra}
    if symbols is not None:
        if isinstance(symbols, (list, tuple)):
            body["symbols"] = ','.join(to_api_symbol(s) for s in symbols)
        else:
            body["symbols"] = symbols

    resp = post_time_series(body, auth_token)
    assert resp.status_code == 200, f"API 请求失败: {resp.status_code} - {resp.text}"

    return _to_internal_symbols(resp.json())


def _to_internal_symbols(data: dict) -> dict:
    """响应里的 API 格式代码转回内部格式（sz.910003）"""
    if "symbols" in data:
        data["symbols"] = [from_api_symbol(s) for s in data["symbols"]]

    for key in ("adf", "kpss"):
        if key in data and isinstance(data[key], dict):
            # on=residuals 时 key 是 "X~Y"，逐段转换
            data[key] = {
                "~".join(from_api_symbol(p) for p in k.split("~")): v
                for k, v in data[key].items()
            }

    if "pair" in data:
        data["pair"]["symbol_x"] = from_api_symbol(data["pair"]["symbol_x"])
        data["pair"]["symbol_y"] = from_api_symbol(data["pair"]["symbol_y"])

    if "pairwise_eg" in data:
        for eg in data["pairwise_eg"]:
            eg["symbol_x"] = from_api_symbol(eg["symbol_x"])
            eg["symbol_y"] = from_api_symbol(eg["symbol_y"])

    if "granger" in data:
        for g in data["granger"].get("pairwise", []):
            g["from"] = from_api_symbol(g["from"])
            g["to"] = from_api_symbol(g["to"])
        for g in data["granger"].get("multivariate", []):
            g["from"] = from_api_symbol(g["from"])
            g["to"] = from_api_symbol(g["to"])
            g["condition_set"] = [from_api_symbol(s) for s in g.get("condition_set", [])]

    return data


# ============================================================
# 1. 通用契约
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestCommonContract:

    @pytest.mark.parametrize("action", ALL_ACTIONS)
    def test_action_returns_symbols_and_dates(self, auth_token, action):
        """每个 action 都返回 symbols 与 dates"""
        symbols = [SYMBOL_A, SYMBOL_B] if action not in ("adf", "kpss") else [SYMBOL_A]
        data = call_time_series(action, symbols, auth_token=auth_token)

        assert "symbols" in data
        assert "dates" in data
        assert len(data["symbols"]) == len(data["dates"])

    def test_missing_action_400(self, auth_token):
        """缺 action → 400，错误信息列出合法值"""
        resp = post_time_series({"symbols": to_api_symbol(SYMBOL_A)}, auth_token)
        assert resp.status_code == 400
        assert "adf" in resp.json()["error"]

    def test_unknown_action_400(self, auth_token):
        """未知 action → 400"""
        resp = post_time_series({"action": "not_a_test", "symbols": to_api_symbol(SYMBOL_A)},
                                auth_token)
        assert resp.status_code == 400
        assert "not_a_test" in resp.json()["error"]

    def test_missing_data_source_400(self, auth_token):
        """既没有 symbols 也没有 series → 400"""
        resp = post_time_series({"action": "adf"}, auth_token)
        assert resp.status_code == 400
        assert "series" in resp.json()["error"] or "symbols" in resp.json()["error"]

    def test_invalid_field_400(self, auth_token):
        """非法 field → 400，错误信息列出合法取值"""
        resp = post_time_series({"action": "adf", "symbols": to_api_symbol(SYMBOL_A),
                                 "field": "vwap"}, auth_token)
        assert resp.status_code == 400
        assert "close" in resp.json()["error"]


# ============================================================
# 2. 数据源
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestDataSource:

    def test_adf_single_symbol(self, auth_token):
        """单位根检验单序列即可"""
        data = call_time_series("adf", [SYMBOL_A], auth_token=auth_token)
        assert SYMBOL_A in data["adf"]

    def test_field_open_differs_from_close(self, auth_token):
        """field 生效：open 与 close 的统计量不应相同"""
        close = call_time_series("adf", [SYMBOL_A], auth_token=auth_token)
        open_ = call_time_series("adf", [SYMBOL_A], field="open", auth_token=auth_token)

        assert close["adf"][SYMBOL_A]["statistic"] != open_["adf"][SYMBOL_A]["statistic"]

    def test_series_payload(self, auth_token):
        """直接传入时间序列，不依赖本地行情"""
        rng = np.random.default_rng(42)
        x = np.cumsum(rng.normal(size=300))
        y = 2.0 * x + rng.normal(scale=0.05, size=300)

        data = call_time_series("cointegration", series=[x.tolist(), y.tolist()],
                                symbols="X,Y", auth_token=auth_token)

        assert data["symbols"] == ["X", "Y"]
        assert len(data["pairwise_eg"]) == 1
        assert data["pairwise_eg"][0]["symbol_x"] == "X"

    def test_series_default_names(self, auth_token):
        """不传 symbols 时按 series_0 / series_1 命名"""
        rng = np.random.default_rng(7)
        x = np.cumsum(rng.normal(size=200)).tolist()
        y = np.cumsum(rng.normal(size=200)).tolist()

        data = call_time_series("johansen_trace", series=[x, y], auth_token=auth_token)
        assert data["symbols"] == ["series_0", "series_1"]

    def test_get_method_supported(self, auth_token):
        """GET 走 query 参数，按标的取数"""
        resp = requests.get(
            api_url(),
            params={"action": "adf", "symbols": to_api_symbol(SYMBOL_A)},
            headers=auth_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 200
        assert len(resp.json()["adf"]) == 1


# ============================================================
# 3. 各 action 的响应结构
# ============================================================

@pytest.mark.usefixtures("auth_token")
class TestADFContract:

    ADF_KEYS = {"statistic", "p_value", "cv_1pct", "cv_5pct", "cv_10pct",
                "lags", "is_stationary"}

    def test_adf_fields(self, auth_token):
        adf = call_time_series("adf", [SYMBOL_A], auth_token=auth_token)["adf"][SYMBOL_A]
        assert self.ADF_KEYS <= set(adf)

    def test_adf_critical_values_ordering(self, auth_token):
        """临界值应满足 cv_1% < cv_5% < cv_10% < 0"""
        adf = call_time_series("adf", [SYMBOL_A], auth_token=auth_token)["adf"][SYMBOL_A]
        assert adf["cv_1pct"] < adf["cv_5pct"] < adf["cv_10pct"] < 0

    def test_adf_on_residuals_returns_pair(self, auth_token):
        """on=residuals → 带残差对，key 为 "X~Y" """
        data = call_time_series("adf", [SYMBOL_A, SYMBOL_B],
                                on="residuals", auth_token=auth_token)

        assert data["pair"]["symbol_x"] == SYMBOL_A
        assert data["pair"]["symbol_y"] == SYMBOL_B
        assert len(data["pair"]["residuals"]) > 0
        assert f"{SYMBOL_A}~{SYMBOL_B}" in data["adf"]

    def test_adf_on_residuals_requires_two(self, auth_token):
        """on=residuals 只接受恰好 2 条序列"""
        resp = post_time_series({"action": "adf", "on": "residuals",
                                 "symbols": to_api_symbol(SYMBOL_A)}, auth_token)
        assert resp.status_code == 400
        assert "2" in resp.json()["error"]


@pytest.mark.usefixtures("auth_token")
class TestKPSSContract:

    KPSS_KEYS = {"statistic", "p_value", "lags", "lr_variance", "is_stationary"}

    def test_kpss_fields(self, auth_token):
        kpss = call_time_series("kpss", [SYMBOL_A], auth_token=auth_token)["kpss"][SYMBOL_A]
        assert self.KPSS_KEYS <= set(kpss)

    def test_kpss_on_residuals(self, auth_token):
        data = call_time_series("kpss", [SYMBOL_A, SYMBOL_B],
                                on="residuals", auth_token=auth_token)
        assert f"{SYMBOL_A}~{SYMBOL_B}" in data["kpss"]


@pytest.mark.usefixtures("auth_token")
class TestCointegrationContract:

    EG_KEYS = {"symbol_x", "symbol_y", "alpha", "beta", "r_squared",
               "adf", "kpss", "half_life", "is_cointegrated", "ou", "residuals"}

    def test_pairwise_fields(self, auth_token):
        eg = call_time_series("cointegration", [SYMBOL_A, SYMBOL_B],
                              auth_token=auth_token)["pairwise_eg"][0]
        assert self.EG_KEYS <= set(eg)
        assert eg["symbol_x"] == SYMBOL_A
        assert eg["symbol_y"] == SYMBOL_B

    def test_pair_count(self, auth_token):
        """3 条序列 → 3 对"""
        data = call_time_series("cointegration", [SYMBOL_A, SYMBOL_B, SYMBOL_C],
                                auth_token=auth_token)
        assert len(data["pairwise_eg"]) == 3

    def test_requires_two_symbols(self, auth_token):
        resp = post_time_series({"action": "cointegration",
                                 "symbols": to_api_symbol(SYMBOL_A)}, auth_token)
        assert resp.status_code == 400


@pytest.mark.usefixtures("auth_token")
class TestJohansenContract:

    SYMBOLS3 = [SYMBOL_A, SYMBOL_B, SYMBOL_C]

    def test_trace_fields(self, auth_token):
        joh = call_time_series("johansen_trace", self.SYMBOLS3, auth_token=auth_token)["johansen"]

        assert {"n_variables", "rank", "trace_stats",
                "trace_cv_95", "trace_cv_99", "trace_significant"} <= set(joh)
        n = len(joh["trace_stats"])
        assert len(joh["trace_cv_95"]) == n == len(joh["trace_significant"])

    def test_trace_excludes_max_eigen(self, auth_token):
        """迹检验只返回 trace 半"""
        joh = call_time_series("johansen_trace", self.SYMBOLS3, auth_token=auth_token)["johansen"]
        assert "max_eigen_stats" not in joh

    def test_max_eigen_fields(self, auth_token):
        joh = call_time_series("johansen_max_eigen", self.SYMBOLS3,
                               auth_token=auth_token)["johansen"]

        assert {"n_variables", "rank", "max_eigen_stats",
                "max_eigen_cv_95", "max_eigen_cv_99", "max_eigen_significant"} <= set(joh)
        n = len(joh["max_eigen_stats"])
        assert len(joh["max_eigen_cv_95"]) == n == len(joh["max_eigen_significant"])

    def test_max_eigen_excludes_trace(self, auth_token):
        """最大特征值检验只返回 max-eigen 半"""
        joh = call_time_series("johansen_max_eigen", self.SYMBOLS3,
                               auth_token=auth_token)["johansen"]
        assert "trace_stats" not in joh

    def test_cv_99_gt_cv_95(self, auth_token):
        joh = call_time_series("johansen_trace", self.SYMBOLS3, auth_token=auth_token)["johansen"]
        assert joh["trace_cv_99"][0] > joh["trace_cv_95"][0]

    def test_two_symbols_supported(self, auth_token):
        """2 条序列也能做 Johansen（原实现要求 ≥3）"""
        joh = call_time_series("johansen_trace", [SYMBOL_A, SYMBOL_B],
                               auth_token=auth_token)["johansen"]
        assert joh["n_variables"] == 2
        assert len(joh["trace_stats"]) == 2


@pytest.mark.usefixtures("auth_token")
class TestGrangerContract:

    def test_pairwise_fields(self, auth_token):
        pairwise = call_time_series("granger", [SYMBOL_A, SYMBOL_B],
                                    auth_token=auth_token)["granger"]["pairwise"]
        assert len(pairwise) == 2, "2 条序列应产生双向 2 条结果"
        assert {"from", "to", "f_statistic", "p_value",
                "is_significant", "optimal_lag"} <= set(pairwise[0])

    def test_multivariate_with_three(self, auth_token):
        granger = call_time_series("granger", [SYMBOL_A, SYMBOL_B, SYMBOL_C],
                                   auth_token=auth_token)["granger"]
        assert len(granger["pairwise"]) == 6
        assert "multivariate" in granger

    def test_no_multivariate_with_two(self, auth_token):
        granger = call_time_series("granger", [SYMBOL_A, SYMBOL_B],
                                   auth_token=auth_token)["granger"]
        assert granger.get("multivariate", []) == []
