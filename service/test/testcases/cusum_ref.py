#!/usr/bin/env python3
"""
CUSUM Python 参考实现（唯一来源）

镜像 C++ `service/src/Metric/CUSUMDetector.cpp` + `include/Metric/CUSUMDetector.h`。
所有涉及 CUSUM 的测试都必须复用本模块，禁止再各自复制 CUSUMDetectorRef ——
历史上有 4 份拷贝，语义已经互相漂移（例如某份缺少 warmup 期累积、某份缺少
threshold_cap）。

对齐检查表（改动 CUSUMDetector 时必须逐条复核）：

| C++ | 本模块 |
|-----|--------|
| `update()`: `++_count` | `update()`: `self.count += 1` |
| `if (_calibratePeriod > 0 && !_calibrated)` 缓冲 | 同 |
| 校准窗口 = `max(_calibratePeriod, _min_obs)` | `max(self.calibrate_period, self.min_obs)` |
| 缓冲未满时返回占位（change_point=false，s_pos/s_neg=0） | 同（返回 False，状态未变） |
| 校准后对缓冲逐个 `_step()` 重放 | 同 |
| `_step()`: `_count < _min_obs` 时**仍累积** s_pos/s_neg，仅抑制触发 | 同 |
| `k = λ·σ`, `drift = r - μ` | 同 |
| `h = min(thr·σ·√max(count,1), cap·σ)`（`cap>0` 时） | 同 |
| 触发 → `++_total_change_points`，`_last_change_index = _count-1`，`s_pos=s_neg=0` | 同 |
| `calibrate()`: `σ = √(Σ(r-μ)²/n)`（**ddof=0**），`σ<1e-10→1e-10` | 同 |
| `calibrate()` 重置 `s_pos/s_neg/_max_drift/_total_change_points` | 同 |
| `_max_drift = max(_max_drift, abs(s_pos - s_neg))`（触发前记录峰值） | 同 |

注意：`CUSUMDetector` 有 3 个调用方，语义并不相同，不要混用收益率类型：
  - `CUSUMNode`（策略节点）     → 对数收益率（FunctionNode::Return）
  - `BacktestContext::updateCUSUM` → 简单收益率 `(cur-prev)/prev`
  - `CUSUMHandler`（/cusum API） → EWMA 标准化后 σ=1 的收益率
"""

import csv
import math

import numpy as np


class CUSUMDetectorRef:
    """双侧 CUSUM（镜像 CUSUMDetector）

    Args:
        calibrate_period: 0 = 不校准（直接使用传入的 mu/sigma），与 C++ 一致
    """

    def __init__(self, mu=0.0, sigma=1.0, lambda_=0.5, threshold=4.0,
                 min_obs=30, calibrate_period=0, threshold_cap=10.0):
        self.mu = float(mu)
        self.sigma = float(sigma)
        self.lambda_ = float(lambda_)
        self.threshold = float(threshold)
        self.min_obs = int(min_obs)
        self.calibrate_period = int(calibrate_period)
        self.threshold_cap = float(threshold_cap)
        self.reset()

    def reset(self):
        self.s_pos = 0.0
        self.s_neg = 0.0
        self.count = 0
        self.max_drift = 0.0
        self.total_change_points = 0
        self.last_change_index = 0
        # 仅测试用：C++ 只暴露计数与最后索引，这里额外记录全部触发点便于断言
        self.change_points = []
        # 自适应校准状态
        self.calibrated = False
        self.calib_buffer = []

    # ---- 校准 ----

    def calibrate(self, returns):
        """镜像 CUSUMDetector::calibrate（朴素顺序求和，与 C++ std::accumulate 风格一致）"""
        n = len(returns)
        if n == 0:
            return
        s = 0.0
        for r in returns:
            s += r
        mu = s / n

        sq = 0.0
        for r in returns:
            d = r - mu
            sq += d * d
        sigma = math.sqrt(sq / n)

        self.mu = mu
        self.sigma = sigma
        if self.sigma < 1e-10:
            self.sigma = 1e-10
        self.calibrated = True

        self.s_pos = 0.0
        self.s_neg = 0.0
        self.max_drift = 0.0
        self.total_change_points = 0
        self.last_change_index = 0
        self.change_points = []

    # ---- 单步 ----

    def _compute_threshold(self):
        n = max(self.count, 1)
        h = self.threshold * self.sigma * math.sqrt(n)
        if self.threshold_cap > 0:
            h = min(h, self.threshold_cap * self.sigma)
        return h

    def _step(self, ret):
        """返回是否触发变点"""
        k = self.lambda_ * self.sigma
        drift = ret - self.mu

        self.s_pos = max(0.0, self.s_pos + drift - k)
        self.s_neg = max(0.0, self.s_neg - drift - k)

        # min_obs 保护期：仍然累积，只是不触发
        if self.count < self.min_obs:
            return False

        self.max_drift = max(self.max_drift, abs(self.s_pos - self.s_neg))

        h = self._compute_threshold()
        change_point = max(self.s_pos, self.s_neg) > h
        if change_point:
            self.total_change_points += 1
            self.last_change_index = self.count - 1
            self.change_points.append(self.count - 1)
            self.s_pos = 0.0
            self.s_neg = 0.0
        return change_point

    def update(self, ret):
        """镜像 CUSUMDetector::update，返回是否触发变点"""
        self.count += 1

        if self.calibrate_period > 0 and not self.calibrated:
            effective_period = max(self.calibrate_period, self.min_obs)
            self.calib_buffer.append(ret)
            if len(self.calib_buffer) >= effective_period:
                self.calibrate(self.calib_buffer)
                for r in self.calib_buffer:
                    self._step(r)
                self.calib_buffer = []
            return False

        return self._step(ret)

    # ---- 批量 ----

    def detect_batch(self, returns):
        """镜像 CUSUMDetector::detect_batch，返回逐步 drift（s_pos - s_neg）"""
        self.reset()
        drifts = []
        for r in returns:
            self.update(r)
            drifts.append(self.s_pos - self.s_neg)
        return drifts


