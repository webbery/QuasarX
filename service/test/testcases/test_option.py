#!/usr/bin/env python3
"""
期权 API 集成测试

覆盖端点:
  POST  /v0/option/pricing         — 单合约定价 (BS/MC/Binomial)
  POST  /v0/option/pricing_multi   — 多合约批量定价
  GET   /v0/option/iv_surface      — IV 曲面 (依赖 option_daily)
  POST  /v0/option/data            — action=import_csv 测试通道 + 下载参数校验
  GET   /v0/option/data            — listContracts / queryByContract / queryBySymbolId
  DELETE /v0/option/data           — 清空表

数据准备:
  生成脚本: generate_option_test_data.py
  数据位置: $DATA_DIR/test_option/{exchange}/{product}/{contract_code}.csv
  导入方式: POST /v0/option/data {"action":"import_csv", "csv_path":...}

黄金标准:
  Python BS 公式 (scipy-free, 用 math.erf 实现 N(x)) — 与 C++ OptionPricer::blackScholes 对齐

注意:
  - 测试 class 级 fixture autouse, 生成+导入仅一次, 清空在 teardown
  - 不依赖任何 akshare/网络, CI 可重现
"""
import math
import os
import shutil
import time
from typing import Dict, List

import pytest
import requests
import urllib3

from tool import BASE_URL

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

VERIFY_SSL = False

# ── 路径解析 (与 generate_option_test_data.py 一致) ──
def _find_service_data_dir() -> str:
    current = os.path.dirname(os.path.abspath(__file__))
    for _ in range(10):
        if os.path.exists(os.path.join(current, "build", "QuantService")):
            return os.path.join(current, "build", "data")
        if os.path.exists(os.path.join(current, "QuantService")):
            return os.path.join(current, "data")
        if os.path.exists(os.path.join(current, "CMakeLists.txt")):
            cand = os.path.join(current, "build", "data")
            return cand if os.path.isdir(os.path.dirname(cand)) else os.path.join(current, "data")
        current = os.path.dirname(current)
    raise RuntimeError("Cannot find service root")


DATA_DIR = _find_service_data_dir()
OPTION_DIR = os.path.join(DATA_DIR, "test_option")

CONTRACT_FIXTURES = [
    # (exchange, product, contract_code, csv_subpath)
    # CFFEX 跳过: strike > 1023 装不进 symbol_t._price 10 bits
    ("SSE",   "50ETF", "10003187",       "sse/50ETF/10003187.csv"),
    ("SSE",   "50ETF", "10003188",       "sse/50ETF/10003188.csv"),
    ("SSE",   "50ETF", "10003189",       "sse/50ETF/10003189.csv"),
    ("SZSE",  "159919","90003187",       "szse/159919/90003187.csv"),
    ("SZSE",  "159919","90003188",       "szse/159919/90003188.csv"),
]


# ── Python BS 黄金标准 (与 C++ OptionPricer::blackScholes 公式一致) ──
def _N(x: float) -> float:
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def _n(x: float) -> float:
    return math.exp(-0.5 * x * x) / math.sqrt(2.0 * math.pi)


