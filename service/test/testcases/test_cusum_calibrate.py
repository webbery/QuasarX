#!/usr/bin/env python3
"""
CUSUM calibrate_period C++ vs Python 对齐测试

通过回测 API 运行含 CUSUMNode(calibrate_period=T) 的策略，
读取 DebugNode CSV 输出，与 Python 参考实现逐 bar 对比。

用法:
  pytest test_cusum_calibrate.py -v
"""

import pytest
import requests
import json
import numpy as np
import csv
from pathlib import Path
from typing import List

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

import sys
sys.path.insert(0, str(Path(__file__).parent))
from tool import _DATA_DIR as SERVICE_DATA_DIR
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"


# ============================================================
# Python 参考实现（与 C++ CUSUMDetector 新实现对齐）
# ============================================================

class CUSUMDetectorRef:
    """带自适应校准的 CUSUM（与 C++ 对齐）"""

    def __init__(self, mu=0.0, sigma=1.0, lambda_=0.5, threshold=4.0,
                 min_obs=30, calibrate_period=30):
        self.mu = mu
        self.sigma = sigma
        self.lambda_ = lambda_
        self.threshold = threshold
        self.min_obs = min_obs
        self.calibrate_period = calibrate_period
        self.reset()

    def reset(self):
        self.s_pos = self.s_neg = 0.0
        self.count = 0
        self.calibrated = False
        self.calib_buffer = []

    def calibrate(self, returns):
        self.mu = float(np.mean(returns))
        self.sigma = float(np.std(returns, ddof=0))
        if self.sigma < 1e-10:
            self.sigma = 1e-10
        self.calibrated = True
        self.s_pos = self.s_neg = 0.0

    def _step(self, ret):
        k = self.lambda_ * self.sigma
        drift = ret - self.mu
        self.s_pos = max(0.0, self.s_pos + drift - k)
        self.s_neg = max(0.0, self.s_neg - drift - k)
        return self.s_pos - self.s_neg

    def update(self, ret):
        self.count += 1
        if self.calibrate_period > 0 and not self.calibrated:
            self.calib_buffer.append(ret)
            if len(self.calib_buffer) >= self.calibrate_period:
                self.calibrate(self.calib_buffer)
                for r in self.calib_buffer:
                    self._step(r)
                self.calib_buffer.clear()
            return self.s_pos - self.s_neg
        return self._step(ret)

    def detect_batch(self, returns):
        self.reset()
        return [self.update(r) for r in returns]


# ============================================================
# 辅助
# ============================================================

def load_prices(symbol: str) -> List[float]:
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        return []
    prices = []
    with open(csv_path) as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if not header:
            return []
        close_idx = next(i for i, h in enumerate(header) if h.strip().lower() == "close")
        for row in reader:
            if len(row) > close_idx:
                try:
                    prices.append(float(row[close_idx]))
                except ValueError:
                    continue
    return prices


def compute_returns(prices):
    return [np.log(prices[i] / prices[i - 1]) if prices[i - 1] != 0 else 0.0
            for i in range(1, len(prices))]


def make_cusum_strategy(symbol_api: str, calibrate_period: int, debug_label: str):
    """Input → Return(1) → CUSUM(calibrate_period=T) → Debug"""
    return {
        "name": f"test_cusum_cal_{calibrate_period}",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "数据", "nodeType": "input",
                       "params": {"code": {"value": [symbol_api]}}}},
            {"id": "2", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "ret1", "nodeType": "function",
                       "params": {"method": {"value": "Return"},
                                  "range": {"value": "1d"}}}},
            {"id": "3", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "cusum", "nodeType": "cusum",
                       "params": {
                           "mode": {"value": "changepoint"},
                           "lambda": {"value": 0.5},
                           "threshold_multiplier": {"value": 4.0},
                           "min_obs": {"value": 10},
                           "mu": {"value": 0.0},
                           "sigma": {"value": 1.0},
                           "cooldown": {"value": 0},
                           "calibrate_period": {"value": calibrate_period},
                       }}},
            {"id": "4", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": debug_label, "nodeType": "debug", "params": {}}},
            {"id": "5", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "信号", "nodeType": "signal",
                       "params": {"buy": {"value": "close > 0"},
                                  "sell": {"value": "close < 0"},
                                  "allowShort": {"value": False}}}},
            {"id": "6", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "组合", "nodeType": "portfolio", "params": {}}},
            {"id": "7", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "执行", "nodeType": "execution", "params": {}}},
        ],
        "edges": [
            {"id": "e1-2", "source": "1", "target": "2",
             "sourceHandle": "field-close", "targetHandle": "input-price"},
            {"id": "e2-3", "source": "2", "target": "3",
             "sourceHandle": "output", "targetHandle": "input-price"},
            {"id": "e3-4", "source": "3", "target": "4",
             "sourceHandle": "output", "targetHandle": "input"},
            {"id": "e1-5", "source": "1", "target": "5",
             "sourceHandle": "field-close", "targetHandle": "input-price"},
            {"id": "e5-6", "source": "5", "target": "6",
             "sourceHandle": "output", "targetHandle": "input"},
            {"id": "e6-7", "source": "6", "target": "7",
             "sourceHandle": "output", "targetHandle": "input"},
        ],
    }