# ============================================================
# 数据 / 收益率辅助
# ============================================================

def load_closes(csv_path):
    """读取行情 CSV 的 close 列（大小写不敏感）"""
    closes = []
    with open(csv_path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if not header:
            return closes
        idx = next((i for i, h in enumerate(header) if h.strip().lower() == "close"), None)
        if idx is None:
            raise ValueError(f"no 'close' column in {csv_path}: {header}")
        for row in reader:
            if len(row) > idx:
                try:
                    closes.append(float(row[idx]))
                except ValueError:
                    continue
    return closes


def log_returns(closes):
    """对数收益率，与 FunctionNode::Return 一致。

    返回长度 = len(closes) - 1。首 bar 无历史价格时 C++ 返回 NaN（本函数不产出该元素，
    由 node_bar_series 的 first_bar_empty 补位）；0/负价格会产生 ±inf/NaN，调用方
    需按 C++ 的 std::isfinite 语义过滤。
    """
    p = np.asarray(closes, dtype=float)
    if p.size < 2:
        return np.empty(0)
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.log(p[1:] / p[:-1])


def simple_returns(closes):
    """简单收益率 (p_t - p_{t-1}) / p_{t-1}，给 BacktestContext::updateCUSUM 路径用"""
    p = np.asarray(closes, dtype=float)
    if p.size < 2:
        return np.empty(0)
    prev = p[:-1]
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(prev != 0, (p[1:] - prev) / prev, np.nan)


def node_bar_series(det, returns, first_bar_empty=True):
    """产出与 C++ `CUSUMNode::ProcessSingleAsset` 逐 bar 对齐的 (s_pos, s_neg)。

    C++ 侧行为：
      - bar 0 无收益率（FunctionNode::Return 返回 NaN）→ 写占位（当前 s_pos/s_neg），
        不调用 detector.update
      - 非有限收益率（0 价格产生的 ±inf/NaN 等）→ 同上，不 update
      - 有限收益率 → update 后写真实值

    返回长度为 `len(returns) + 1`（first_bar_empty=True 时），与 DebugNode CSV 行数一致。
    """
    seq = list(returns)
    if first_bar_empty:
        seq = [None] + seq

    spos, sneg = [], []
    for r in seq:
        if r is None or not math.isfinite(r):
            spos.append(det.s_pos)
            sneg.append(det.s_neg)
            continue
        det.update(r)
        spos.append(det.s_pos)
        sneg.append(det.s_neg)
    return spos, sneg
