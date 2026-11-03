#!/usr/bin/env python3
"""统一统计实现：仓库内 IC / 显著性 / 多重检验 / 稳定性的唯一来源。

背景
----
仓库里长期存在多份同名统计实现，归一化方式并不一致，同一个指标在不同脚本
里会给出不同的 t 值，无法横向比较：

  - Newey-West t：4 份（tools/ml_metrics.py、tools/feature_screen.py、
    lab/train_xgb_fundamental.py、lab/analyze_universe_width.py）
  - 日频横截面 Spearman IC：5 份（宽表 / 长表 / pandas / numpy 各一份）

本模块把这些统计收敛成一份，并把口径写死在这里。所有调用方（生产训练链
和研究脚本）都从这里取，避免再出现"注释里声明同口径、代码里各写各的"。

Newey-West 口径
--------------
HAC 方差按"样本均值的方差"归一化：

    var = (1/n)·Σ e²  +  2·Σ_{l=1..L} (1 - l/(L+1))·(1/n)·Σ_t e_t·e_{t-l}
    se  = sqrt(var / n)

其中 e = x - mean(x)，L = lag。这一口径自 2026-09 起由 feature_screen 与
ml_metrics 共同使用。注意与另外两份旧实现区分：旧实现把自协方差求和不除 n、
方差除以 n(n-1)，得到的 t 值系统性偏小，同一份数据两套公式会给出不同结论。

两处口径差异（有意为之，不要"顺手统一"）
----------------------------------------
`newey_west_t`（IC 序列显著性）与 `series_mean_test`（回归系数序列均值检验）
共用同一个 `_hac_mean_stat`，因此 **t 值完全一致**。但两处差异仍在：

1. **p 值分布**：newey_west_t 用正态近似（erfc），series_mean_test 用
   student-t（不完全 Beta）。IC 序列动辄几百个交易日，正态近似足够；
   回归系数序列样本少，student-t 才不虚高。
2. **lag 下限**：newey_west_t 至少取一阶（min_lag=1，保持历史行为），
   series_mean_test 允许 L=0（min_lag=0，L=0 即退化成 IID）。

除这两点外，两边的 S / se / t 是逐字一致的。

数据布局
--------
本模块提供两套等价的数据布局，统计口径完全一致：

  - 宽表 (T, N)：x / y / mask 三个二维数组，T 为日期、N 为标的。
    `daily_cs_ic_wide` 面向"每个特征一张宽表"的筛选场景。
  - 长表 (M,)：score / future / date_codes 三个一维数组，M 为样本行。
    `daily_cs_ic_long` 面向"所有特征堆在一张表"的训练评估场景。

`daily_cs_ic_wide` 与 `daily_cs_ic_long` 在同一批数据上必须给出相同的 IC 值
（tools/test_ml_stats.py 有对拍用例）。两者的**对齐方式**不同：宽表返回与
输入等长的逐日 IC（当日有效样本不足时为 NaN），长表只返回有有效 IC 的日期
及其取值，比较时需按长表返回的日期码回取宽表对应位置。

只依赖 numpy，便于单测和被不同模型（XGBoost / CNN / Transformer）复用。
"""

from __future__ import annotations

import math
from statistics import NormalDist
from typing import Optional, Sequence, Tuple

import numpy as np

__all__ = [
    "num",
    "rankdata",
    "spearman_ic",
    "newey_west_t",
    "bh_qvalues",
    "bonferroni_t_crit",
    "row_ranks",
    "daily_cs_ic_wide",
    "daily_cs_ic_long",
    "pooled_ic",
    "ts_ic",
    "autocorr_1",
    "topk_excess",
    "fold_stats",
    "effective_gap",
    "MIN_FOLD_OBS",
    "ks_2samp",
    "psi",
    "block_bounds",
    # 回归内核（C++ service/src/Algorithms/regression.cpp 的镜像）
    "student_t_two_sided_p",
    "greedy_rank_select",
    "ols_fit",
    "series_mean_test",
    "build_group_codes",
    "winsorize_cross_section",
    "alternate_demean",
    "partial_residual",
]


# ============================================================
# 数值安全
# ============================================================

def num(value) -> Optional[float]:
    """JSON 安全的 float：NaN/Inf/不可转换 → None。"""
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if np.isfinite(v) else None


# ============================================================
# 秩与相关
# ============================================================

def rankdata(values) -> np.ndarray:
    """平均秩（并列取平均名次），用于 Spearman IC。"""
    x = np.asarray(values, dtype=float)
    n = len(x)
    if n == 0:
        return np.empty(0, dtype=float)
    order = np.argsort(x, kind="mergesort")
    ranks = np.empty(n, dtype=float)
    ranks[order] = np.arange(1, n + 1, dtype=float)
    sorted_x = x[order]
    i = 0
    while i < n:
        j = i + 1
        while j < n and sorted_x[j] == sorted_x[i]:
            j += 1
        if j - i > 1:
            ranks[order[i:j]] = (i + 1 + j) / 2.0
        i = j
    return ranks


def spearman_ic(x, y) -> float:
    """Spearman rank IC；有效样本不足 3 个或任一侧无变化时返回 NaN。"""
    a = np.asarray(x, dtype=float)
    b = np.asarray(y, dtype=float)
    m = np.isfinite(a) & np.isfinite(b)
    if int(m.sum()) < 3:
        return float("nan")
    ra = rankdata(a[m])
    rb = rankdata(b[m])
    if ra.std() < 1e-12 or rb.std() < 1e-12:
        return float("nan")
    return float(np.corrcoef(ra, rb)[0, 1])


# ============================================================
# Newey-West HAC 显著性
# ============================================================

def _hac_mean_stat(values, lag: int, min_lag: int = 0) -> Tuple[float, float, float]:
    """HAC 均值检验的核心统计量，返回 (mean, se, S)。

    口径（全仓库唯一，与 C++ regression.cpp::seriesMeanTest 逐字一致）：

        d_t     = x_t - mean(x)
        gamma_j = (1/n) * sum_t d_t * d_{t-j}      ← 除以 n，不是 n-j
        S       = gamma_0 + 2 * sum_{j=1..L} w_j * gamma_j,  w_j = 1 - j/(L+1)
        Var(mean) = S / n
        se      = sqrt(Var(mean))

    注意 gamma_j 的分母是 n。写成 ``(d[j:] * d[:-j]).mean()`` 会除以 n-j，
    与本模块头声明的公式、以及回归内核的口径都不一致，同一份数据会给出
    不同的 t 值。

    lag 的截断在函数内统一：``L = max(min_lag, min(lag, n-2))``。
    ``min_lag=0`` 是回归内核的口径（允许 L=0，即退化成 IID）；
    ``min_lag=1`` 是 newey_west_t 的历史口径（至少取一阶）。

    调用方各自决定 p 值分布与边界情形，本函数只给统计量。
    """
    x = np.asarray(values, dtype=float).ravel()
    x = x[np.isfinite(x)]
    n = len(x)
    if n == 0:
        return float("nan"), float("nan"), float("nan")

    m = float(x.mean())
    d = x - m
    S = float(np.sum(d * d)) / n

    L = max(int(min_lag), min(int(lag), n - 2))
    for j in range(1, L + 1):
        w = 1.0 - j / (L + 1.0)
        g = float(np.sum(d[j:] * d[:-j])) / n
        S += 2.0 * w * g

    se = math.sqrt(max(S, 0.0) / n) if S > 0.0 else 0.0
    return m, se, S


