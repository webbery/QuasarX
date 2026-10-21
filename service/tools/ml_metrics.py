"""模型无关的评估指标。

这里只依赖 numpy，不依赖 xgboost/pandas，便于单测和被不同模型（XGBoost/
CNN/Transformer）复用。

约定：
  - classification 的 y_true / pred_idx 都是全局编码后的类别索引 0..K-1；
  - score 统一为“越大越看多”，用于 IC：multi:softprob 为 P(UP)-P(DOWN)，
    binary 为正类方向概率，regression 为预测收益本身；
  - 所有输出都经过 JSON 安全处理，NaN/Inf → None。
"""

import math

import numpy as np


def num(value):
    """JSON 安全的 float：NaN/Inf/不可转换 → None。"""
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if np.isfinite(v) else None


def newey_west_t(series, lag: int):
    """Newey-West HAC t 值和双侧 p 值（正态近似）。

    与 service/lab/feature_screen.py 的实现保持同一口径。
    """
    x = np.asarray(series, dtype=float)
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


def rankdata(values) -> np.ndarray:
    """平均秩（处理 ties），用于 Spearman IC。"""
    x = np.asarray(values, dtype=float)
    n = len(x)
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
    """Spearman rank IC；有效样本不足或任一侧无变化时返回 NaN。"""
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
    """优先截面 IC；横截面样本不足时退化为整段时序 IC。"""
    score = np.asarray(score, dtype=float)
    future = np.asarray(future, dtype=float)
    dates = np.unique(date_codes[mask])

    ics = []
    for d in dates:
        m = mask & (date_codes == d) & np.isfinite(score) & np.isfinite(future)
        if int(m.sum()) < 2:
            continue
        ic = spearman_ic(score[m], future[m])
        if np.isfinite(ic):
            ics.append(ic)

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