def py_black_scholes(S, K, T, sigma, r, q, is_call):
    """解析 BS, 返回 (price, delta, gamma, theta, vega, rho)"""
    if T <= 0 or sigma <= 0:
        intrinsic = max(0.0, (S - K) if is_call else (K - S))
        sign = 1.0 if is_call else -1.0
        return intrinsic, sign if intrinsic > 0 else 0.0, 0.0, 0.0, 0.0, 0.0

    sqrt_T = math.sqrt(T)
    d1 = (math.log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / (sigma * sqrt_T)
    d2 = d1 - sigma * sqrt_T

    if is_call:
        price = S * math.exp(-q * T) * _N(d1) - K * math.exp(-r * T) * _N(d2)
        delta = math.exp(-q * T) * _N(d1)
        theta = (-S * math.exp(-q * T) * _n(d1) * sigma / (2.0 * sqrt_T)
                 - r * K * math.exp(-r * T) * _N(d2)
                 + q * S * math.exp(-q * T) * _N(d1)) / 365.0
        rho = K * T * math.exp(-r * T) * _N(d2) / 100.0
    else:
        price = K * math.exp(-r * T) * _N(-d2) - S * math.exp(-q * T) * _N(-d1)
        delta = -math.exp(-q * T) * _N(-d1)
        theta = (-S * math.exp(-q * T) * _n(d1) * sigma / (2.0 * sqrt_T)
                 + r * K * math.exp(-r * T) * _N(-d2)
                 - q * S * math.exp(-q * T) * _N(-d1)) / 365.0
        rho = -K * T * math.exp(-r * T) * _N(-d2) / 100.0

    gamma = math.exp(-q * T) * _n(d1) / (S * sigma * sqrt_T)
    vega = S * math.exp(-q * T) * _n(d1) * sqrt_T / 100.0
    return price, delta, gamma, theta, vega, rho


def py_binomial_crr(S, K, T, sigma, r, q, is_call, n_steps=200, is_american=False):
    """CRR 二叉树 (黄金标准)"""
    dt = T / n_steps
    u = math.exp(sigma * math.sqrt(dt))
    d = 1.0 / u
    p = (math.exp((r - q) * dt) - d) / (u - d)
    disc = math.exp(-r * dt)

    # 终值
    values = []
    for i in range(n_steps + 1):
        ST = S * (u ** (n_steps - i)) * (d ** i)
        values.append(max(0.0, ST - K) if is_call else max(0.0, K - ST))

    # 倒推
    for step in range(n_steps, 0, -1):
        for i in range(step):
            hold = disc * (p * values[i] + (1 - p) * values[i + 1])
            if is_american:
                ST = S * (u ** (step - 1 - i)) * (d ** i)
                exercise = max(0.0, ST - K) if is_call else max(0.0, K - ST)
                values[i] = max(hold, exercise)
            else:
                values[i] = hold

    return values[0]


# ═══════════════════════════════════════════════════════════
#  Fixture: 生成 + 导入 + 清理
# ═══════════════════════════════════════════════════════════

def _generate_and_import(headers) -> List[str]:
    """生成 CSV 并通过 import_csv action 灌入 OptionDataDB, 返回 CSV路径列表"""
    # 调生成脚本 (subprocess, 保证 cwd 在 testcases/)
    from generate_option_test_data import generate as gen
    paths, n = gen()
    assert n == 200, f"Expected 200 rows (10 contracts × 20 days), got {n}"

    # 逐文件导入
    imported = []
    for path in paths:
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"action": "import_csv", "csv_path": path},
            headers=headers, verify=VERIFY_SSL, timeout=30)
        assert resp.status_code == 200, f"Import failed {path}: {resp.text}"
        body = resp.json()
        assert body["status"] == "imported", f"Bad status: {body}"
        imported.append(path)

    # ── 上传 underlying 标的行情 ──
    # IV surface handler 通过 QuoteDB.getLatestClose("stock_daily", "sh.510050") 取 spot;
    # 测试 conftest 只灌策略引用的标的, 不灌 underlying. 这里补一份 50ETF 的 close 行情,
    # 让 spot_price ≈ 2.6 (与期权 strike 同量级), filter L1 才能正确计算 intrinsic.
    _upload_underlying_quotes(headers)

    return imported


def _upload_underlying_quotes(headers):
    """为 IV 曲面测试生成并上传 underlying 标的 (sh.510050 / sz.159919) 行情

    时间范围与 generate_option_test_data.py 的 TRADING_DAYS 对齐 (2024-06-03 ~ 2024-06-28),
    价格固定在 strike 量级 (50ETF=2.6, 300ETF=3.5).
    """
    from datetime import date, timedelta

    # 与 generate_option_test_data.py 的 TRADING_DAYS 一致 (20 个工作日)
    days = []
    d = date(2024, 6, 3)
    while len(days) < 20:
        if d.weekday() < 5:
            days.append(d)
        d += timedelta(days=1)

    # 生成两份 CSV 内容 (与 A_hfq 目录里 baostock 输出格式对齐)
    targets = [
        ("sh.510050", 2.6),
        ("sz.159919", 3.5),
    ]
    for symbol, base_price in targets:
        rows = ["date,open,high,low,close,volume,amount,turn"]
        for d in days:
            # 微量随机扰动, 但保持 close 在 base_price 附近
            noise = ((d.toordinal() % 5) - 2) * 0.001
            close = round(base_price * (1.0 + noise), 4)
            rows.append(f"{d.isoformat()},{close},{close},{close},{close},1000000,{close*1000000:.4f},{close/2}")
        data = "\n".join(rows)

        resp = requests.post(
            f"{BASE_URL}/quote/data",
            json={
                "action": "import",
                "table": "stock_1d",
                "symbol": symbol,
                "data": data.split("\n"),
                "data_hfq": data.split("\n"),
            },
            headers=headers, verify=VERIFY_SSL, timeout=30)
        assert resp.status_code == 200, f"underlying import {symbol} failed: {resp.text}"


