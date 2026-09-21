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
                 min_obs=30, calibrate_period=30, threshold_cap=10.0):
        self.mu = mu
        self.sigma = sigma
        self.lambda_ = lambda_
        self.threshold = threshold
        self.min_obs = min_obs
        self.calibrate_period = calibrate_period
        self.threshold_cap = threshold_cap
        self.reset()

    def reset(self):
        self.s_pos = self.s_neg = 0.0
        self.count = 0
        self.calibrated = False
        self.calib_buffer = []
        self.change_points = []

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
        if self.count >= self.min_obs:
            n = max(self.count, 1)
            h = self.threshold * self.sigma * (n ** 0.5)
            if self.threshold_cap > 0:
                h = min(h, self.threshold_cap * self.sigma)
            if max(self.s_pos, self.s_neg) > h:
                self.s_pos = 0.0
                self.s_neg = 0.0
                self.change_points.append(self.count - 1)
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
        "id": f"test_cusum_cal_{calibrate_period}",
        "name": f"test_cusum_cal_{calibrate_period}",
        "version": 1,
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
                       "params": {"buy": {"value": "close[t] > 0"},
                                  "sell": {"value": "close[t] < 0"},
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
    """运行回测，读取 DebugNode CSV

    DebugNode::Done 实际写入路径为 <db>/debug/<strategy_id>/<label>.csv，
    因此目录取策略 id，文件名取节点 label（不再硬编码 xgb_debug.csv）。
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/backtest",
        json={"script": json.dumps(strategy), "validate": True},
        headers=headers, verify=VERIFY_SSL, timeout=300
    )
    assert resp.status_code == 200, f"Backtest failed: {resp.text[:300]}"

    strategy_id = strategy["id"]
    debug_csv = SERVICE_DATA_DIR / "debug" / strategy_id / f"{debug_label}.csv"
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
        # 必须用内部格式 sz.900007——QuoteInputNode::Init → to_symbol() 假设
        # 第一个 token 是交易所代码（system.cpp:654-656），外部格式 900007.SZ 会
        # 让 tokens.front()="900007" 落到 exchange_map().at() 抛 map::at 异常
        self.symbol_api = "sz.900007"
        self.prices = load_prices(self.symbol)
        self.returns = compute_returns(self.prices) if len(self.prices) > 1 else []

    def test_cpp_python_drift_calibrated(self):
        """calibrate_period=30: C++ vs Python 结构 + 容差比较

        根因（2026-09-22 定位并修复）：FunctionNode::Return 首 bar 返回 NaN，
        CUSUMNode 原先未过滤 NaN，污染了 calibrate 的 mean/sigma →
        std::max(0.0, NaN) 恒为 0 → C++ drift 全序列退化为 0（静默失败）。
        这解释了历史报告 62/154 bars 偏差。修复见 CUSUMNode::ProcessSingleAsset
        的 std::isfinite 过滤。

        C++ bar 序列首 bar 无收益率（写占位输出），Python 参考对齐该结构
        （首元素 0.0），两侧各 200 个元素。

        判据为"统计等价性"（4 项）：
          1. 变点总数相对差异 ≤ 10%      （结构：触发频率一致）
          2. 变点位置分位数差异 ≤ 10 bars（结构：时间分布一致）
          3. |drift| 幅值分位相对差异 ≤ 25%（容差：波动幅度一致）
          4. drift 相对 L1 距离 ≤ 50%    （容差：整体数值接近）
        """
        if len(self.returns) < 60:
            pytest.skip("数据不足")

        T = 30
        debug_label = "debug_cusum_cal30"
        strategy = make_cusum_strategy(self.symbol_api, T, debug_label)
        rows = run_backtest_read_debug(self.token, strategy, debug_label)

        # C++ bar 序列首 bar 无收益率（FunctionNode::Return 返回 NaN，CUSUMNode
        # 跳过 update 但写占位输出）；Python 参考对齐该结构：首元素占位 0.0
        det = CUSUMDetectorRef(calibrate_period=T, min_obs=10)
        py_drifts = [0.0] + det.detect_batch(self.returns[:len(rows) - 1])
        py_cps = list(det.change_points)

        drift_col = f"{self.symbol}.cusum.drift"
        assert drift_col in rows[0], \
            f"缺少列 {drift_col}，可用: {list(rows[0].keys())[:10]}"
        cpp_drifts = [float(r[drift_col]) for r in rows]

        n = min(len(py_drifts), len(cpp_drifts))
        py_drifts, cpp_drifts = py_drifts[:n], cpp_drifts[:n]

        # C++ 侧变点：优先用 signal 列（触发 bar = 1.0，可靠，见 CUSUMNode::InterpretSignal）
        # 退化方案：drift 由非零回落到 ~0 —— 会漏检（|ret-mu| ≤ k 时 drift 连续为 0，
        # 无法区分"重置"与"停在 0"），仅在 signal 列缺失时使用
        signal_col = f"{self.symbol}.cusum.signal"
        if signal_col in rows[0]:
            cpp_cps = [i for i, r in enumerate(rows[:n])
                       if float(r[signal_col]) > 0.5]
        else:
            cpp_cps = [i for i in range(1, n)
                       if abs(cpp_drifts[i]) <= 1e-8 < abs(cpp_drifts[i - 1])]

        # 判据 1：变点总数
        base_n = max(len(py_cps), len(cpp_cps), 1)
        assert abs(len(py_cps) - len(cpp_cps)) / base_n <= 0.10, \
            f"变点数差异 >10%: Python={len(py_cps)}, C++={len(cpp_cps)}"

        # 判据 2：变点位置分位数
        if py_cps and cpp_cps:
            for q in (10, 30, 50, 70, 90):
                pq = int(np.percentile(py_cps, q))
                cq = int(np.percentile(cpp_cps, q))
                assert abs(pq - cq) <= 10, \
                    f"变点 {q}% 分位差异 >10 bars: Python={pq}, C++={cq}"

        # 判据 3：|drift| 幅值分位
        py_abs = [abs(x) for x in py_drifts]
        cpp_abs = [abs(x) for x in cpp_drifts]
        for q in (50, 90):
            pm = float(np.percentile(py_abs, q))
            cm = float(np.percentile(cpp_abs, q))
            denom = max(cm, 1e-12)
            assert abs(pm - cm) / denom <= 0.25, \
                f"|drift| {q}% 分位相对差异 >25%: Python={pm:.6e}, C++={cm:.6e}"

        # 判据 4：相对 L1 距离
        num = sum(abs(a - b) for a, b in zip(py_drifts, cpp_drifts))
        den = sum(max(abs(a), abs(b)) for a, b in zip(py_drifts, cpp_drifts))
        rel_l1 = num / den if den > 0 else 0.0
        assert rel_l1 <= 0.50, f"drift 相对 L1 距离 {rel_l1:.1%} > 50%"

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
