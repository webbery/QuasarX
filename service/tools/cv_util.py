"""时序交叉验证的切分工具。

Phase 0 只做一件事：把样本按“日期”而不是“行号”切分。
matrix 模式下同一天会有多个标的的多行样本，按行号切会把同一天的标的
分到训练/测试两侧（截面泄漏），也会让“测试集”变成最后几个标的而不是
最后一段时间。这里先按日期稳定排序，再以唯一日期为边界切分。

Phase 1 的 walk-forward 折计划也放在本模块；评估指标和 NW t 在
ml_metrics.py，Dataset 组装在 ml_dataset.py。

折间隔离（gap / purge）
------------------------
标签是 horizon 根 bar 的前瞻收益时，相邻两段之间必须留出 horizon 根 bar，
否则训练样本的标签窗口会伸进下一段，看到下一段的价格。walk-forward 的
折计划（``build_walk_forward_folds``）一直按这个规则留 gap；单次切分
（``split_indices_by_date``）此前**没有**留 gap，是泄漏来源之一。

``count_label_overlap_rows`` 把"泄漏了多少行"算出来，让泄漏检查从
硬编码的字面量变成真实统计。
"""

from typing import List, Optional, Sequence

import numpy as np


def _floor_ratio(n: int, frac: float) -> int:
    """按比例取整（向下），并抵消浮点相减带来的边界误差。

    ``1.0 - 0.15 - 0.2`` 在二进制浮点下是 0.6499999999999999，于是
    ``int(100 * (1 - 0.15 - 0.2))`` 得 64 而不是 65 —— 切分边界与文档写的
    比例差一天。加一个极小的 epsilon 只影响"恰好落在整数边界"的情形，
    其余取值不变。
    """
    return int(n * frac + 1e-9)