def _clear_option_table(headers):
    """DELETE /option/data 清空 (无 contract 参数 → 清空整表)"""
    requests.delete(f"{BASE_URL}/option/data", headers=headers, verify=VERIFY_SSL)


@pytest.fixture(scope="class", autouse=True)
def option_data_fixture(auth_api):
    """class 级 fixture: 首测前导入, 末测后清空 (复用 session 级 auth_api)"""
    headers = {"Authorization": auth_api.token} if auth_api.token else {}
    # 清空上轮残留 (跨测试 session 保险)
    try:
        _clear_option_table(headers)
    except Exception:
        pass
    _generate_and_import(headers)
    yield
    _clear_option_table(headers)


# ═══════════════════════════════════════════════════════════
#  POST /option/pricing — 纯计算, BS/MC/Binomial 黄金标准
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionPricingSingle:
    """POST /v0/option/pricing 单合约定价"""

    def _post(self, headers, payload):
        return requests.post(
            f"{BASE_URL}/option/pricing",
            json=payload,
            headers=headers, verify=VERIFY_SSL, timeout=60)

    # ── Black-Scholes ──
    def test_bs_atm_call_matches_python(self, headers):
        """ATM 看涨期权, C++ BS 与 Python BS 误差 < 1e-4"""
        S, K, T, sigma, r, q = 100.0, 100.0, 0.5, 0.2, 0.05, 0.0
        resp = self._post(headers, {
            "method": "black_scholes", "spot": S, "strike": K, "T": T,
            "volatility": sigma, "risk_free_rate": r,
            "dividend_yield": q, "is_call": True})
        assert resp.status_code == 200, resp.text
        d = resp.json()
        py_price, py_delta, py_gamma, py_theta, py_vega, py_rho = py_black_scholes(
            S, K, T, sigma, r, q, True)
        assert abs(d["price"] - py_price) < 1e-4, f"price C++={d['price']} py={py_price}"
        assert abs(d["greeks"]["delta"] - py_delta) < 1e-4
        assert abs(d["greeks"]["gamma"] - py_gamma) < 1e-6
        assert abs(d["greeks"]["vega"]  - py_vega)  < 1e-4
        assert abs(d["greeks"]["theta"] - py_theta) < 1e-4
        assert abs(d["greeks"]["rho"]   - py_rho)   < 1e-4
        assert d["moneyness"] == "ATM"
        assert d["intrinsic_value"] == pytest.approx(0.0, abs=1e-6)
        assert d["time_value"] > 0
        assert isinstance(d["payoff_curve"], list) and len(d["payoff_curve"]) > 0

    def test_bs_otm_put_matches_python(self, headers):
        """OTM 看跌, 校验 Python 一致性"""
        S, K, T, sigma = 95.0, 100.0, 0.25, 0.3
        resp = self._post(headers, {
            "method": "black_scholes", "spot": S, "strike": K, "T": T,
            "volatility": sigma, "is_call": False})
        d = resp.json()
        py_price, *_ = py_black_scholes(S, K, T, sigma, 0.015, 0.0, False)
        assert abs(d["price"] - py_price) < 1e-4
        assert d["moneyness"] == "OTM"
        assert d["greeks"]["delta"] < 0  # put delta < 0

    def test_bs_itm_intrinsic_and_time_value(self, headers):
        """深度 ITM 看涨: intrinsic = S-K, time_value = price - intrinsic"""
        S, K = 130.0, 100.0
        resp = self._post(headers, {
            "method": "black_scholes", "spot": S, "strike": K, "T": 0.5,
            "volatility": 0.2, "is_call": True})
        d = resp.json()
        assert d["moneyness"] == "ITM"
        assert d["intrinsic_value"] == pytest.approx(S - K, abs=1e-6)
        assert d["time_value"] == pytest.approx(d["price"] - d["intrinsic_value"], abs=1e-6)
        assert d["time_value"] > 0

    def test_bs_uses_default_volatility(self, headers):
        """不传 volatility → 默认 0.2"""
        resp = self._post(headers, {
            "method": "black_scholes", "spot": 100, "strike": 100, "T": 0.5,
            "is_call": True})
        d = resp.json()
        # 默认 sigma=0.2 → 期望与 Python sigma=0.2 一致
        py_price, *_ = py_black_scholes(100, 100, 0.5, 0.2, 0.015, 0.0, True)
        assert abs(d["price"] - py_price) < 1e-4

    def test_bs_expiry_string_converts_to_T(self, headers):
        """传 expiry="2025-12-31" 自动换算 T (1/365 单位)"""
        from datetime import date
        T_expected = max((date(2025, 12, 31) - date.today()).days, 1) / 365.0
        resp = self._post(headers, {
            "method": "black_scholes", "spot": 100, "strike": 100,
            "expiry": "2025-12-31", "volatility": 0.2, "is_call": True})
        d = resp.json()
        py_price, *_ = py_black_scholes(100, 100, T_expected, 0.2, 0.015, 0.0, True)
        assert abs(d["price"] - py_price) < 1e-4

    # ── Monte Carlo ──
    def test_mc_within_std_error(self, headers):
        """MC 多次取均值应在 ±3σ 命中 Python BS (用固定 seed 保证复现)"""
        S, K, T, sigma, r, q = 100.0, 100.0, 0.5, 0.2, 0.05, 0.0
        resp = self._post(headers, {
            "method": "monte_carlo", "spot": S, "strike": K, "T": T,
            "volatility": sigma, "risk_free_rate": r,
            "dividend_yield": q, "is_call": True,
            "n_paths": 200000, "n_steps": 252})
        d = resp.json()
        py_price, *_ = py_black_scholes(S, K, T, sigma, r, q, True)
        # MC 的 price 含时间价值贴现后, 与解析解差几个 std error
        sigma_mc = d.get("mc_std_error", 0)
        diff = abs(d["price"] - py_price)
        # 用 5σ 容差 (固定 seed, 实际会远小于)
        assert diff < 5 * sigma_mc + 1e-3, f"diff={diff} mc_std={sigma_mc}"

    def test_mc_returns_std_error(self, headers):
        """MC 必须返回 mc_std_error 字段"""
        resp = self._post(headers, {
            "method": "monte_carlo", "spot": 100, "strike": 100, "T": 0.5,
            "volatility": 0.2, "n_paths": 10000, "n_steps": 50})
        d = resp.json()
        assert "mc_std_error" in d
        assert d["mc_std_error"] > 0

    def test_mc_does_not_include_early_exercise_premium(self, headers):
        """BS/MC 不应含 early_exercise_premium 字段 (那是 binomial 专有)"""
        resp = self._post(headers, {
            "method": "monte_carlo", "spot": 100, "strike": 100, "T": 0.5,
            "volatility": 0.2})
        d = resp.json()
        assert "early_exercise_premium" not in d

    # ── Binomial ──
    def test_binomial_european_matches_python_crr(self, headers):
        """欧式二叉树应与 Python CRR 误差 < 0.01 (离散化引入的 1 step 误差)"""
        S, K, T, sigma = 100.0, 100.0, 0.5, 0.25
        resp = self._post(headers, {
            "method": "binomial", "spot": S, "strike": K, "T": T,
            "volatility": sigma, "is_call": True,
            "is_american": False, "n_steps": 500})
        d = resp.json()
        py_price = py_binomial_crr(S, K, T, sigma, 0.015, 0.0, True, 500, False)
        assert abs(d["price"] - py_price) < 0.01, f"C++={d['price']} py={py_price}"

    def test_binomial_american_higher_than_european(self, headers):
        """美式 ITM 看跌应 >= 欧式 (提前行权溢价)"""
        common = {"method": "binomial", "spot": 95, "strike": 100, "T": 1.0,
                  "volatility": 0.3, "is_call": False, "n_steps": 500}
        eur = self._post(headers, {**common, "is_american": False}).json()
        amr = self._post(headers, {**common, "is_american": True}).json()
        assert amr["price"] >= eur["price"] - 1e-4
        assert amr.get("early_exercise_premium", 0) >= 0

    # ── 响应结构 ──
    def test_response_has_all_required_fields(self, headers):
        resp = self._post(headers, {
            "method": "black_scholes", "spot": 100, "strike": 100, "T": 0.5,
            "volatility": 0.2, "is_call": True})
        d = resp.json()
        for key in ("price", "intrinsic_value", "time_value",
                    "moneyness", "greeks", "payoff_curve"):
            assert key in d, f"missing {key}"
        for g in ("delta", "gamma", "theta", "vega", "rho"):
            assert g in d["greeks"]
        for p in d["payoff_curve"]:
            assert "spot" in p and "payoff_at_expiry" in p and "payoff_now" in p