def run_backtest_read_debug(token: str, strategy: dict, debug_label: str):
    """运行回测，读取 DebugNode CSV"""
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/backtest",
        json={"script": json.dumps(strategy), "validate": True},
        headers=headers, verify=VERIFY_SSL, timeout=300
    )
    assert resp.status_code == 200, f"Backtest failed: {resp.text[:300]}"

    debug_csv = SERVICE_DATA_DIR / "data" / "debug" / debug_label / "xgb_debug.csv"
    assert debug_csv.exists(), f"Debug CSV not found: {debug_csv}"

    with open(debug_csv) as f:
        return list(csv.DictReader(f))


# ============================================================
# 测试
# ============================================================

class TestCUSUMCalibrateAlign:
    """C++ vs Python calibrate_period 对齐（通过回测 API）"""

    @pytest.fixture(autouse=True)
    def setup(self, auth_token):
        self.token = auth_token
        self.symbol = "sz.900007"
        self.symbol_api = "900007.SZ"
        self.prices = load_prices(self.symbol)
        self.returns = compute_returns(self.prices) if len(self.prices) > 1 else []

    def test_cpp_python_drift_calibrated(self):
        """calibrate_period=30: C++ DebugNode drift vs Python 逐 bar 对比"""
        if len(self.returns) < 60:
            pytest.skip("数据不足")

        T = 30
        debug_label = "debug_cusum_cal30"
        strategy = make_cusum_strategy(self.symbol_api, T, debug_label)
        rows = run_backtest_read_debug(self.token, strategy, debug_label)

        det = CUSUMDetectorRef(calibrate_period=T, min_obs=10)
        py_drifts = det.detect_batch(self.returns[:len(rows)])

        drift_col = f"{self.symbol}.cusum.drift"
        assert drift_col in rows[0], \
            f"缺少列 {drift_col}，可用: {list(rows[0].keys())[:10]}"
        cpp_drifts = [float(r[drift_col]) for r in rows]

        start = T + 15
        mismatches = 0
        total = min(len(py_drifts), len(cpp_drifts)) - start
        for i in range(start, min(len(py_drifts), len(cpp_drifts))):
            py_v, cpp_v = py_drifts[i], cpp_drifts[i]
            if abs(py_v) < 1e-8 and abs(cpp_v) < 1e-8:
                continue
            denom = max(abs(py_v), abs(cpp_v), 1e-10)
            if abs(py_v - cpp_v) / denom > 0.01:
                mismatches += 1

        assert mismatches == 0, \
            f"C++ vs Python drift 偏差 >1%: {mismatches}/{total} bars"

    def test_cpp_python_drift_no_calibrate(self):
        """calibrate_period=0: drift 应恒为 0（C++ 和 Python 一致）"""
        if len(self.returns) < 60:
            pytest.skip("数据不足")

        debug_label = "debug_cusum_cal0"
        strategy = make_cusum_strategy(self.symbol_api, 0, debug_label)
        rows = run_backtest_read_debug(self.token, strategy, debug_label)

        drift_col = f"{self.symbol}.cusum.drift"
        cpp_drifts = [float(r[drift_col]) for r in rows]

        for i in range(30, len(cpp_drifts)):
            assert abs(cpp_drifts[i]) < 1e-10, \
                f"不校准 drift[{i}]={cpp_drifts[i]} 应恒为 0"