def newey_west_t(series, lag: int) -> Tuple[float, float]:
    """Newey-West HAC t 值和双侧 p 值（正态近似）。

    series：任意可转 float 的一维序列（list / np.ndarray / pd.Series）。
    lag：HAC 截断阶数，实际取 max(1, min(lag, n-2))。
    样本不足 10 个时返回 (nan, nan)。

    口径见模块头：var 按均值的方差归一化，se = sqrt(var / n)。
    自协方差分母是 n（与模块头公式、回归内核 series_mean_test 一致）。

    p 值用正态近似，与回归内核的 student-t 不同——见模块头"两处口径差异"。
    """
    x = np.asarray(series, dtype=float).ravel()
    x = x[np.isfinite(x)]
    n = len(x)
    if n < 10:
        return float("nan"), float("nan")

    mu, se, S = _hac_mean_stat(x, lag, min_lag=1)
    if S <= 1e-18:
        if abs(mu) < 1e-18:
            return 0.0, 1.0
        return math.copysign(float("inf"), mu), 0.0
    t = mu / se
    p = math.erfc(abs(t) / math.sqrt(2.0))
    return float(t), float(p)


# ============================================================
# 多重检验
# ============================================================

def bh_qvalues(pvals: Sequence[float], m_tests: Optional[int] = None) -> list:
    """Benjamini-Hochberg FDR q 值。

    m_tests 可大于本次候选数，用于粗略计入历史搜索；默认等于候选数。
    """
    p = np.asarray(pvals, dtype=float)
    m = int(m_tests) if m_tests and m_tests > 0 else len(p)
    q = np.full(len(p), np.nan)
    ok = np.isfinite(p)
    if not ok.any():
        return q.tolist()
    idx = np.where(ok)[0]
    order = idx[np.argsort(p[idx])]
    prev = 1.0
    for rank in range(len(order) - 1, -1, -1):
        i = order[rank]
        val = p[i] * m / (rank + 1)
        prev = min(prev, val)
        q[i] = min(1.0, max(0.0, prev))
    return q.tolist()


def bonferroni_t_crit(m_tests: int, alpha: float = 0.05) -> float:
    """Bonferroni 校正下的双侧 t 门槛（正态近似）。"""
    m = int(m_tests)
    if m <= 0:
        return float("nan")
    return float(NormalDist().inv_cdf(1.0 - alpha / (2.0 * m)))


# ============================================================
# 逐行平均秩
# ============================================================

def row_ranks(a) -> np.ndarray:
    """逐行平均秩（并列取平均名次），NaN 位置保持 NaN。

    a：二维数组 (T, N)，T 为日期、N 为标的。返回同形状的秩矩阵。

    实现说明：先按行排序，相同值构成的"并列组"共享平均名次。并列组的起点
    用 maximum.accumulate 一次扫出，组长度用相邻组起点之差得到，全程没有
    Python 循环，可以直接作用在 (T, N) 的大矩阵上。

    与 pandas 的 ``df.rank(axis=1)`` 口径一致（含并列取平均名次），
    tools/test_ml_stats.py 有对拍用例。
    """
    a = np.asarray(a, dtype=float)
    if a.ndim != 2:
        raise ValueError("row_ranks 需要二维数组")
    t, n = a.shape
    if n == 0:
        return np.full((t, n), np.nan)

    nan_mask = ~np.isfinite(a)
    # NaN 排到行尾。连续的 NaN 之间会互相"相等"从而落进同一组，
    # 但它们的秩最后会被统一置回 NaN，不影响有效值的名次。
    b = np.where(nan_mask, np.inf, a)
    order = np.argsort(b, axis=1, kind="stable")
    s = np.take_along_axis(b, order, axis=1)

    is_new = np.ones((t, n), dtype=bool)
    if n > 1:
        is_new[:, 1:] = s[:, 1:] != s[:, :-1]

    pos = np.arange(n)
    # 组起点：同一行内 is_new 为真的位置里最靠左的那个
    start = np.maximum.accumulate(np.where(is_new, pos[None, :], -1), axis=1)
    # 下一组起点：同一行内严格位于当前位置之后的第一个 is_new 位置
    rev_vals = np.where(is_new[:, ::-1], pos[None, :][:, ::-1], n)
    rev_min = np.minimum.accumulate(rev_vals, axis=1)
    next_start = np.concatenate(
        [rev_min[:, ::-1][:, 1:], np.full((t, 1), n)], axis=1)
    size = next_start - start

    # 1-based 平均名次：组内第一个是 start+1，最后一个是 start+size
    ranks_sorted = start + (size + 1) / 2.0
    out = np.full((t, n), np.nan)
    rows = np.arange(t)[:, None]
    out[rows, order] = ranks_sorted
    out[nan_mask] = np.nan
    return out


# ============================================================
# 日频横截面 Spearman IC
# ============================================================