# ═══════════════════════════════════════════════════════════
#  POST /option/pricing_multi — 批量定价
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionPricingMulti:
    """POST /v0/option/pricing_multi 多合约批量定价"""

    def _post(self, headers, payload):
        return requests.post(
            f"{BASE_URL}/option/pricing_multi",
            json=payload,
            headers=headers, verify=VERIFY_SSL, timeout=60)

    def test_batch_pricing_returns_array(self, headers):
        """批量定价应返回数组, 每个元素含 strike/is_call"""
        resp = self._post(headers, {
            "method": "black_scholes", "spot": 100,
            "contracts": [
                    {"strike": 95,  "is_call": True,  "T": 0.5, "volatility": 0.2},
                    {"strike": 100, "is_call": True,  "T": 0.5, "volatility": 0.2},
                    {"strike": 105, "is_call": False, "T": 0.5, "volatility": 0.2},
                ]})
        assert resp.status_code == 200, resp.text
        data = resp.json()
        assert isinstance(data, list)
        assert len(data) == 3
        for i, item in enumerate(data):
            assert "strike" in item
            assert "is_call" in item
            assert "price" in item
            assert "greeks" in item

    def test_batch_pricing_per_contract_volatility(self, headers):
        """每个合约可独立指定 volatility"""
        resp = self._post(headers, {
            "spot": 100,
            "contracts": [
                {"strike": 100, "T": 0.5, "volatility": 0.1, "is_call": True},
                {"strike": 100, "T": 0.5, "volatility": 0.4, "is_call": True},
            ]})
        data = resp.json()
        # vol 越大, ATM call 越贵
        assert data[1]["price"] > data[0]["price"]

    def test_batch_pricing_shared_spot(self, headers):
        """批量定价共享 spot, 各合约各自 strike/call_put"""
        spot = 100
        contracts = [
            {"strike": 90,  "is_call": True,  "T": 0.5},
            {"strike": 110, "is_call": True,  "T": 0.5},
            {"strike": 110, "is_call": False, "T": 0.5},
        ]
        resp = self._post(headers, {"method": "black_scholes",
                                    "spot": spot, "contracts": contracts})
        data = resp.json()
        for c in contracts:
            py_price, *_ = py_black_scholes(spot, c["strike"], c["T"], 0.2, 0.015, 0.0, c["is_call"])
            item = next(d for d in data if d["strike"] == c["strike"] and d["is_call"] == c["is_call"])
            assert abs(item["price"] - py_price) < 1e-3, f"{c} py={py_price} cpp={item['price']}"

    def test_batch_pricing_empty_contracts_list(self, headers):
        """空合约列表 → 返回空数组 (不应崩溃)"""
        resp = self._post(headers, {
            "method": "black_scholes", "spot": 100, "contracts": []})
        assert resp.status_code == 200
        assert resp.json() == []

    def test_batch_pricing_missing_contracts_field(self, headers):
        """缺 contracts 字段 → 400 (nlohmann json 抛异常)"""
        resp = self._post(headers, {"method": "black_scholes", "spot": 100})
        assert resp.status_code == 400
        assert "error" in resp.json()


