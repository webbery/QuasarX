#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CUSUM 浮点敏感性诊断（只读、独立于 pytest、不需要 service 运行）

背景
----
test_cusum_calibrate.py::test_cpp_python_drift_calibrated 报：
    62/154 bars 的 C++ vs Python drift 相对偏差 >1%

假设：根因是 sigma 计算的浮点差异（C++ std::accumulate 顺序求和 vs
numpy pairwise summation），被 CUSUM 的"阈值判定 + 状态重置"放大为
变点时机偏移（首达时间的阶梯敏感性），而非逐 bar 的误差累积。

本脚本用同一份收益率数据验证该假设：
  实验 A  三种 sigma 求和方式（numpy / 顺序 / math.fsum 精确）的数值差异
  实验 B  三者产生的变点索引序列是否一致
  实验 C  对 sigma 施加相对扰动 δ，观察变点序列在哪个 δ 量级开始崩坏

判读
----
  δ ~ 1e-15（几个 ulp）即可改变变点序列  → 浮点敏感成立
      逐 bar 精确对比在原理上不可行，测试须改为容差 / 结构比较
  变点序列对 δ 极不敏感                → 根因不在浮点
      需排查集成层（CUSUMNode 的 continue 跳 bar / retKey 选择）
"""

import csv
import math
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover
    print("需要 numpy：pip install numpy")
    sys.exit(1)

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

# ── 与测试一致的常量 ──────────────────────────────────────────
DATA_DIR = Path(r"D:\UnixlikePrograms\Ubuntu24\QuasarX\service\build\data")
HFQ_DIR = DATA_DIR / "A_hfq"
SYMBOL = "sz.900007"

CALIB_PERIOD = 30
MIN_OBS = 10
LAMBDA = 0.5
THRESHOLD = 4.0
THRESHOLD_CAP = 10.0


# ── 数据加载（与测试 tool / test_cusum_calibrate 一致）────────
def load_prices(symbol):
    csv_path = HFQ_DIR / f"{symbol}.csv"
    if not csv_path.exists():
        raise FileNotFoundError(csv_path)
    prices = []
    with open(csv_path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        close_idx = next(i for i, h in enumerate(header)
                         if h.strip().lower() == "close")
        for row in reader:
            if len(row) > close_idx:
                try:
                    prices.append(float(row[close_idx]))
                except ValueError:
                    continue
    return prices


def compute_returns(prices):
    # 与测试一致：np.log
    return [float(np.log(prices[i] / prices[i - 1])) if prices[i - 1] != 0 else 0.0
            for i in range(1, len(prices))]


# ── 三种校准（mu, sigma）求和方式 ────────────────────────────
def calib_numpy(xs):
    """当前 Python 参考实现：numpy pairwise summation"""
    return float(np.mean(xs)), float(np.std(xs, ddof=0))


def calib_sequential(xs):
    """C++ std::accumulate 风格：朴素顺序求和"""
    n = len(xs)
    s = 0.0
    for x in xs:
        s += x
    mean = s / n
    sq = 0.0
    for x in xs:
        d = x - mean
        sq += d * d
    return mean, math.sqrt(sq / n)


def calib_fsum(xs):
    """高精度参考：math.fsum（精确求和），作为 ground truth"""
    n = len(xs)
    mean = math.fsum(xs) / n
    sq = math.fsum((x - mean) ** 2 for x in xs)
    return mean, math.sqrt(sq / n)


# ── CUSUM 参考实现（可注入校准函数 + sigma 扰动）─────────────
class Detector:
    def __init__(self, calib_fn, sigma_scale=1.0, mu_scale=1.0):
        self.calib_fn = calib_fn
        self.sigma_scale = sigma_scale
        self.mu_scale = mu_scale
        self.reset()

    def reset(self):
        self.mu = 0.0
        self.sigma = 1.0
        self.s_pos = self.s_neg = 0.0
        self.count = 0
        self.calibrated = False
        self.buf = []
        self.change_points = []

    def _step(self, ret):
        k = LAMBDA * self.sigma
        drift = ret - self.mu
        self.s_pos = max(0.0, self.s_pos + drift - k)
        self.s_neg = max(0.0, self.s_neg - drift - k)
        cp = False
        if self.count >= MIN_OBS:
            n = max(self.count, 1)
            h = THRESHOLD * self.sigma * (n ** 0.5)
            if THRESHOLD_CAP > 0:
                h = min(h, THRESHOLD_CAP * self.sigma)
            if max(self.s_pos, self.s_neg) > h:
                cp = True
                self.s_pos = 0.0
                self.s_neg = 0.0
                self.change_points.append(self.count - 1)
        return self.s_pos - self.s_neg, cp

    def update(self, ret):
        self.count += 1
        if CALIB_PERIOD > 0 and not self.calibrated:
            self.buf.append(ret)
            if len(self.buf) >= CALIB_PERIOD:
                mu, sigma = self.calib_fn(self.buf)
                self.mu = mu * self.mu_scale
                self.sigma = max(sigma * self.sigma_scale, 1e-10)
                self.calibrated = True
                self.s_pos = self.s_neg = 0.0
                for r in self.buf:
                    self._step(r)
                self.buf.clear()
            return self.s_pos - self.s_neg, False
        return self._step(ret)


def run(calib_fn, sigma_scale=1.0, mu_scale=1.0):
    det = Detector(calib_fn, sigma_scale=sigma_scale, mu_scale=mu_scale)
    drifts = []
    for r in RETURNS:
        d, _ = det.update(r)
        drifts.append(d)
    return det, drifts


def first_diff(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return None


def count_diff(a, b):
    return sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i])


def count_rel_diff(a, b, rel=0.01):
    """测试口径：相对差异 > rel 的 bar 数（量级差异，排除 ulp 噪声）"""
    n = 0
    for x, y in zip(a, b):
        if abs(x) < 1e-8 and abs(y) < 1e-8:
            continue
        denom = max(abs(x), abs(y), 1e-10)
        if abs(x - y) / denom > rel:
            n += 1
    return n


# ── 主流程 ───────────────────────────────────────────────────
PRICES = load_prices(SYMBOL)
RETURNS = compute_returns(PRICES)

print("=" * 78)
print(f"CUSUM 浮点敏感性诊断   symbol={SYMBOL}")
print(f"bars={len(PRICES)}  returns={len(RETURNS)}  "
      f"calibrate_period={CALIB_PERIOD}  min_obs={MIN_OBS}")
print("=" * 78)

# 实验 A：三种 sigma 的数值差异
calib_window = RETURNS[:CALIB_PERIOD]
mu_np, sig_np = calib_numpy(calib_window)
mu_sq, sig_sq = calib_sequential(calib_window)
mu_fs, sig_fs = calib_fsum(calib_window)

print("\n[实验 A] 校准窗口 (前 %d 个收益率) 的 mu / sigma" % CALIB_PERIOD)
print(f"  numpy   : mu={mu_np!r:26} sigma={sig_np!r:26} hex={sig_np.hex()}")
print(f"  sequential: mu={mu_sq!r:26} sigma={sig_sq!r:26} hex={sig_sq.hex()}")
print(f"  fsum    : mu={mu_fs!r:26} sigma={sig_fs!r:26} hex={sig_fs.hex()}")
print(f"  |sigma_np - sigma_seq| = {abs(sig_np - sig_sq):.3e}  "
      f"(relative {abs(sig_np - sig_sq) / sig_fs:.3e})")
print(f"  |sigma_np - sigma_fsum| = {abs(sig_np - sig_fs):.3e}  "
      f"(relative {abs(sig_np - sig_fs) / sig_fs:.3e})")
print(f"  |sigma_seq - sigma_fsum| = {abs(sig_sq - sig_fs):.3e}  "
      f"(relative {abs(sig_sq - sig_fs) / sig_fs:.3e})")

# 实验 B：三种求和方式的变点序列对比
print("\n[实验 B] 三种 sigma 求和方式 → 变点序列 / drift 序列")
_, drifts_np = run(calib_numpy)
_, drifts_sq = run(calib_sequential)
_, drifts_fs = run(calib_fsum)

print(f"  numpy      : change_points={len(run(calib_numpy)[0].change_points):3d}  "
      f"max_drift={max(abs(x) for x in drifts_np):.6e}")
print(f"  sequential : change_points={len(run(calib_sequential)[0].change_points):3d}  "
      f"max_drift={max(abs(x) for x in drifts_sq):.6e}")
print(f"  fsum       : change_points={len(run(calib_fsum)[0].change_points):3d}  "
      f"max_drift={max(abs(x) for x in drifts_fs):.6e}")

fd = first_diff(drifts_np, drifts_sq)
print(f"  numpy vs sequential : 首个 drift 差异 bar={fd}  "
      f"差异 bar 数={count_diff(drifts_np, drifts_sq)}/{len(drifts_np)}")
fd = first_diff(drifts_np, drifts_fs)
print(f"  numpy vs fsum       : 首个 drift 差异 bar={fd}  "
      f"差异 bar 数={count_diff(drifts_np, drifts_fs)}/{len(drifts_np)}")
fd = first_diff(drifts_sq, drifts_fs)
print(f"  sequential vs fsum  : 首个 drift 差异 bar={fd}  "
      f"差异 bar 数={count_diff(drifts_sq, drifts_fs)}/{len(drifts_sq)}")

# 实验 C：sigma 相对扰动 → 变点序列崩坏阈值
print("\n[实验 C] sigma 相对扰动 δ → 变点序列变化（基于 numpy 基线）")
print(f"  {'δ':>10}  {'变点数':>7}  {'首个差异':>10}  {'!=计数':>9}  {'>1%计数':>9}")
base_det, base_drifts = run(calib_numpy, 1.0)
base_cps = base_det.change_points
marked = False
for delta in [0.0, 1e-16, 1e-15, 1e-14, 1e-13, 1e-12, 1e-11,
              1e-10, 1e-9, 1e-8, 1e-6]:
    det, drifts = run(calib_numpy, 1.0 + delta)
    fd = first_diff(base_drifts, drifts)
    nd = count_diff(base_drifts, drifts)
    flag = ""
    if fd is not None and not marked:
        flag = "  <-- 首个 ulp 差异"
        marked = True
    nrel = count_rel_diff(base_drifts, drifts)
    print(f"  {delta:>10.0e}  {len(det.change_points):>7d}  "
          f"{str(fd):>10}  {nd:>9d}  {nrel:>9d}{flag}")

# 实验 D：mu 相对扰动 → drift 序列变化（sigma 固定）
print("\n[实验 D] mu 相对扰动 δ → drift 序列变化（sigma 固定为 numpy 基线）")
print(f"  baseline mu = {mu_np!r}   sigma = {sig_np!r}")
print(f"  {'δ_mu':>10}  {'|Δmu|':>10}  {'首个差异':>10}  {'!=计数':>9}  {'>1%计数':>9}")
marked_mu = False
for delta in [0.0, 1e-16, 1e-15, 1e-14, 1e-13, 1e-12, 1e-11, 1e-10, 1e-9]:
    _, drifts = run(calib_numpy, 1.0, 1.0 + delta)
    fd = first_diff(base_drifts, drifts)
    nd = count_diff(base_drifts, drifts)
    flag = ""
    if fd is not None and not marked_mu:
        flag = "  <-- 首个 ulp 差异"
        marked_mu = True
    nrel = count_rel_diff(base_drifts, drifts)
    print(f"  {delta:>10.0e}  {abs(mu_np * delta):>10.2e}  "
          f"{str(fd):>10}  {nd:>9d}  {nrel:>9d}{flag}")

# 实验 E：回测起始日期平移 → 变点序列变化（校准窗口随起点移动）
print("\n[实验 E] 回测起始日期平移 k 天 → 变点序列（对比 k=0 基线）")
print(f"  {'k':>4}  {'bars':>5}  {'变点数':>6}  {'重叠区base':>10}  "
      f"{'k版变点':>8}  {'对称匹配率':>10}")


def run_on(slice_returns):
    det = Detector(calib_numpy)
    for r in slice_returns:
        det.update(r)
    return det.change_points


def sym_match_rate(a, b, tol=3):
    """对称匹配率：双向最近邻匹配取平均（避免一对多虚高）"""
    if not a and not b:
        return 1.0
    fwd = sum(1 for x in a if any(abs(x - y) <= tol for y in b)) / len(a)
    bwd = sum(1 for y in b if any(abs(x - y) <= tol for x in a)) / len(b)
    return (fwd + bwd) / 2.0


base_cps_e = run_on(RETURNS)
for k in [0, 1, 2, 3, 5, 10, 20, 30, 60]:
    cps_k = run_on(RETURNS[k:])
    cps_shift = [c + k for c in cps_k]
    base_in = [c for c in base_cps_e if c >= k]
    rate = sym_match_rate(base_in, cps_shift)
    print(f"  {k:>4d}  {len(RETURNS) - k:>5d}  {len(cps_k):>6d}  "
          f"{len(base_in):>10d}  {len(cps_shift):>8d}  {rate:>9.1%}")

# 实验 F：测试判据自检（np vs seq 精确模拟 C++ vs Python 的 mu 求和差异）
print("\n[实验 F] 测试判据自检（np vs seq = C++ vs Python 的 mu 差异）")
_det_np, py_d = run(calib_numpy)
_det_sq, cpp_d = run(calib_sequential)
py_c = _det_np.change_points
cpp_c = _det_sq.change_points
_same = sum(1 for x, y in zip(py_c, cpp_c) if x == y)
print(f"  [debug] != 差异 bar={count_diff(py_d, cpp_d)}/{len(py_d)}  "
      f">1% 差异 bar={count_rel_diff(py_d, cpp_d)}/{len(py_d)}  "
      f"变点 py={len(py_c)} cpp={len(cpp_c)} 索引相同={_same}")

base_n = max(len(py_c), len(cpp_c), 1)
d1 = abs(len(py_c) - len(cpp_c)) / base_n
print(f"  判据1 变点数差异 {d1:>6.1%} (≤10%)   "
      f"P={len(py_c)} C={len(cpp_c)}  {'PASS' if d1 <= 0.10 else 'FAIL'}")

for q in (10, 30, 50, 70, 90):
    pq, cq = int(np.percentile(py_c, q)), int(np.percentile(cpp_c, q))
    print(f"  判据2 {q:>2}% 分位 {abs(pq - cq):>4d} bars (≤10)  "
          f"P={pq:>3} C={cq:>3}  {'PASS' if abs(pq - cq) <= 10 else 'FAIL'}")

for q in (50, 90):
    pm = float(np.percentile([abs(x) for x in py_d], q))
    cm = float(np.percentile([abs(x) for x in cpp_d], q))
    rel = abs(pm - cm) / max(cm, 1e-12)
    print(f"  判据3 |drift| {q:>2}% rel {rel:>6.1%} (≤25%)   "
          f"P={pm:.3e} C={cm:.3e}  {'PASS' if rel <= 0.25 else 'FAIL'}")

_num = sum(abs(a - b) for a, b in zip(py_d, cpp_d))
_den = sum(max(abs(a), abs(b)) for a, b in zip(py_d, cpp_d))
rel_l1 = _num / _den if _den > 0 else 0.0
print(f"  判据4 相对 L1 {rel_l1:>9.1%} (≤50%)   "
      f"{'PASS' if rel_l1 <= 0.50 else 'FAIL'}")

# 实验 G：模拟 C++ 首 bar NaN 污染 → CUSUMNode drift 是否恒为 0
print("\n[实验 G] 模拟 C++ 首 bar NaN（FunctionNode::Return 首 bar=NaN，CUSUMNode 未过滤）")
_det_nan = Detector(calib_numpy)
_drifts_nan = [_det_nan.update(x)[0] for x in [float("nan")] + RETURNS]
_nz_nan = sum(1 for x in _drifts_nan if abs(x) > 1e-12)
print(f"  C++(NaN 版): len={len(_drifts_nan)} (Python={len(RETURNS)})  "
      f"非零 drift bar={_nz_nan}  mu={_det_nan.mu}  sigma={_det_nan.sigma}")

_start = 45
_mm = 0
for _i in range(_start, len(RETURNS)):
    _pv = py_d[_i]
    _cv = _drifts_nan[_i] if _i < len(_drifts_nan) else 0.0
    if abs(_pv) < 1e-8 and abs(_cv) < 1e-8:
        continue
    _denom = max(abs(_pv), abs(_cv), 1e-10)
    if abs(_pv - _cv) / _denom > 0.01:
        _mm += 1
print(f"  测试口径 mismatch (bar>={_start}) = {_mm}/{len(RETURNS)-_start}  "
      f"(历史测试报 62/154)")

print("\n[结论判读]")
print("  · 实验 A：sigma 三种求和方式 bit-exact → sigma 求和方式不是差异源")
print("  · 实验 B/F：np vs seq 的 drift 仅 ulp 级差异（!= 20/199，但 >1% = 0），")
print("    且 CUSUM 变点索引 86/86 完全相同")
print("    → 校准参数的浮点求和差异**不足以**造成 >1% 的 drift 偏差")
print("  · 实验 C/D：即使 σ/mu 扰动 δ=1e-6/1e-9，>1% 差异仍为 0 且不错位")
print("    → 之前的『崩坏阈值 1e-15』是 != 计数把 ulp 噪声误当分岔，结论作废")
print("  · 实验 E：校准窗口平移 k 天 → 变点数 ±30%（真实的起点依赖，因 mu/sigma 实质变化）")
print("  · 未解问题：历史测试报 C++ vs Python 62/154 bars >1% 偏差，")
print("    但浮点求和差异解释不了 → 存在实质（非浮点）差异，候选：")
print("      (a) C++ 读到的收益率序列与 CSV close 推导的不同")
print("      (b) C++ detector 的 _count 与 Python 错位（warmup / 跳 bar）")
print("      (c) retKey 选择到了错误的上游字段")
print("    需用实际 DebugNode CSV 定位（跑一次回测）。")
print("=" * 78)
