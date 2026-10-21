"""模型无关的评估指标。

指标计算本身只依赖 numpy（不依赖 xgboost），统计口径（Newey-West、
Spearman IC）统一来自 ml_stats.py，这里不再保留第二份实现——历史上
ml_metrics 与 feature_screen 各写了一套 NW-t，注释写着"同一口径"，
代码却是两份，归一化方式还不同。

约定：
  - classification 的 y_true / pred_idx 都是全局编码后的类别索引 0..K-1；
  - score 统一为“越大越看多”，用于 IC：multi:softprob 为 P(UP)-P(DOWN)，
    binary 为正类方向概率，regression 为预测收益本身；
  - 所有输出都经过 JSON 安全处理，NaN/Inf → None。
"""

import numpy as np

# 统计口径的唯一实现。这里重新导出，保持 ml_metrics 原有的公开接口不变
# （ml_cv.py / ml_adapter.py / test_ml_pipeline.py 都从这里导入）。
from ml_stats import (daily_cs_ic_long, newey_west_t, num, rankdata,
                      spearman_ic)

__all__ = [
    "num", "rankdata", "spearman_ic", "newey_west_t",
    "classification_metrics", "regression_metrics", "ic_metrics",
    "learning_curve", "predictions_payload",
]


def classification_metrics(y_true, pred_idx, proba, n_classes):
    """分类指标；proba 为 1D（binary）或 2D（multi）。"""
    y_true = np.asarray(y_true, dtype=int)
    pred_idx = np.asarray(pred_idx, dtype=int)
    n = len(y_true)
    if n == 0:
        return {}

    out = {"accuracy": num(np.mean(pred_idx == y_true))}
    f1s, precs, recs, supports = [], [], [], []
    for c in range(n_classes):
        tp = int(np.sum((pred_idx == c) & (y_true == c)))
        fp = int(np.sum((pred_idx == c) & (y_true != c)))
        fn = int(np.sum((pred_idx != c) & (y_true == c)))
        prec = tp / (tp + fp) if (tp + fp) else 0.0
        rec = tp / (tp + fn) if (tp + fn) else 0.0
        f1 = 2.0 * prec * rec / (prec + rec) if (prec + rec) else 0.0
        f1s.append(f1)
        precs.append(prec)
        recs.append(rec)
        supports.append(int(np.sum(y_true == c)))

    f1_w = float(np.dot(f1s, supports) / n)
    prec_w = float(np.dot(precs, supports) / n)
    rec_w = float(np.dot(recs, supports) / n)
    # 兼容旧结果的 key（前端一直在用 f1/precision/recall）
    out["f1"] = num(f1_w)
    out["precision"] = num(prec_w)
    out["recall"] = num(rec_w)
    out["f1_macro"] = num(np.mean(f1s))
    out["f1_weighted"] = num(f1_w)
    out["precision_weighted"] = num(prec_w)
    out["recall_weighted"] = num(rec_w)

    proba = np.asarray(proba)
    eps = 1e-15
    if proba.ndim == 2:
        p_true = proba[np.arange(n), y_true]
    else:
        p_true = np.where(y_true == 1, proba, 1.0 - proba)
    p_true = np.clip(p_true, eps, 1.0 - eps)
    out["logloss"] = num(-np.mean(np.log(p_true)))

    if n_classes == 2:
        pos = y_true == 1
        n_pos = int(pos.sum())
        n_neg = n - n_pos
        if n_pos > 0 and n_neg > 0:
            score = proba if proba.ndim == 1 else proba[:, 1]
            ranks = rankdata(score)
            auc = (ranks[pos].sum() - n_pos * (n_pos + 1) / 2.0) / (n_pos * n_neg)
            out["auc"] = num(auc)
    return out


def regression_metrics(y_true, pred):
    y_true = np.asarray(y_true, dtype=float)
    pred = np.asarray(pred, dtype=float)
    m = np.isfinite(y_true) & np.isfinite(pred)
    if not m.any():
        return {}
    yt, yp = y_true[m], pred[m]
    resid = yp - yt
    out = {
        "rmse": num(np.sqrt(np.mean(resid ** 2))),
        "mae": num(np.mean(np.abs(resid))),
    }
    ss_tot = float(np.sum((yt - yt.mean()) ** 2))
    ss_res = float(np.sum(resid ** 2))
    out["r2"] = num(1.0 - ss_res / ss_tot) if ss_tot > 1e-18 else None
    return out


def ic_metrics(score, future, date_codes, mask, nw_lag):
    """优先截面 IC；横截面样本不足时退化为整段时序 IC。

    逐日 IC 与 feature_screen 共用 ml_stats.daily_cs_ic_long，口径一致。
    """
    score = np.asarray(score, dtype=float)
    future = np.asarray(future, dtype=float)

    _, ics = daily_cs_ic_long(score, future, date_codes, mask, min_names=2)

    if len(ics) >= 5:
        arr = np.asarray(ics, dtype=float)
        std = float(arr.std(ddof=1)) if len(arr) > 1 else 0.0
        t, p = newey_west_t(arr, nw_lag)
        return {
            "kind": "cross_sectional",
            "mean": num(arr.mean()),
            "std": num(std),
            "ir": num(arr.mean() / std) if std > 1e-12 else None,
            "nw_t": num(t),
            "p_value": num(p),
            "days": int(len(arr)),
            "positive_ratio": num((arr > 0).mean()),
        }

    m = mask & np.isfinite(score) & np.isfinite(future)
    ic = spearman_ic(score[m], future[m]) if int(m.sum()) >= 3 else float("nan")
    return {
        "kind": "time_series",
        "mean": num(ic),
        "std": None,
        "ir": None,
        "nw_t": None,
        "p_value": None,
        "days": int(np.unique(date_codes[m]).size) if int(m.sum()) else 0,
        "positive_ratio": None,
    }


def learning_curve(history):
    """从 xgboost evals_result（或同结构 dict）提取 train/eval 曲线。"""
    def first_metric(d):
        for key in ("logloss", "mlogloss", "rmse"):
            if key in d:
                return d[key]
        return next(iter(d.values()), []) if d else []

    train_losses = first_metric(history.get("train", {}))
    eval_losses = first_metric(history.get("eval", {}))
    out = []
    for i in range(min(len(train_losses), len(eval_losses))):
        out.append({
            "iteration": i,
            "train_loss": num(train_losses[i]),
            "eval_loss": num(eval_losses[i]),
        })
    return out


def predictions_payload(y_true, pred_idx, proba, pred_value,
                        is_classification, classes):
    """构造前端使用的 predictions 列表，actual/pred_class 还原为原始类别。"""
    out = []
    for i in range(len(y_true)):
        if is_classification:
            p = float(np.max(proba[i])) if np.ndim(proba) == 2 else float(proba[i])
            out.append({
                "actual": int(classes[int(y_true[i])]),
                "predicted": p,
                "pred_class": int(classes[int(pred_idx[i])]),
            })
        else:
            out.append({
                "actual": float(y_true[i]),
                "predicted": float(pred_value[i]),
                "pred_class": 0,
            })
    return out
