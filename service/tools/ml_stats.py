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

数据布局
--------
本模块提供两套等价的数据布局，统计口径完全一致：

  - 宽表 (T, N)：x / y / mask 三个二维数组，T 为日期、N 为标的。
    `daily_cs_ic_wide` 面向"每个特征一张宽表"的筛选场景。
  - 长表 (M,)：score / future / date_codes 三个一维数组，M 为样本行。
    `daily_cs_ic_long` 面向"所有特征堆在一张表"的训练评估场景。

`daily_cs_ic_wide` 与 `daily_cs_ic_long` 在同一批数据上必须给出相同结果
（tools/test_ml_stats.py 有对拍用例）。

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

def newey_west_t(series, lag: int) -> Tuple[float, float]:
    """Newey-West HAC t 值和双侧 p 值（正态近似）。

    series：任意可转 float 的一维序列（list / np.ndarray / pd.Series）。
    lag：HAC 截断阶数，实际取 max(1, min(lag, n-2))。
    样本不足 10 个时返回 (nan, nan)。

    口径见模块头：var 按均值的方差归一化，se = sqrt(var / n)。
    """
    x = np.asarray(series, dtype=float).ravel()
    x = x[np.isfinite(x)]
    n = len(x)
    if n < 10:
        return float("nan"), float("nan")
    lag = max(1, min(int(lag), n - 2))
    mu = float(x.mean())
    e = x - mu
    var = float((e * e).mean())
    for l in range(1, lag + 1):
        gamma = float((e[l:] * e[:-l]).mean())
        var += 2.0 * (1.0 - l / (lag + 1.0)) * gamma
    if var <= 1e-18:
        if abs(mu) < 1e-18:
            return 0.0, 1.0
        return math.copysign(float("inf"), mu), 0.0
    se = math.sqrt(var / n)
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
