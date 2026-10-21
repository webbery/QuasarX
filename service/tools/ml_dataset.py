"""模型无关的数据集构建。

把 C++ collect 产出的宽表 CSV 转成训练用的 Dataset：
  - matrix 模式：每个标的使用自己的 close 算标签，特征按标的匿名化后拼接；
  - vector 模式：所有标的共享一个 label source；
  - 统一做 NaN 过滤、按日期排序、全局类别编码。

后续 CNN/Transformer 也复用这个 Dataset，只是 adapter 自己决定如何把
X 组织成张量。
"""

from dataclasses import dataclass
from typing import List, Optional

import numpy as np
import pandas as pd

from cv_util import sort_samples_by_date
from xgb_util import compute_label


@dataclass
class Dataset:
    X: np.ndarray
    y: np.ndarray                 # 分类：全局编码 0..K-1；回归：future return
    future: np.ndarray            # 未来收益，用于 IC
    dates: Optional[pd.DatetimeIndex]
    symbols: np.ndarray           # 每行对应的标的（vector 模式为 "__single__"）
    feature_cols: List[str]
    classes: Optional[List[int]]
    is_classification: bool
    n_classes: int
    label_type: str = "classification"
    label_period: int = 5

    @property
    def n_samples(self) -> int:
        return len(self.X)


def prepare_dataframe(df: pd.DataFrame, start_date: str = "",
                      end_date: str = "") -> pd.DataFrame:
    """日期过滤 + date 列转 index + 按时间排序。"""
    if "date" not in df.columns:
        return df
    df["date"] = pd.to_datetime(df["date"])
    if start_date:
        df = df[df["date"] >= start_date]
    if end_date:
        df = df[df["date"] <= end_date]
    return df.set_index("date").sort_index()


def _parse_symbols(df: pd.DataFrame) -> List[str]:
    prefixes = set()
    for col in df.columns:
        if col == "date":
            continue
        parts = col.split(".")
        if len(parts) >= 3:
            prefixes.add(f"{parts[0]}.{parts[1]}.")
    return sorted(prefixes)


def _build_matrix_dataset(df, label_period, label_type, vol_k,
                          sigma_mode, emit):
    symbols = _parse_symbols(df)
    if not symbols:
        raise ValueError("matrix 模式无法从列名解析标的前缀（期望 market.code.feature 格式）")

    emit({"type": "info", "phase": "label_shape",
          "shape": "matrix", "symbols": symbols, "n_symbols": len(symbols)})

    first_prefix = symbols[0]
    feature_names = [c[len(first_prefix):] for c in df.columns
                     if c.startswith(first_prefix)]
    close_candidates = ["quote.close", "input.close", "close"]
    close_names = {c for c in close_candidates if c in feature_names}
    base_features = [f for f in feature_names if f not in close_names]
    if not base_features:
        raise ValueError("matrix 模式没有可用的特征列（除 close 外）")

    X_parts, y_parts, future_parts = [], [], []
    date_parts, symbol_parts = [], []
    has_dates = isinstance(df.index, pd.DatetimeIndex)

    for sym in symbols:
        sym_cols = [sym + f for f in base_features if (sym + f) in df.columns]
        if len(sym_cols) != len(base_features):
            missing = [f for f in base_features if (sym + f) not in df.columns]
            emit({"type": "warning",
                  "message": f"标的 {sym} 缺少特征 {missing}，跳过"})
            continue

        close_series = None
        for cand in close_candidates:
            if (sym + cand) in df.columns:
                close_series = df[sym + cand]
                break
        if close_series is None and (sym + "close") in df.columns:
            close_series = df[sym + "close"]
        if close_series is None:
            emit({"type": "warning", "message": f"标的 {sym} 无 close 列，无法计算标签，跳过"})
            continue

        sym_df = df[sym_cols].rename(columns=lambda c, p=sym: c[len(p):])
        sym_label, sym_future = compute_label(
            close_series, label_period, label_type, vol_k, sigma_mode)
        valid = sym_label.notna() & sym_df.notna().all(axis=1)

        n_valid = int(valid.sum())
        if n_valid == 0:
            emit({"type": "warning", "message": f"标的 {sym} 标签/特征过滤后无有效样本，跳过"})
            continue

        X_parts.append(sym_df.loc[valid].values)
        y_parts.append(sym_label[valid].values)
        future_parts.append(sym_future[valid].values)
        symbol_parts.append(np.full(n_valid, sym, dtype=object))
        if has_dates:
            date_parts.append(sym_df.index[valid])

    if not X_parts:
        raise ValueError("matrix 模式：所有标的均无有效数据")

    X = np.vstack(X_parts)
    y = np.concatenate(y_parts)
    future = np.concatenate(future_parts)
    symbols_arr = np.concatenate(symbol_parts)
    dates = None
    if has_dates:
        dates = pd.DatetimeIndex(
            np.concatenate([np.asarray(d) for d in date_parts]))

    emit({"type": "info", "phase": "matrix_reshape",
          "symbols": len(symbols),
          "samples_per_symbol": [len(xp) for xp in X_parts],
          "total_samples": len(X), "n_features": len(base_features),
          "features": base_features})
    return X, y, future, dates, symbols_arr, base_features