def split_indices_by_date(dates: Sequence, use_val: bool,
                          val_ratio: float, test_ratio: float,
                          gap_dates: int = 0) -> Optional[dict]:
    """按唯一日期计算 train/val/test 的行边界，并预留折间间隔。

    参数
    ----
    dates : 已按时间升序排列的样本日期（可重复；同一个值代表同一天的多条样本）
    use_val : 是否切出验证集
    val_ratio, test_ratio : 验证/测试集的日期占比
    gap_dates : 相邻两段之间被完全跳过的**日期数**（purge）。

    返回
    ----
    dict，键：
      train_end  训练段结束行号（不含）  训练 = [0, train_end)
      val_start  验证段起始行号（含）      验证 = [val_start, val_end)
      val_end    验证段结束行号（不含）
      test_start 测试段起始行号（含）      测试 = [test_start, n)
      train_date_end / val_date_end  以唯一日期计的段边界下标
      gap_dates  实际生效的间隔日期数
      n_dates    唯一日期总数
    日期数不足或无法切出非空段时返回 None。

    关于 gap_dates
    --------------
    标签是 horizon 根 bar 的前瞻收益时，第 d 天的标签覆盖 [d, d+horizon]。
    要让训练段的标签窗口不伸进下一段，训练侧的段必须从尾部挖掉 horizon
    根 bar，所以 gap_dates 应取 label_period。

    purge 语义：间隔从**训练侧那一段的尾部**挖掉，留出段保持完整的名义跨度

        训练段 = [0, T-gap)
        验证段 = [T, V-gap)
        测试段 = [V, N)          ← 完整，不被间隔截短

    留出集是唯一无偏的读数，不该被间隔切掉尾巴；而训练段少几天无关紧要。
    这与 walk-forward 折计划的 ``train_end = oos_start - gap`` 同一口径。

    间隔按**日期**而不是行号计：多标的场景下同一天有多个标的的多行样本，
    按行号隔 horizon 行只能剔掉某一个标的的若干行，起不到隔离作用。

    数据不够放间隔时，间隔会被压缩到"刚好放得下"，并在返回值里用
    ``gap_shrunk`` 标出来 —— 压缩后可能仍有泄漏，由调用方用
    ``count_label_overlap_rows`` 如实报出。

    切分边界永远落在某个日期的第一行，因此同一天的所有样本必然在同一段。
    """
    n = len(dates)
    if n == 0:
        return None

    # 每个唯一日期的首个行索引（要求 dates 已排序）
    first_idx: List[int] = []
    prev = None
    for i, d in enumerate(dates):
        if i == 0 or d != prev:
            first_idx.append(i)
            prev = d
    n_dates = len(first_idx)
    if n_dates < 2:
        return None

    gap = max(0, int(gap_dates or 0))

    def row_at(k: int) -> int:
        return first_idx[k] if 0 <= k < n_dates else n

    def _pack(train_end_k, val_start_k, val_end_k, test_start_k,
              eff_gap, has_val, has_test):
        """把日期边界翻成行边界并做一致性检查。

        purge 语义：间隔从**训练侧那一段的尾部**挖掉，而不是在两段之间插洞。
        这样留出段（test）保持完整的名义跨度 —— 留出集是唯一无偏的读数，
        不该被间隔截短；而训练段少几天无关紧要。

          训练段 = [0, T-gap)
          验证段 = [T, V-gap)
          测试段 = [V, N)          ← 完整名义跨度

        污染判据：第 d 天的标签覆盖 [d, d+label_period]，因此
            a + label_period <= b   （训练段末 a，验证段首 b）
            c + label_period <= d   （验证段末 c，测试段首 d）
        取 gap = label_period 时两条恰好成立。
        """
        train_end = row_at(train_end_k)
        val_start = row_at(val_start_k)
        val_end = row_at(val_end_k)
        test_start = row_at(test_start_k) if has_test else n
        if not (0 < train_end < n):
            return None
        if has_val and not (train_end <= val_start <= val_end <= n):
            return None
        if has_test and not (0 < test_start <= n):
            return None
        return {
            "train_end": int(train_end),
            "val_start": int(val_start),
            "val_end": int(val_end),
            "test_start": int(test_start),
            "train_date_end": int(train_end_k),
            "val_date_start": int(val_start_k),
            "val_date_end": int(val_end_k),
            "test_date_start": int(test_start_k) if has_test else int(n_dates),
            "gap_dates": int(eff_gap),
            "gap_requested": int(gap),
            "gap_shrunk": bool(eff_gap < gap),
            "n_dates": int(n_dates),
        }

    if use_val:
        if test_ratio <= 0:
            # 保留"test_ratio=0 即不评估测试集"的旧语义：val 一直延伸到末尾
            T = _floor_ratio(n_dates, 1.0 - val_ratio)
            T = min(max(T, 1), n_dates - 1)
            eff_gap = min(gap, max(T - 1, 0))
            return _pack(T - eff_gap, T, n_dates, n_dates,
                         eff_gap, has_val=True, has_test=False)

        T = _floor_ratio(n_dates, 1.0 - val_ratio - test_ratio)   # 训练段名义末尾
        V = _floor_ratio(n_dates, 1.0 - test_ratio)                # 测试段名义起点
        T = min(max(T, 1), n_dates - 1)
        V = min(max(V, T + 1), n_dates - 1)
        # 间隔要放得下：训练段至少留 1 天，验证段至少留 1 天
        max_gap = max(min(T - 1, V - T - 1), 0)
        eff_gap = min(gap, max_gap)
        return _pack(T - eff_gap, T, V - eff_gap, V,
                     eff_gap, has_val=True, has_test=True)

    if test_ratio <= 0:
        return None
    T = _floor_ratio(n_dates, 1.0 - test_ratio)
    T = min(max(T, 1), n_dates - 1)
    eff_gap = min(gap, max(T - 1, 0))
    return _pack(T - eff_gap, T - eff_gap, T - eff_gap, T,
                 eff_gap, has_val=False, has_test=True)