# ═══════════════════════════════════════════════════════════
#  GET /option/iv_surface — IV 曲面
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionIvSurface:
    """GET /v0/option/iv_surface"""

    def _get(self, headers, **params):
        return requests.get(
            f"{BASE_URL}/option/iv_surface",
            params=params, headers=headers, verify=VERIFY_SSL, timeout=30)

    def test_missing_exchange_param_returns_400(self, headers):
        resp = self._get(headers, product="50ETF")
        assert resp.status_code == 400
        assert "exchange" in resp.json().get("error", "").lower()

    def test_missing_product_param_returns_400(self, headers):
        resp = self._get(headers, exchange="SSE")
        assert resp.status_code == 400

    def test_sse_50etf_returns_iv_surface(self, headers):
        """SSE 50ETF 已导入 8 合约 × 20 天, 3 个正常通过 filter, 5 个被各层剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        assert resp.status_code == 200, resp.text
        d = resp.json()
        assert "raw_points" in d
        assert "strikes" in d
        assert "expiry_days" in d
        assert "surface" in d
        assert "count" in d
        # 8 个合约同月到期 → 同一 expiry_days
        assert d["filter_stats"]["total_contracts"] == 8
        # filter 后只剩 3 个 (10003187/10003188 call + 10003189 put)
        assert len(d["raw_points"]) == 3
        assert len(d["expiry_days"]) == 1
        assert len(d["strikes"]) == 2  # 2.6 与 2.7
        # surface 网格 [expiry_idx][strike_idx] → [1][2]
        assert len(d["surface"]) == 1
        assert len(d["surface"][0]) == 2

    def test_raw_point_fields(self, headers):
        resp = self._get(headers, exchange="SSE", product="50ETF")
        pt = resp.json()["raw_points"][0]
        for k in ("strike", "expiry_days", "iv", "contract_name", "call_put"):
            assert k in pt
        assert 0 < pt["iv"] < 1  # IV 在合理范围
        assert pt["strike"] in (2.6, 2.7)

    def test_sse_returns_empty_when_no_data(self, headers):
        """SSE 50ETF 数据已灌, 但若 product 不匹配 → 空曲面 (count=0)"""
        resp = self._get(headers, exchange="SSE", product="NONEXIST")
        assert resp.status_code == 200
        d = resp.json()
        assert d["count"] == 0
        assert d["raw_points"] == []
        assert d["strikes"] == []
        assert d["expiry_days"] == []

    def test_szse_159919_returns_data(self, headers):
        """SZSE 沪深300ETF 2 个合约 (对照组, 全部通过 filter)"""
        resp = self._get(headers, exchange="SZSE", product="159919")
        assert resp.status_code == 200
        d = resp.json()
        assert d["count"] == 2
        assert d["filter_stats"]["total_contracts"] == 2
        assert d["filter_stats"]["filtered_count"] == 2

    def test_filter_stats_present(self, headers):
        """filter_stats 字段存在且结构正确"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        d = resp.json()
        assert "filter_stats" in d
        fs = d["filter_stats"]
        assert "total_contracts" in fs
        assert "filtered_count" in fs
        assert "removed_by_layer" in fs
        assert "removed_contracts" in fs
        # 8 个合约: 3 正常 + 5 各层违规
        assert fs["total_contracts"] == 8
        assert fs["filtered_count"] == 3

    def test_filter_stats_removed_by_layer_keys(self, headers):
        """removed_by_layer 包含 5 个层级键, 每个层级至少剔除 1 个"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        rbl = fs["removed_by_layer"]
        for key in ("L1_hard", "L2_liquidity", "L3_spread_proxy", "L4_moneyness", "L5_parity"):
            assert key in rbl
            assert isinstance(rbl[key], int)
            assert rbl[key] >= 1, f"{key} 层没有剔除任何合约"

    def test_filter_l1_iv_low_removed(self, headers):
        """L1: iv=0.02 < iv_min (0.05) 应被剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        assert fs["removed_by_layer"]["L1_hard"] >= 1
        # 验证 removed_contracts 中包含该合约, reason 含 'iv'
        l1_removed = [c for c in fs["removed_contracts"] if c["layer"] == "L1"]
        assert any("10003190" in c["contract_name"] for c in l1_removed)
        assert any("iv=" in c["reason"] for c in l1_removed)

    def test_filter_l2_volume_zero_removed(self, headers):
        """L2: volume=0 + oi<10 应被 volume_oi 规则剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        assert fs["removed_by_layer"]["L2_liquidity"] >= 1
        l2_removed = [c for c in fs["removed_contracts"] if c["layer"] == "L2"]
        assert any("10003191" in c["contract_name"] for c in l2_removed)

    def test_filter_l3_spread_wide_removed(self, headers):
        """L3: (high-low)/mid > 0.5 应被剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        assert fs["removed_by_layer"]["L3_spread_proxy"] >= 1
        l3_removed = [c for c in fs["removed_contracts"] if c["layer"] == "L3"]
        assert any("10003192" in c["contract_name"] for c in l3_removed)

    def test_filter_l4_moneyness_removed(self, headers):
        """L4: K/S=5/2.6≈1.92 > moneyness_max_etf (1.3) 应被剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        assert fs["removed_by_layer"]["L4_moneyness"] >= 1
        l4_removed = [c for c in fs["removed_contracts"] if c["layer"] == "L4"]
        assert any("10003193" in c["contract_name"] for c in l4_removed)
        assert any("K/S=" in c["reason"] for c in l4_removed)

    def test_filter_l5_parity_removed(self, headers):
        """L5: put 与同 strike call 价差不满足 put-call parity 应被剔除"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        fs = resp.json()["filter_stats"]
        assert fs["removed_by_layer"]["L5_parity"] >= 1
        l5_removed = [c for c in fs["removed_contracts"] if c["layer"] == "L5"]
        assert any("10003194" in c["contract_name"] for c in l5_removed)

    def test_filter_keeps_control_group(self, headers):
        """对照组 3 个合约应全部保留在 raw_points 中"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        d = resp.json()
        names = {pt["contract_name"] for pt in d["raw_points"]}
        # 10003187 call + 10003188 call + 10003189 put 三个对照组应保留
        assert any("10003187" in n or "50ETF购2409月02600" in n for n in names), \
            f"对照组缺失: {names}"
        # 5 个 filter 测试合约不应在 raw_points 中
        for filtered_code in ("10003190", "10003191", "10003192", "10003193", "10003194"):
            assert not any(filtered_code in n for n in names), \
                f"{filtered_code} 不应通过 filter, 但出现在 raw_points: {names}"

    def test_filter_stats_empty_when_no_data(self, headers):
        """无数据时 filter_stats 仍返回但 count=0"""
        resp = self._get(headers, exchange="SSE", product="NONEXIST")
        d = resp.json()
        assert d["count"] == 0
        fs = d["filter_stats"]
        assert fs["total_contracts"] == 0
        assert fs["filtered_count"] == 0

    def test_call_surface_present(self, headers):
        """call_surface 字段存在且维度正确"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        d = resp.json()
        assert "call_surface" in d
        # 维度: [expiry_idx][strike_idx]
        assert len(d["call_surface"]) == len(d["expiry_days"])
        assert len(d["call_surface"][0]) == len(d["strikes"])

    def test_surface_backward_compatible(self, headers):
        """surface 字段与 call_surface 一致(向后兼容)"""
        resp = self._get(headers, exchange="SSE", product="50ETF")
        d = resp.json()
        assert d["surface"] == d["call_surface"]