def daily_cs_ic_wide(x, y, mask=None, min_names: int = 2) -> Tuple[np.ndarray, np.ndarray]:
    """日频横截面 Spearman IC（宽表布局）。

    x、y：(T, N) 二维数组，T 为日期、N 为标的。mask：(T, N) 布尔可选。
    返回 (ic, n_names)：
      ic      —— (T,) 每个交易日的横截面 IC，无效日为 NaN；
      n_names —— (T,) 每个交易日的有效标的数。

    有效样本 = x 有限 ∧ y 有限 ∧ mask 为真。名次只在有效样本之间计算
    （等价于先按 mask 过滤再排名）。单日有效标的数 < min_names 的交易日
    记为 NaN。
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if x.shape != y.shape:
        raise ValueError("daily_cs_ic_wide: x 与 y 形状必须一致")
    t, n = x.shape
    if n == 0:
        return np.full(t, np.nan), np.zeros(t, dtype=int)

    valid = np.isfinite(x) & np.isfinite(y)
    if mask is not None:
        valid &= np.asarray(mask, dtype=bool)
    n_names = valid.sum(axis=1)

    rx = row_ranks(np.where(valid, x, np.nan))
    ry = row_ranks(np.where(valid, y, np.nan))

    cnt = np.maximum(n_names, 1)
    mx = np.nansum(rx, axis=1) / cnt
    my = np.nansum(ry, axis=1) / cnt
    xc = np.where(valid, rx - mx[:, None], 0.0)
    yc = np.where(valid, ry - my[:, None], 0.0)

    cov = (xc * yc).sum(axis=1)
    sx = (xc * xc).sum(axis=1)
    sy = (yc * yc).sum(axis=1)
    denom = np.sqrt(sx * sy)
    ok = denom > 1e-18
    ic = np.where(ok, cov / np.where(ok, denom, 1.0), np.nan)
    ic = np.where(n_names >= int(min_names), ic, np.nan)
    return ic, n_names


def daily_cs_ic_long(score, future, date_codes, mask=None,
                      min_names: int = 2) -> Tuple[np.ndarray, np.ndarray]:
    """日频横截面 Spearman IC（长表布局）。

    score / future / date_codes：等长一维数组，M 为样本行。
    mask：一维布尔数组，可选。
    返回 (dates, ics)：有有效 IC 的日期（升序）与其 IC 值。

    与 daily_cs_ic_wide 口径一致；差别仅在单日最少样本：Spearman 至少需要
    3 个有效样本才能定义，因此这里的实际门槛是 max(min_names, 3)。
    """
    score = np.asarray(score, dtype=float).ravel()
    future = np.asarray(future, dtype=float).ravel()
    dc = np.asarray(date_codes).ravel()
    keep = np.isfinite(score) & np.isfinite(future)
    if mask is not None:
        keep &= np.asarray(mask, dtype=bool).ravel()
    need = max(int(min_names), 3)
    if not keep.any():
        return np.empty(0), np.empty(0)

    sc, fu, dd = score[keep], future[keep], dc[keep]
    order = np.argsort(dd, kind="stable")
    sc, fu, dd = sc[order], fu[order], dd[order]

    uniq, starts = np.unique(dd, return_index=True)
    ends = np.append(starts[1:], len(dd))

    dates_out, ics = [], []
    for k, (s0, s1) in enumerate(zip(starts, ends)):
        if s1 - s0 < need:
            continue
        v = spearman_ic(sc[s0:s1], fu[s0:s1])
        if math.isfinite(v):
            dates_out.append(uniq[k])
            ics.append(v)
    return np.asarray(dates_out), np.asarray(ics, dtype=float)


def pooled_ic(x, y, mask=None) -> float:
    """把所有样本堆在一起算的 rank IC。

    混入时序/市场择时效应，只作对照，不能当选股依据。
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    m = np.isfinite(x) & np.isfinite(y)
    if mask is not None:
        m &= np.asarray(mask, dtype=bool)
    if int(m.sum()) < 50:
        return float("nan")
    return spearman_ic(x[m], y[m])