def count_label_overlap_rows(date_codes, fit_mask, next_start_idx: int,
                              label_period: int) -> int:
    """统计训练集中"标签窗口伸进下一段"的样本行数。

    第 d 天的标签覆盖 [d, d+label_period]。下一段从第 next_start_idx 天
    开始，所以当 d + label_period >= next_start_idx 时，该训练样本的标签
    窗口已经看到了下一段的价格 —— 这就是折间泄漏。

    next_start_idx 是以**唯一日期**计的下标（与 split_indices_by_date
    返回的 train_date_end / oos_start 同一口径）。

    返回污染的训练行数；label_period <= 0 时返回 0。
    """
    if label_period is None or int(label_period) <= 0:
        return 0
    codes = np.asarray(date_codes)
    mask = np.asarray(fit_mask, dtype=bool)
    lo = int(next_start_idx) - int(label_period)
    return int((mask & (codes >= lo)).sum())



def sort_samples_by_date(X, y, dates, *extra):
    """按日期稳定排序 X/y/dates 以及可选的额外数组。

    dates 缺失或长度与 X 不匹配时原样返回，并标记为不可用。
    返回 (X, y, dates, *extra, has_dates)。
    """
    if dates is None or len(dates) != len(X):
        return (X, y, dates) + extra + (False,)

    order = np.argsort(np.asarray(dates), kind="stable")
    if not np.array_equal(order, np.arange(len(order))):
        X = X[order]
        y = y[order]
        dates = dates[order]
        extra = tuple(e[order] for e in extra)
    return (X, y, dates) + extra + (True,)


def build_walk_forward_folds(n_dates: int, train_len: int, oos_len: int,
                             gap: int, n_folds: int = 5,
                             step: Optional[int] = None,
                             window_mode: str = "rolling",
                             min_oos_frac: float = 0.66) -> List[dict]:
    """生成 walk-forward 折计划（单位：唯一日期数）。

    每个 fold:
      train_start, train_end (exclusive)
      oos_start, oos_end (exclusive)

    train_end = oos_start - gap，保证训练样本的标签前瞻窗口不伸进 OOS。
    window_mode:
      rolling  —— 训练窗口固定为 train_len 根，随 OOS 向前滚动
      anchored —— 训练从第 0 根开始，窗口随 OOS 增长

    step:
      显式传入时按该步长向前推，最多 n_folds 折；
      未传入时按 n_folds 在可用数据上均分，尽量让最后一折落在数据末尾。
      OOF 评估要求各折 OOS 不重叠，因此 step 会被抬到 >= oos_len。
    """
    if n_dates <= 0 or train_len <= 0 or oos_len <= 0:
        return []
    gap = max(0, int(gap))
    n_folds = max(1, int(n_folds))
    window_mode = window_mode if window_mode in ("rolling", "anchored") else "rolling"

    first_start = int(train_len) + gap
    last_start = int(n_dates) - int(oos_len)
    available = last_start - first_start

    if step is None or int(step) <= 0:
        if n_folds <= 1:
            step = max(1, available)
        else:
            step = max(1, available // (n_folds - 1))
    step = max(1, int(step))
    # OOF 需要每个样本只被一个 fold 预测
    step = max(step, int(oos_len))

    folds = []
    oos_start = first_start
    while oos_start < n_dates and len(folds) < n_folds:
        oos_end = min(oos_start + oos_len, n_dates)
        if oos_end - oos_start < max(1, int(oos_len * min_oos_frac)):
            break
        train_end = oos_start - gap
        train_start = 0 if window_mode == "anchored" else max(0, train_end - train_len)
        if train_start >= train_end:
            break
        folds.append({
            "index": len(folds),
            "train_start": int(train_start),
            "train_end": int(train_end),
            "oos_start": int(oos_start),
            "oos_end": int(oos_end),
        })
        oos_start += step
    return folds