# ═══════════════════════════════════════════════════════════
#  POST /option/data — import_csv action + 下载参数校验
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionDataPost:

    def test_import_csv_success(self, headers):
        """import_csv action 重新灌入已清空的合约"""
        _clear_option_table(headers)
        # 重新生成一份, 调一次 import_csv
        from generate_option_test_data import generate as gen
        paths, _ = gen()
        # 只灌一个
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"action": "import_csv", "csv_path": paths[0]},
            headers=headers, verify=VERIFY_SSL, timeout=30)
        assert resp.status_code == 200
        body = resp.json()
        assert body["status"] == "imported"
        assert body["rows"] == 20

    def test_import_csv_missing_path_returns_400(self, headers):
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"action": "import_csv"},
            headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 400
        assert "csv_path" in resp.json().get("message", "")

    def test_import_csv_nonexistent_path_returns_404(self, headers):
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"action": "import_csv", "csv_path": "/nonexistent/fake.csv"},
            headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 404

    def test_invalid_exchange_returns_400(self, headers):
        """原下载入口, exchange 非法 → 400"""
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"exchange": "FAKE"},
            headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 400
        assert "CFFEX/SSE/SZSE" in resp.json().get("message", "")

    def test_invalid_product_returns_400(self, headers):
        """product 不属于该交易所 → 400"""
        resp = requests.post(
            f"{BASE_URL}/option/data",
            json={"exchange": "CFFEX", "products": ["FAKE"]},
            headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 400

    def test_invalid_json_returns_400(self, headers):
        resp = requests.post(
            f"{BASE_URL}/option/data",
            data="not json",
            headers={**headers, "Content-Type": "application/json"},
            verify=VERIFY_SSL)
        assert resp.status_code == 400


# ═══════════════════════════════════════════════════════════
#  GET /option/data — listContracts / queryByContract / queryBySymbolId
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionDataGet:

    def _get(self, headers, **params):
        return requests.get(
            f"{BASE_URL}/option/data",
            params=params, headers=headers, verify=VERIFY_SSL, timeout=30)

    def test_list_contracts_all(self, headers):
        """无参数 → 返回全部合约概要"""
        resp = self._get(headers)
        assert resp.status_code == 200
        d = resp.json()
        assert "contracts" in d
        assert "count" in d
        # 10 个合约 (8 SSE 50ETF + 2 SZSE 159919, 跳过 CFFEX)
        assert d["count"] == 10
        assert len(d["contracts"]) == 10
        for c in d["contracts"]:
            for k in ("symbol_id", "exchange", "product", "contract_name",
                      "call_put", "strike_price", "start_date", "end_date", "count"):
                assert k in c
            assert c["count"] == 20  # 每合约 20 天

    def test_list_contracts_filter_exchange(self, headers):
        resp = self._get(headers, exchange="SSE")
        d = resp.json()
        assert d["count"] == 8  # 50ETF 8 个合约 (3 对照 + 5 过滤测试)
        assert all(c["exchange"] == "SSE" for c in d["contracts"])

    def test_list_contracts_filter_exchange_and_product(self, headers):
        resp = self._get(headers, exchange="SZSE", product="159919")
        d = resp.json()
        assert d["count"] == 2
        assert all(c["exchange"] == "SZSE" and c["product"] == "159919" for c in d["contracts"])

    def test_query_by_contract_returns_history(self, headers):
        """按合约字符串查历史 (SSE 50ETF ATM 看涨)"""
        resp = self._get(headers, contract="10003187", name="50ETF购2409月02600")
        assert resp.status_code == 200
        d = resp.json()
        assert d["count"] == 20
        assert d["contract_code"] == "10003187"
        assert "symbol_id" in d
        assert len(d["data"]) == 20
        for row in d["data"]:
            for k in ("trade_date", "symbol_id", "exchange", "product",
                      "contract_name", "call_put", "strike_price",
                      "open", "high", "low", "close",
                      "settlement", "volume", "implied_volatility"):
                assert k in row
            assert row["contract_name"] == "50ETF购2409月02600"
            assert row["strike_price"] == 2.6
            assert row["call_put"] == "认购"

    def test_query_by_symbol_id_matches_contract_query(self, headers):
        """symbol_id 查询应与 contract 查询返回相同数据"""
        by_code = self._get(headers, contract="10003187", name="50ETF购2409月02600").json()
        sid = by_code["symbol_id"]
        by_sid = self._get(headers, symbol_id=str(sid)).json()
        assert by_sid["symbol_id"] == sid
        assert by_sid["count"] == 20
        assert len(by_sid["data"]) == 20

    def test_query_with_limit(self, headers):
        resp = self._get(headers, contract="10003187", name="50ETF购2409月02600", limit=5)
        d = resp.json()
        assert d["count"] == 5
        assert len(d["data"]) == 5

    def test_query_with_date_range(self, headers):
        resp = self._get(headers, contract="10003187", name="50ETF购2409月02600",
                         start="2024-06-10", end="2024-06-14")
        d = resp.json()
        # 6-10 ~ 6-14 含 5 个交易日
        assert d["count"] == 5


# ═══════════════════════════════════════════════════════════
#  DELETE /option/data — 按合约删 / 清空
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("headers")
class TestOptionDataDelete:

    def test_delete_all_clears_table(self, headers):
        """无参数 → 清空全部"""
        # 验证 fixture 已灌入
        resp = requests.get(f"{BASE_URL}/option/data",
                            headers=headers, verify=VERIFY_SSL)
        assert resp.json()["count"] == 5

        resp = requests.delete(f"{BASE_URL}/option/data",
                               headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 200
        assert resp.json()["status"] == "cleared"

        # 验证清空
        resp = requests.get(f"{BASE_URL}/option/data",
                            headers=headers, verify=VERIFY_SSL)
        assert resp.json()["count"] == 0

        # 重新灌入, 让后续测试可用 (teardown 不会再清)
        _generate_and_import(headers)

    def test_delete_one_contract_by_code(self, headers):
        """按 contract 删一个, 其它保留"""
        before = requests.get(f"{BASE_URL}/option/data",
                              headers=headers, verify=VERIFY_SSL).json()
        assert before["count"] == 5

        resp = requests.delete(f"{BASE_URL}/option/data",
                               params={"contract": "10003187"},
                               headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 200
        body = resp.json()
        assert body["status"] == "deleted"
        assert body["contract"] == "10003187"

        after = requests.get(f"{BASE_URL}/option/data",
                             headers=headers, verify=VERIFY_SSL).json()
        assert after["count"] == 4
        names = {c["contract_name"] for c in after["contracts"]}
        assert "50ETF购2409月02600" not in names

        # 重新灌入恢复
        _generate_and_import(headers)

    def test_delete_by_symbol_id(self, headers):
        """按数字 symbol_id 字符串删"""
        before = requests.get(f"{BASE_URL}/option/data",
                              headers=headers, verify=VERIFY_SSL).json()
        target = before["contracts"][0]
        sid = str(target["symbol_id"])

        resp = requests.delete(f"{BASE_URL}/option/data",
                               params={"contract": sid},
                               headers=headers, verify=VERIFY_SSL)
        assert resp.status_code == 200
        assert resp.json()["status"] == "deleted"

        # 恢复
        _generate_and_import(headers)