def ts_ic(x, y, mask=None, min_obs: int = 60) -> Tuple[float, float]:
    """逐标的时间序列 IC 的均值/标准差，用来和横截面 IC 对照。

    x、y：(T, N)。返回 (mean, std)。
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    t, n = x.shape
    mask_arr = None if mask is None else np.asarray(mask, dtype=bool)
    vals = []
    for j in range(n):
        a, b = x[:, j], y[:, j]
        m = np.isfinite(a) & np.isfinite(b)
        if mask_arr is not None:
            m &= mask_arr[:, j]
        if int(m.sum()) < min_obs:
            continue
        av, bv = a[m], b[m]
        if np.unique(av).size < 5 or np.unique(bv).size < 5:
            continue
        v = spearman_ic(av, bv)
        if math.isfinite(v):
            vals.append(v)
    if not vals:
        return float("nan"), float("nan")
    arr = np.asarray(vals, dtype=float)
    return (float(arr.mean()),
            float(arr.std(ddof=1)) if len(arr) > 1 else 0.0)


def autocorr_1(x, min_obs: int = 20) -> float:
    """逐列的 lag-1 自相关均值。

    x：(T, N) 二维数组，T 为时间、N 为标的。只统计相邻两期都有效的样本；
    有效样本不足 min_obs 或该列无变化的标的跳过。

    用途：滞后敏感性诊断的参照系。一个高自相关的慢变特征，晚一天使用
    几乎不损失信息，IC 不衰减是正常的；低自相关的快变特征则应当快速衰减。
    """
    x = np.asarray(x, dtype=float)
    if x.ndim != 2:
        raise ValueError("autocorr_1 需要二维数组")
    vals = []
    for j in range(x.shape[1]):
        a, b = x[1:, j], x[:-1, j]
        m = np.isfinite(a) & np.isfinite(b)
        if int(m.sum()) < min_obs:
            continue
        av, bv = a[m], b[m]
        if av.std() < 1e-12 or bv.std() < 1e-12:
            continue
        vals.append(float(np.corrcoef(av, bv)[0, 1]))
    if not vals:
        return float("nan")
    return float(np.mean(vals))


# ============================================================
# top-k 超额
# ============================================================

def topk_excess(x, fwd, mask, sign: int, k: int, min_names: int,
                nw_lag: int, dates=None) -> dict:
    """top-k 超额与 top-bottom spread（宽表布局）。

    x、fwd、mask：(T, N)。sign：打分方向（由 DEV IC 符号决定，TEST 沿用）。
    k：持仓数。dates：参与统计的日期下标，默认全部。
    有效标的数 < max(min_names, k+1) 的交易日跳过。

    超额 = 当日 top-k 均值 − 当日全池均值；spread = top-k 均值 − bottom-k 均值。
    """
    x = np.asarray(x, dtype=float)
    fwd = np.asarray(fwd, dtype=float)
    t, n = x.shape
    if dates is None:
        rows = np.arange(t)
    else:
        rows = np.asarray(dates, dtype=int)

    score = x * float(sign)
    valid = np.isfinite(score) & np.isfinite(fwd)
    if mask is not None:
        valid &= np.asarray(mask, dtype=bool)

    sub_valid = valid[rows]
    n_valid = sub_valid.sum(axis=1)
    keep_row = n_valid >= max(int(min_names), int(k) + 1)

    s = score[rows]
    f = fwd[rows]
    v = sub_valid
    # 选 top-k 时无效项排到行尾，选 bottom-k 时无效项排到行首
    s_top = np.where(v, s, -np.inf)
    s_bot = np.where(v, s, np.inf)
    idx_top = np.argsort(-s_top, axis=1, kind="stable")[:, :k]
    idx_bot = np.argsort(s_bot, axis=1, kind="stable")[:, :k]

    f_valid = np.where(v, f, 0.0)
    pool_sum = f_valid.sum(axis=1)
    pool_cnt = np.maximum(v.sum(axis=1), 1)
    pool_mean = pool_sum / pool_cnt

    def _group_mean(idx):
        f_sel = np.take_along_axis(f_valid, idx, axis=1)
        v_sel = np.take_along_axis(v, idx, axis=1)
        cnt = np.maximum(v_sel.sum(axis=1), 1)
        return (f_sel * v_sel).sum(axis=1) / cnt

    top_mean = _group_mean(idx_top)
    bot_mean = _group_mean(idx_bot)

    excess_all = top_mean - pool_mean
    ls_all = top_mean - bot_mean

    excess = excess_all[keep_row]
    ls = ls_all[keep_row]

    if len(excess) < 10:
        return {"k": int(k), "excess": float("nan"), "excess_t": float("nan"),
                "ls": float("nan"), "ls_t": float("nan"), "hit": float("nan"),
                "days": int(len(excess))}

    ex_t, _ = newey_west_t(excess, nw_lag)
    ls_t, _ = newey_west_t(ls, nw_lag)
    return {"k": int(k),
            "excess": float(excess.mean()), "excess_t": ex_t,
            "ls": float(ls.mean()), "ls_t": ls_t,
            "hit": float((excess > 0).mean()), "days": int(len(excess))}


# ============================================================
# 折稳定性
# ============================================================

# 折内最少有效观测数：低于此值该折不可信
MIN_FOLD_OBS = 10


def effective_gap(user_gap: int, horizon: int) -> Tuple[int, bool]:
    """折间间隔（gap）的生效值。

    gap = 相邻折之间被完全跳过、不参与任何折的 bar 数。

    标签是 horizon 根 bar 的前瞻收益时，第 t 根 bar 的标签窗口覆盖
    [t, t+horizon]。要让相邻两折的标签窗口不重叠，两折之间至少要隔
    horizon 根 bar，所以：

        effective_gap = max(user_gap, horizon)

    返回 (生效值, 是否被提升)。设定值小于 horizon 时自动提升到
    horizon，调用方需要显式提示用户，不能静默截断。
    """
    user_gap = int(max(0, user_gap or 0))
    horizon = int(max(0, horizon or 0))
    eff = max(user_gap, horizon)
    return eff, eff != user_gap


def fold_stats(ic, n_folds: int, nw_lag: int, gap: int = 0,
               horizon: int = 0) -> dict:
    """把一段逐日 IC 切成 n_folds 个折，报每折 IC 与同号折数。

    ic：逐日 IC 序列。传**带日期索引的 Series** 时 gap 按日历 bar 计——
        此时序列必须保留无效日的 NaN，不能先 dropna 再切，否则 gap 变成
        "有效观测数"口径，与 bar 口径不符。传一维数组时 gap 按数组位置计。
    gap ：相邻折之间被完全跳过、不参与任何折的 bar 数。
        实际生效值 = max(gap, horizon)，见 effective_gap。
    horizon：标签前瞻期（根 bar），是 gap 的下限。

    观测数不足以支撑 n_folds 折（含 gap 占用的间隔）时返回空结果。
    """
    eff_gap, gap_raised = effective_gap(gap, horizon)
    n_folds = int(n_folds)

    empty = {"fold_ic": [], "fold_t": [], "sign_consistency": 0,
             "fold_ic_std": float("nan"), "n_folds": int(n_folds),
             "gap": int(eff_gap), "effective_gap": int(eff_gap),
             "gap_raised": bool(gap_raised), "fold_dates": []}

    has_index = hasattr(ic, "index") and hasattr(ic, "iloc")
    values = np.asarray(ic, dtype=float).ravel()
    index = ic.index if has_index else None

    n = len(values)
    need = n_folds * MIN_FOLD_OBS + max(0, n_folds - 1) * eff_gap
    if n_folds < 1 or n < need:
        return empty

    # 折宽 = 剩余长度均分；折与折之间跳过 eff_gap 根 bar
    width = (n - (n_folds - 1) * eff_gap) // n_folds
    means, ts, dates = [], [], []
    start = 0
    for _ in range(n_folds):
        stop = start + width
        part = values[start:stop]
        part = part[np.isfinite(part)]
        if len(part) == 0:
            means.append(float("nan"))
            ts.append(float("nan"))
        else:
            means.append(float(part.mean()))
            t, _ = newey_west_t(part, nw_lag)
            ts.append(float(t))
        if index is not None and stop > start:
            dates.append([str(index[start]), str(index[stop - 1])])
        else:
            dates.append(None)
        start = stop + eff_gap

    finite_all = values[np.isfinite(values)]
    sign = np.sign(finite_all.mean()) if len(finite_all) else 0.0
    consistency = (int(sum(1 for m in means
                           if np.isfinite(m) and np.sign(m) == sign))
                   if sign != 0 else 0)
    return {"fold_ic": [round(x, 6) if np.isfinite(x) else None for x in means],
            "fold_t": [round(x, 4) if np.isfinite(x) else None for x in ts],
            "sign_consistency": consistency,
            "fold_ic_std": float(np.std(means, ddof=1)) if len(means) > 1 else float("nan"),
            "n_folds": int(n_folds),
            "gap": int(eff_gap),
            "effective_gap": int(eff_gap),
            "gap_raised": bool(gap_raised),
            "fold_dates": dates}


# ============================================================
# 分布漂移（KS / PSI）
# ============================================================

def ks_2samp(a, b) -> dict:
    """两样本 Kolmogorov-Smirnov 检验。

    返回 {"D": float, "p": float|None, "n": int, "m": int, "p_method": str}。

    D = max|F_a(x) - F_b(x)|，在合并排序后的所有取值点上算。
    p 用渐近 Kolmogorov 分布（大样本近似）：
        λ = (√n_eff + 0.12 + 0.11/√n_eff)·D
        Q(λ) = 2 Σ (-1)^{k-1} exp(-2k²λ²)
    其中 n_eff = nm/(n+m)。

    小样本（min(n,m) < 20）时 p=None，只给 D——渐近近似在 n<20 时误差大，
    不假装精确。p_method 写 "asymptotic" 或 "insufficient"，便于调用方判断。

    与 scipy.stats.ks_2samp(method="asymp") 对拍（D 1e-12、p 1e-3）。
    """
    x = np.asarray(a, dtype=float).ravel()
    y = np.asarray(b, dtype=float).ravel()
    x = x[np.isfinite(x)]
    y = y[np.isfinite(y)]
    n, m = len(x), len(y)
    if n == 0 or m == 0:
        return {"D": float("nan"), "p": None, "n": n, "m": m,
                "p_method": "insufficient"}

    # 合并排序，计算经验分布的差
    xy = np.concatenate([x, y])
    order = np.argsort(xy, kind="mergesort")
    xy_sorted = xy[order]
    # 标记每个位置来自 x 还是 y
    is_x = np.zeros(len(xy), dtype=bool)
    is_x[:n] = True
    is_x = is_x[order]

    # 累积分布：F_a(t) = #{x_i ≤ t}/n, F_b(t) = #{y_i ≤ t}/m
    # 在排序后的每个点上算
    cum_x = np.cumsum(is_x) / n
    cum_y = np.cumsum(~is_x) / m
    D = float(np.max(np.abs(cum_x - cum_y)))

    # 小样本时 p 不可靠，返回 None
    if min(n, m) < 20:
        return {"D": D, "p": None, "n": n, "m": m,
                "p_method": "insufficient"}

    # 渐近 p 值
    n_eff = (n * m) / (n + m)
    sqrt_n_eff = math.sqrt(n_eff)
    lam = (sqrt_n_eff + 0.12 + 0.11 / sqrt_n_eff) * D
    if lam < 1e-12:
        return {"D": D, "p": 1.0, "n": n, "m": m, "p_method": "asymptotic"}

    # Q(λ) = 2 Σ (-1)^{k-1} exp(-2k²λ²)，k=1,2,...
    # 截断到项 < 1e-12
    p = 0.0
    k = 1
    while True:
        term = 2.0 * ((-1) ** (k - 1)) * math.exp(-2.0 * k * k * lam * lam)
        p += term
        if abs(term) < 1e-12:
            break
        k += 1
        if k > 1000:  # 安全上限
            break
    p = max(0.0, min(1.0, p))

    return {"D": D, "p": float(p), "n": n, "m": m, "p_method": "asymptotic"}


def psi(ref, cur, bins: int = 10, binning: str = "auto", eps: float = 1e-6) -> dict:
    """Population Stability Index。

    返回 {"psi": float, "binning_used": str, "n_bins": int, "n_clipped": int,
          "n_ref": int, "n_cur": int, "bins": list[dict]}。

    PSI = Σ (p_cur - p_ref)·ln(p_cur/p_ref)，p 先 clip 到 eps（记录 n_clipped）。

    分箱：
      - "auto"：unique(ref) <= bins 时用每个唯一值一箱（离散特征必须这样，
        否则分位箱全是并列值）；否则用 ref 的分位数边界。
      - "quantile"：强制分位数边界。
      - "width"：等宽边界（min(ref)..max(ref)）。
    边界重复时去重，有效箱 < 3 时退化为等宽箱并标记 binning_used。

    逐箱明细 bins[i] = {"lo": float, "hi": float, "ref_frac": float,
                         "cur_frac": float, "contrib": float}。
    contrib = (cur_frac - ref_frac)·ln(cur_frac/ref_frac)，所有 contrib 之和 = psi。

    PSI 不对称：ref 必须是基线，cur 是当前窗。

    与 sklearn 的 population_stability_index 对拍（同分箱、同 eps 下 PSI 1e-10）。
    """
    ref = np.asarray(ref, dtype=float).ravel()
    cur = np.asarray(cur, dtype=float).ravel()
    ref = ref[np.isfinite(ref)]
    cur = cur[np.isfinite(cur)]
    n_ref, n_cur = len(ref), len(cur)
    if n_ref == 0 or n_cur == 0:
        return {"psi": float("nan"), "binning_used": binning, "n_bins": 0,
                "n_clipped": 0, "n_ref": n_ref, "n_cur": n_cur, "bins": []}

    # 分箱边界
    #
    # 三种口径：
    #   unique   —— 每个唯一值一箱（离散特征/状态量必须这样，分位箱会被并列值压扁）
    #   quantile —— ref 的分位边界（连续特征默认）
    #   width    —— 等宽边界（quantile 退化时的兜底）
    # histogram 的最后一箱是闭区间，所以右端要用 nextafter 推开一点，
    # 否则 ref 的最大值会落在箱外；左端不需要扩，第一箱本来就左闭。
    binning_used = binning
    if binning == "auto":
        binning_used = "unique" if len(np.unique(ref)) <= bins else "quantile"

    if binning_used == "unique":
        u = np.unique(ref)
        edges = np.append(u, np.nextafter(u[-1], np.inf))
    elif binning_used == "quantile":
        edges = np.unique(np.percentile(ref, np.linspace(0, 100, bins + 1)))
        if len(edges) < 2:
            # ref 是常量：分位边界全部重合，退化为唯一值箱
            binning_used = "unique"
            edges = np.array([ref[0], np.nextafter(ref[0], np.inf)])
    elif binning_used == "width":
        if ref.min() == ref.max():
            binning_used = "unique"
            edges = np.array([ref[0], np.nextafter(ref[0], np.inf)])
        else:
            edges = np.linspace(ref.min(), ref.max(), bins + 1)
    else:
        raise ValueError(f"unknown binning: {binning}")

    n_bins = len(edges) - 1
    # 计算每箱的占比
    ref_counts = np.histogram(ref, bins=edges)[0]
    cur_counts = np.histogram(cur, bins=edges)[0]
    ref_frac = ref_counts / n_ref
    cur_frac = cur_counts / n_cur

    # clip 到 eps
    n_clipped = 0
    ref_frac_clipped = ref_frac.copy()
    cur_frac_clipped = cur_frac.copy()
    mask_ref = ref_frac_clipped < eps
    mask_cur = cur_frac_clipped < eps
    n_clipped = int(mask_ref.sum() + mask_cur.sum())
    ref_frac_clipped[mask_ref] = eps
    cur_frac_clipped[mask_cur] = eps

    # 逐箱 contrib
    bins_detail = []
    psi_total = 0.0
    for i in range(n_bins):
        r = ref_frac_clipped[i]
        c = cur_frac_clipped[i]
        contrib = (c - r) * math.log(c / r)
        psi_total += contrib
        bins_detail.append({
            "lo": float(edges[i]),
            "hi": float(edges[i + 1]),
            "ref_frac": float(ref_frac[i]),
            "cur_frac": float(cur_frac[i]),
            "contrib": float(contrib),
        })

    return {"psi": float(psi_total), "binning_used": binning_used,
            "n_bins": n_bins, "n_clipped": n_clipped,
            "n_ref": n_ref, "n_cur": n_cur, "bins": bins_detail}


def block_bounds(n: int, n_blocks: int, gap: int) -> list:
    """等宽连续折的边界。

    返回 [(start, stop), ...]，共 n_blocks 个元组。
    折宽 = (n - (B-1)·gap) // B，余数留在尾部不参与。
    相邻折之间跳过 gap 个样本。

    与 fold_stats 的切分逻辑完全一致：同参数下 fold_stats 的 fold_ic
    与 block 稳定性的 ic 逐块相等（test_stability.py 有对拍用例）。
    """
    n_blocks = int(n_blocks)
    gap = int(gap)
    if n_blocks < 1 or gap < 0:
        return []
    need = n_blocks * 1 + max(0, n_blocks - 1) * gap  # 最少每折 1 个样本
    if n < need:
        return []

    width = (n - (n_blocks - 1) * gap) // n_blocks
    bounds = []
    start = 0
    for _ in range(n_blocks):
        stop = start + width
        bounds.append((start, stop))
        start = stop + gap
    return bounds


# ============================================================
# 金融回归内核（C++ service/src/Algorithms/regression.cpp 的逐字镜像）
# ============================================================
#
# 这一节的口径必须与 C++ 侧完全一致：service/test/testcases/
# test_regression_parity.py 通过 REST 接口做对拍，任何一处公式、阈值、
# 迭代策略改动都要两边同时改，否则 parity 会红。
#
# 共同约定
#   listwise 删除：y 或任一 X 非有限 → 整行丢弃，计数上报，绝不插补
#   秩判定：按列顺序贪心，投影残差相对范数 > 1e-10 才保留
#   IID：V = sigma^2 (X'X)^-1，sigma^2 = e'e/(n-rank)
#   HAC：V = (X'X)^-1 Omega (X'X)^-1，Bartlett 权 w_j = 1 - j/(L+1)
#   聚类：V = c (X'X)^-1 (sum_g X_g'u_g u_g'X_g) (X'X)^-1
#        c = G/(G-1)·(n-1)/(n-rank)
#   t 分布 p 值：p = I_{df/(df+t^2)}(df/2, 1/2)，df = n-rank
#   系数序列均值检验：Var(mean) = S/n，S 为 Bartlett 加权长期方差

REG_RANK_TOL = 1e-10
REG_ZERO_NORM = 1e-12
REG_CONST_TOL = 1e-12


def _betacf(a: float, b: float, x: float) -> float:
    """Lentz 连分式 betacf(a, b, x)，与 C++ betacf 逐行对齐。

    参数必须显式传入：Numerical Recipes 的 I_x(a, b) 在
    x >= (a+1)/(a+b+2) 时走补集分支，需要 betacf(b, a, 1-x)——
    参数与自变量都要换。若写成闭包捕获 (a, b, x)，补集分支会算错，
    导致小 |t| 时 p 值变成负数。
    """
    k_max_iter = 200
    eps = 3e-16
    fpmin = 1e-300

    qab = a + b
    qap = a + 1.0
    qam = a - 1.0
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < fpmin:
        d = fpmin
    d = 1.0 / d
    h = d
    for m in range(1, k_max_iter + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < fpmin:
            d = fpmin
        c = 1.0 + aa / c
        if abs(c) < fpmin:
            c = fpmin
        d = 1.0 / d
        h *= d * c

        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < fpmin:
            d = fpmin
        c = 1.0 + aa / c
        if abs(c) < fpmin:
            c = fpmin
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < eps:
            break
    return h


def _regularized_incomplete_beta(a: float, b: float, x: float) -> float:
    """正则化不完全 Beta I_x(a, b)（Lentz 连分式，与 C++ betacf 同算法）。"""
    if not (a > 0.0) or not (b > 0.0):
        return float("nan")
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0

    lbeta = math.lgamma(a) + math.lgamma(b) - math.lgamma(a + b)

    if x < (a + 1.0) / (a + b + 2.0):
        return (math.exp(a * math.log(x) + b * math.log1p(-x) - lbeta)
                * _betacf(a, b, x) / a)
    return 1.0 - (math.exp(b * math.log1p(-x) + a * math.log(x) - lbeta)
                  * _betacf(b, a, 1.0 - x) / b)


def student_t_two_sided_p(t: float, df: float) -> float:
    """双侧 student-t p 值；与 C++ studentTTwoSidedP 同口径。"""
    if not np.isfinite(t) or not (df > 0.0):
        return float("nan")
    if t == 0.0:
        return 1.0
    x = df / (df + t * t)
    return _regularized_incomplete_beta(df / 2.0, 0.5, x)


def _is_constant(col: np.ndarray) -> bool:
    if col.size == 0:
        return True
    return bool(np.max(np.abs(col - col[0])) <= REG_CONST_TOL)


def _sample_std(v: np.ndarray) -> float:
    if len(v) < 2:
        return 0.0
    return float(np.std(v, ddof=1))


def greedy_rank_select(A, forced_drop=None):
    """按列顺序贪心选列：投影残差的相对范数 > REG_RANK_TOL 才保留。

    与 C++ greedyRankSelect 同判据。返回 (kept, dropped)，均为升序下标。
    """
    A = np.asarray(A, dtype=float)
    if A.ndim == 1:
        A = A[:, None]
    n, k = A.shape
    kept: list[int] = []
    dropped: list[int] = []

    for j in range(k):
        if forced_drop is not None and bool(forced_drop[j]):
            dropped.append(j)
            continue
        col = A[:, j]
        norm = float(np.linalg.norm(col))
        if not (norm > REG_ZERO_NORM):
            dropped.append(j)
            continue
        if not kept:
            kept.append(j)
            continue
        K = A[:, kept]
        coef, *_ = np.linalg.lstsq(K, col, rcond=None)
        rel = float(np.linalg.norm(col - K @ coef)) / norm
        if rel > REG_RANK_TOL:
            kept.append(j)
        else:
            dropped.append(j)
    return kept, dropped


def _drop_reason(A: np.ndarray, dropped_col: int, kept, names) -> str:
    x = A[:, dropped_col]
    sx = _sample_std(x) if x.size > 1 else 0.0
    if sx <= 1e-15:
        return "constant" if _is_constant(x) else "zero_variance"
    best = -1.0
    best_name = "unknown"
    for j in kept:
        z = A[:, j]
        sz = _sample_std(z) if z.size > 1 else 0.0
        if sz <= 1e-15:
            continue
        cov = float(np.sum((x - x.mean()) * (z - z.mean())))
        corr = abs(cov / (sx * sz * x.size))
        if corr > best:
            best = corr
            best_name = names[j] if j < len(names) else f"col{j}"
    return f"collinear_with:{best_name}"


def _solve_design(A, b, ridge_lambda: float = 0.0, forced_drop=None) -> dict:
    """OLS/岭回归（保留列 + (X'X)^-1），与 C++ solveDesign 同口径。"""
    A = np.asarray(A, dtype=float)
    b = np.asarray(b, dtype=float)
    if A.ndim == 1:
        A = A[:, None]
    n, k = A.shape

    kept, dropped = greedy_rank_select(A, forced_drop)
    rank = len(kept)
    beta = np.full(k, np.nan)
    resid = np.zeros(n)

    if rank == 0:
        return {
            "beta": beta, "resid": resid,
            "Akept": np.zeros((n, 0)), "XtXinv": np.zeros((0, 0)),
            "kept": kept, "dropped": dropped, "rank": 0,
        }

    Ak = A[:, kept]
    XtX = Ak.T @ Ak
    Xty = Ak.T @ b
    is_const = [_is_constant(Ak[:, m]) for m in range(rank)]

    if ridge_lambda > 0.0:
        XtXr = XtX.copy()
        for m in range(rank):
            if not is_const[m]:
                XtXr[m, m] += ridge_lambda
        beta_k = np.linalg.solve(XtXr, Xty)
        XtXinv = np.linalg.inv(XtXr)
    else:
        beta_k, *_ = np.linalg.lstsq(Ak, b, rcond=None)
        XtXinv = np.linalg.inv(XtX)

    resid = b - Ak @ beta_k
    beta[kept] = beta_k
    return {
        "beta": beta, "resid": resid, "Akept": Ak, "XtXinv": XtXinv,
        "kept": kept, "dropped": dropped, "rank": rank,
    }


def _cov_hac(A: np.ndarray, u: np.ndarray, XtXinv: np.ndarray, lag: int) -> np.ndarray:
    n, k = A.shape
    omega = np.zeros((k, k))
    for t in range(n):
        xt = A[t]
        omega += (u[t] * u[t]) * np.outer(xt, xt)
    L = min(int(lag), n - 2)
    if L < 0:
        L = 0
    for j in range(1, L + 1):
        w = 1.0 - j / (L + 1.0)
        gamma = np.zeros((k, k))
        for t in range(j, n):
            gamma += (u[t] * u[t - j]) * np.outer(A[t], A[t - j])
        omega += w * (gamma + gamma.T)
    return XtXinv @ omega @ XtXinv


def _cov_cluster(A: np.ndarray, u: np.ndarray, XtXinv: np.ndarray,
                 cluster, rank: int) -> np.ndarray:
    n, k = A.shape
    groups: dict[int, list[int]] = {}
    for t in range(min(n, len(cluster))):
        groups.setdefault(int(cluster[t]), []).append(t)
    if len(groups) < 2:
        return XtXinv.copy()

    meat = np.zeros((k, k))
    for idx in groups.values():
        s = np.zeros(k)
        for t in idx:
            s += u[t] * A[t]
        meat += np.outer(s, s)

    G = float(len(groups))
    df = float(max(1, n - rank))
    c = (G / (G - 1.0)) * (float(n) - 1.0) / df
    return c * (XtXinv @ meat @ XtXinv)


def _compute_cond(A: np.ndarray) -> float:
    if A.shape[1] < 2 or A.shape[0] < 2:
        return 0.0
    S = A.copy()
    for j in range(S.shape[1]):
        nrm = float(np.linalg.norm(S[:, j]))
        if nrm > 1e-12:
            S[:, j] /= nrm
    sv = np.linalg.svd(S, compute_uv=False)
    smin = float(sv[-1])
    return float(sv[0] / smin) if smin > 0.0 else float("inf")


def _compute_vif(A: np.ndarray) -> list:
    k = A.shape[1]
    out = [0.0] * k
    if k < 2 or A.shape[0] < 3:
        return out
    # 设计里已有常量列（截距）就不再补，避免秩亏导致投影差异（与 C++ 同规则）
    has_const = any(_is_constant(A[:, m]) for m in range(k))
    for j in range(k):
        yj = A[:, j]
        if _is_constant(yj):
            continue
        cols = [A[:, m] for m in range(k) if m != j]
        if not has_const:
            cols.append(np.ones(A.shape[0]))
        Xo = np.column_stack(cols)
        coef, *_ = np.linalg.lstsq(Xo, yj, rcond=None)
        sst = float(np.sum((yj - yj.mean()) ** 2))
        if sst <= 1e-15:
            continue
        r2 = 1.0 - float(np.sum((yj - Xo @ coef) ** 2)) / sst
        out[j] = float("inf") if r2 >= 1.0 - 1e-12 else 1.0 / (1.0 - r2)
    return out


def ols_fit(y, X, names=None, se: str = "iid", hac_lag: int = 20,
            ridge_lambda: float = 0.0, cluster=None,
            report_vif: bool = True, time_ordered: bool = False) -> dict:
    """单次 OLS / 岭回归，返回与 C++ Fit 同构的 dict。

    se: "iid" | "hac" | "cluster"（cluster 与 y 等长，按原始行号索引）
    """
    y = np.asarray(y, dtype=float).ravel()
    X = np.asarray(X, dtype=float)
    if X.ndim == 1:
        X = X[:, None]
    n_raw, k = X.shape
    if n_raw <= 0 or k <= 0 or X.shape[0] != y.size:
        return {"n_obs": 0, "n_reg": k, "rank": 0, "n_dropped_rows": 0,
                "r2": float("nan"), "adj_r2": float("nan"), "sigma": float("nan"),
                "dw": 0.0, "cond": 0.0, "terms": [], "vif": [],
                "warnings": ["输入为空或维度不一致"]}

    names = list(names) if names else [f"x{j}" for j in range(k)]
    valid = np.isfinite(y) & np.all(np.isfinite(X), axis=1)
    rows = np.flatnonzero(valid)
    n = len(rows)
    n_dropped = n_raw - n
    if n < 2:
        return {"n_obs": n, "n_reg": k, "rank": 0, "n_dropped_rows": n_dropped,
                "r2": float("nan"), "adj_r2": float("nan"), "sigma": float("nan"),
                "dw": 0.0, "cond": 0.0,
                "terms": [{"name": names[j], "beta": float("nan"), "se": float("nan"),
                           "t": float("nan"), "p": float("nan"), "std_beta": 0.0,
                           "dropped": True, "drop_reason": "insufficient_obs"}
                          for j in range(k)],
                "vif": [0.0] * k, "warnings": ["有效样本不足"]}

    A = X[rows, :]
    b = y[rows]
    s = _solve_design(A, b, ridge_lambda)
    rank = s["rank"]

    warnings: list = []
    terms = []
    for j in range(k):
        dropped = j in s["dropped"]
        terms.append({"name": names[j], "beta": float("nan"), "se": float("nan"),
                      "t": float("nan"), "p": float("nan"), "std_beta": 0.0,
                      "dropped": dropped,
                      "drop_reason": _drop_reason(A, j, s["kept"], names) if dropped else ""})

    sse = float(np.sum(s["resid"] ** 2))
    df = n - rank
    sigma2 = sse / df if df > 0 else float("nan")
    sigma = math.sqrt(sigma2) if np.isfinite(sigma2) else float("nan")

    my = float(b.mean())
    sst = float(np.sum((b - my) ** 2))
    r2 = 1.0 - sse / sst if sst > 1e-15 else float("nan")
    adj_r2 = (1.0 - (1.0 - r2) * (n - 1.0) / df
              if df > 0 and np.isfinite(r2) else float("nan"))

    dw = 0.0
    if time_ordered and n > 1 and sse > 1e-15:
        dw = float(np.sum(np.diff(s["resid"]) ** 2)) / sse

    if se == "hac":
        V = _cov_hac(s["Akept"], s["resid"], s["XtXinv"], hac_lag)
    elif se == "cluster":
        if cluster is not None and len(cluster) == n_raw:
            c = np.asarray(cluster)[rows]
            V = _cov_cluster(s["Akept"], s["resid"], s["XtXinv"], c, rank)
        else:
            warnings.append("聚类 id 长度不符，退化为 IID")
            V = s["XtXinv"] * sigma2
    else:
        V = s["XtXinv"] * sigma2

    sy = _sample_std(b)
    for m, j in enumerate(s["kept"]):
        beta = float(s["beta"][j])
        se_j = math.sqrt(max(0.0, float(V[m, m])))
        t = beta / se_j if se_j > 0.0 else float("nan")
        p = student_t_two_sided_p(abs(t), float(df))
        xj = s["Akept"][:, m]
        std_beta = (beta * _sample_std(xj) / sy
                    if sy > 1e-15 and not _is_constant(xj) else 0.0)
        terms[j].update({"beta": beta, "se": se_j, "t": t, "p": p,
                         "std_beta": std_beta, "dropped": False, "drop_reason": ""})

    vif = [0.0] * k
    if report_vif:
        vif_kept = _compute_vif(s["Akept"])
        for m, j in enumerate(s["kept"]):
            vif[j] = float(vif_kept[m])

    return {
        "n_obs": n, "n_reg": k, "rank": rank, "n_dropped_rows": n_dropped,
        "r2": r2, "adj_r2": adj_r2, "sigma": sigma, "dw": dw,
        "cond": _compute_cond(s["Akept"]),
        "terms": terms, "vif": vif, "warnings": warnings,
    }


def series_mean_test(values, se: str = "iid", hac_lag: int = 20) -> dict:
    """系数序列的均值检验（横截面逐日系数 / 时序逐标的暴露）。

    Var(mean) = S/n，S = gamma0 + 2·sum_j w_j·gamma_j，gamma 为自协方差。
    与 newey_west_t 共用 _hac_mean_stat：自协方差分母是 n，两侧 t 值一致。
    """
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    n = len(v)
    nan = float("nan")
    if n == 0:
        return {"mean": nan, "se": nan, "t": nan, "p": nan, "n": 0}
    if n == 1:
        return {"mean": float(v[0]), "se": nan, "t": nan, "p": nan, "n": 1}

    # IID 口径不引入自协方差项（L=0）；HAC 口径用 hac_lag。
    lag = int(hac_lag) if se == "hac" else 0
    m, se_v, _S = _hac_mean_stat(v, lag, min_lag=0)
    t = m / se_v if se_v > 0.0 else nan
    return {"mean": m, "se": se_v, "t": t,
            "p": student_t_two_sided_p(abs(t), float(n - 1)), "n": n}


def build_group_codes(labels, min_size: int = 2, fallback: str = "OTHER"):
    """组标签 → 组号；成员数 < min_size 的组并入 fallback。

    返回 (codes, levels)，levels 升序排列（与 C++ std::set 排序一致）。
    """
    labels = [str(x) for x in labels]
    counts: dict = {}
    for l in labels:
        counts[l] = counts.get(l, 0) + 1
    mapped = [l if counts[l] >= min_size else fallback for l in labels]
    levels = sorted(set(mapped))
    index = {l: i for i, l in enumerate(levels)}
    return [index[m] for m in mapped], levels


def winsorize_cross_section(values, q: float):
    """按分位裁剪（线性插值分位，与 C++ 一致）。q=0 表示不裁剪。"""
    v = np.asarray(values, dtype=float).copy()
    if not (q > 0.0) or q >= 0.5:
        return v
    finite = v[np.isfinite(v)]
    if len(finite) < 3:
        return v
    lo = float(np.quantile(finite, q))
    hi = float(np.quantile(finite, 1.0 - q))
    return np.clip(v, lo, hi)


def _demean_by_codes(v: np.ndarray, code, ng: int) -> np.ndarray:
    code = np.asarray(code, dtype=int)
    sums = np.bincount(code, weights=v, minlength=ng)
    cnt = np.bincount(code, minlength=ng)
    means = np.divide(sums, cnt, out=np.zeros(ng), where=cnt > 0)
    return v - means[code]


def alternate_demean(y, X, codes, n_groups):
    """交替投影去均值（返回新数组）。

    单分组变量一步精确；多分组变量迭代到不动点（容差 1e-13，上限 200 次），
    与 C++ alternateDemean 同策略。
    """
    y = np.asarray(y, dtype=float).copy()
    X = np.asarray(X, dtype=float).copy()
    if X.ndim == 1:
        X = X[:, None]
    if not codes:
        return y, X

    if len(codes) == 1:
        y = _demean_by_codes(y, codes[0], n_groups[0])
        for c in range(X.shape[1]):
            X[:, c] = _demean_by_codes(X[:, c], codes[0], n_groups[0])
        return y, X

    for _ in range(200):
        y_prev = y.copy()
        X_prev = X.copy()
        for g, code in enumerate(codes):
            y = _demean_by_codes(y, code, n_groups[g])
            for c in range(X.shape[1]):
                X[:, c] = _demean_by_codes(X[:, c], code, n_groups[g])
        delta = float(np.max(np.abs(y - y_prev))) if y.size else 0.0
        if X.size:
            delta = max(delta, float(np.max(np.abs(X - X_prev))))
        if delta < 1e-13:
            break
    return y, X


def partial_residual(y, X, codes, n_groups, add_const: bool = True):
    """FWL 部分残差：等价于 y ~ 1 + 分组 + X 的联合回归残差。

    输入必须全部有限（NaN 会被静默传播，调用方负责清洗）。
    """
    y1, X1 = alternate_demean(y, X, codes, n_groups)
    if X1.shape[1] == 0:
        return y1
    A = np.column_stack([np.ones(len(y1)), X1]) if add_const else X1
    return _solve_design(A, y1, 0.0)["resid"]

