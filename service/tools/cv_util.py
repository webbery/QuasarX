"""时序交叉验证的切分工具。

Phase 0 只做一件事：把样本按“日期”而不是“行号”切分。
matrix 模式下同一天会有多个标的的多行样本，按行号切会把同一天的标的
分到训练/测试两侧（截面泄漏），也会让“测试集”变成最后几个标的而不是
最后一段时间。这里先按日期稳定排序，再以唯一日期为边界切分。

Phase 1 的 walk-forward 折计划也放在本模块；评估指标和 NW t 在
ml_metrics.py，Dataset 组装在 ml_dataset.py。
"""

from typing import List, Optional, Sequence, Tuple

import numpy as np

# (train_end, val_end) 或 (split, None)
Split = Optional[Tuple[int, Optional[int]]]


def split_indices_by_date(dates: Sequence, use_val: bool,
                          val_ratio: float, test_ratio: float) -> Split:
    """按唯一日期计算 train/val/test 的行边界。

    参数
    ----
    dates : 已按时间升序排列的样本日期（可重复；同一个值代表同一天的多条样本）
    use_val : 是否切出验证集
    val_ratio, test_ratio : 验证/测试集的日期占比

    返回
    ----
    use_val=True  : (train_end, val_end)，三段为 [0,train_end)、
                    [train_end,val_end)、[val_end,n)
    use_val=False : (split, None)，两段为 [0,split)、[split,n)
    日期数不足或无法切出非空段时返回 None。

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

    def row_at(k: int) -> int:
        return first_idx[k] if k < n_dates else n

    if use_val:
        if test_ratio <= 0:
            # 保留“test_ratio=0 即不评估测试集”的旧语义：val 一直延伸到末尾
            train_k = int(n_dates * (1.0 - val_ratio))
            train_k = min(max(train_k, 1), n_dates - 1)
            return row_at(train_k), n
        train_k = int(n_dates * (1.0 - val_ratio - test_ratio))
        val_k = int(n_dates * (1.0 - test_ratio))
        # 每段至少留一个日期；val 段必须非空，且测试段非空
        train_k = min(max(train_k, 1), n_dates - 1)
        val_k = min(max(val_k, train_k + 1), n_dates - 1)
        if val_k <= train_k:
            return None
        train_end, val_end = row_at(train_k), row_at(val_k)
        if not (0 < train_end < val_end < n):
            return None
        return train_end, val_end

    if test_ratio <= 0:
        return n, None
    split_k = int(n_dates * (1.0 - test_ratio))
    split_k = min(max(split_k, 1), n_dates - 1)
    split = row_at(split_k)
    if not (0 < split < n):
        return None
    return split, None


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