def _build_vector_dataset(df, label_source, label_period, label_type,
                          vol_k, sigma_mode, emit):
    if not label_source:
        raise ValueError("vector 模式需要指定 label.source")
    if label_source not in df.columns:
        raise ValueError(f"标签来源列 '{label_source}' 不在数据中。可用列: {list(df.columns)}")

    raw_label, raw_future = compute_label(
        df[label_source], label_period, label_type, vol_k, sigma_mode)

    feature_cols = [c for c in df.columns if c not in {label_source, "date"}]
    if not feature_cols:
        raise ValueError("没有可用的特征列")

    nan_stats = {c: int(df[c].notna().sum()) for c in feature_cols}
    total_rows = len(df)
    all_nan_cols = [c for c, cnt in nan_stats.items() if cnt == 0]
    partial_nan_cols = [c for c, cnt in nan_stats.items() if cnt < total_rows]
    emit({"type": "info", "phase": "nan_stats",
          "total_rows": total_rows,
          "all_nan_cols": all_nan_cols,
          "partial_nan_cols_count": len(partial_nan_cols),
          "fully_valid_cols": total_rows - len(all_nan_cols)})
    if all_nan_cols:
        feature_cols = [c for c in feature_cols if c not in set(all_nan_cols)]
        emit({"type": "info", "phase": "drop_all_nan",
              "dropped": all_nan_cols, "remaining_features": len(feature_cols)})
    if not feature_cols:
        raise ValueError(f"所有 {total_rows} 行特征数据均为 NaN，无法训练")

    valid = raw_label.notna() & df[feature_cols].notna().all(axis=1)
    X = df.loc[valid, feature_cols].values
    y = raw_label[valid].values
    future = raw_future[valid].values
    if len(X) == 0:
        col_valid = {c: int(df[c].notna().sum()) for c in feature_cols}
        label_valid = int(raw_label.notna().sum())
        partial = sorted(
            [(c, v) for c, v in col_valid.items() if v < total_rows],
            key=lambda kv: kv[1])
        partial_detail = []
        for c, v in partial[:6]:
            nan_idx = df.index[df[c].isna()].tolist()
            if len(nan_idx) <= 10:
                idx_str = ",".join(str(i) for i in nan_idx)
            else:
                idx_str = (f"{','.join(str(i) for i in nan_idx[:5])}..."
                           f"{','.join(str(i) for i in nan_idx[-5:])} (共{len(nan_idx)}个)")
            partial_detail.append(f"{c}={v}/{total_rows} (NaN@[{idx_str}])")
        if len(partial) > 6:
            partial_detail.append(f"...其余 {len(partial) - 6} 列省略")
        raise ValueError(
            f"标签过滤后无有效样本。标签有效: {label_valid}/{total_rows}。"
            f"日期范围: {df.index.min()} ~ {df.index.max()}。"
            f"部分 NaN 特征列 ({len(partial)} 个): {'; '.join(partial_detail)}。"
            f"可能原因：特征间时间对齐不一致，或标签来源列与特征列日期范围不重叠。")

    dates = df.index[valid.to_numpy()] if isinstance(df.index, pd.DatetimeIndex) else None
    symbols = np.full(len(X), "__single__", dtype=object)
    return X, y, future, dates, symbols, feature_cols


def build_dataset(df: pd.DataFrame, label_source: str, label_period: int,
                  label_type: str, label_shape: str, vol_k: float,
                  sigma_mode: str, emit) -> Dataset:
    """构建 Dataset；分类标签在这里做全局编码，保证各折类别空间一致。"""
    # 直接调用本模块时允许传入带 date 列的原始 DataFrame
    if "date" in df.columns and not isinstance(df.index, pd.DatetimeIndex):
        df = prepare_dataframe(df)

    if label_shape == "matrix":
        X, y, future, dates, symbols, feature_cols = _build_matrix_dataset(
            df, label_period, label_type, vol_k, sigma_mode, emit)
    else:
        X, y, future, dates, symbols, feature_cols = _build_vector_dataset(
            df, label_source, label_period, label_type, vol_k, sigma_mode, emit)

    if len(X) == 0:
        raise ValueError("标签/特征过滤后无有效样本")

    X, y, dates, future, symbols, has_dates = sort_samples_by_date(
        X, y, dates, future, symbols)
    if not has_dates:
        emit({"type": "warning",
              "message": "CSV 无可用 date 列；CV 需要日期，单次切分将退回按行切分"})

    is_classification = label_type == "classification"
    classes = None
    n_classes = 0
    if is_classification:
        unique_labels = sorted(set(int(v) for v in y))
        n_classes = len(unique_labels)
        if n_classes <= 1:
            raise ValueError(f"标签只有 {n_classes} 个类别，无法训练分类模型")
        classes = unique_labels
        label_to_idx = {c: i for i, c in enumerate(classes)}
        y = np.array([label_to_idx[int(v)] for v in y], dtype=float)
        emit({"type": "info", "phase": "label_stats", "n_classes": n_classes,
              "classes": classes})

    return Dataset(
        X=X, y=y, future=future, dates=dates, symbols=symbols,
        feature_cols=feature_cols, classes=classes,
        is_classification=is_classification, n_classes=n_classes,
        label_type=label_type, label_period=label_period,
    )
