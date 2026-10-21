"""模型无关的验证/训练流程。

两个入口：
  - run_single_split：保留原来的单次 train/val/test 时序切分；
  - run_walk_forward：walk-forward 折循环，只做 OOF 评估，最终模型全量重训。

两者都通过 ModelAdapter 调模型，不直接依赖 xgboost；折计划、gap、OOF、
指标和结果结构在这里统一，后续 CNN/Transformer 复用同一套流程。
"""

import numpy as np

from cv_util import build_walk_forward_folds, split_indices_by_date
from ml_metrics import (classification_metrics, ic_metrics,
                        predictions_payload, regression_metrics)


def _date_str(d):
    return str(d)[:10]


def _date_codes(dates):
    unique, codes = np.unique(np.asarray(dates), return_inverse=True)
    return unique, codes


def _make_result(dataset, adapter, model, best_rounds, history,
                 eval_metrics, predictions, X_test, test_dates,
                 n_train, n_val, n_test, split_mode, learning_history=None):
    return {
        "model_path": None,  # 由调用方补
        "best_iteration": int(best_rounds) - 1,
        "n_train": int(n_train),
        "n_val": int(n_val),
        "n_test": int(n_test),
        "n_features": len(dataset.feature_cols),
        "features": dataset.feature_cols,
        "classes": dataset.classes,
        "split_mode": split_mode,
        "test_date_start": test_dates[0] if test_dates else "",
        "test_date_end": test_dates[-1] if test_dates else "",
        "learning_curve": adapter.learning_curve(
            history if learning_history is None else learning_history),
        "feature_importance": adapter.feature_importance(model, dataset.feature_cols),
        "eval_metrics": eval_metrics,
        "predictions": predictions,
        "X_test": X_test.tolist() if len(X_test) else [],
        "X_test_dates": test_dates,
    }


def run_single_split(dataset, adapter, test_ratio, val_ratio,
                     model_output, emit):
    """单次 train/val/test 时序切分（原训练路径）。"""
    X, y = dataset.X, dataset.y
    n = len(X)
    if n == 0:
        raise ValueError("训练数据为空")

    use_val = val_ratio > 0 and n >= 1000
    split = None
    if dataset.dates is not None and len(dataset.dates) == n:
        split = split_indices_by_date(dataset.dates, use_val, val_ratio, test_ratio)
        if split is None and use_val:
            emit({"type": "warning",
                  "message": "日期数不足以切出 train/val/test 三段，退化为 train/test 两段"})
            use_val = False
            split = split_indices_by_date(dataset.dates, False, val_ratio, test_ratio)
        if split is None:
            emit({"type": "warning",
                  "message": "日期切分失败，退化为按行切分（多标的场景可能出现同日跨集）"})

    if split is not None:
        if use_val:
            train_end, val_end = split
        else:
            train_end, val_end = split[0], None
        split_mode = "date"
    else:
        if use_val:
            train_end = int(n * (1 - val_ratio - test_ratio))
            val_end = int(n * (1 - test_ratio))
        else:
            train_end = int(n * (1 - test_ratio))
            val_end = None
        split_mode = "row"

    if use_val:
        X_train, X_val, X_test = X[:train_end], X[train_end:val_end], X[val_end:]
        y_train, y_val, y_test = y[:train_end], y[train_end:val_end], y[val_end:]
        test_start = val_end
        n_val = len(X_val)
    else:
        X_val, y_val = None, None
        X_train, X_test = X[:train_end], X[train_end:]
        y_train, y_test = y[:train_end], y[train_end:]
        test_start = train_end
        n_val = 0

    test_dates = []
    if dataset.dates is not None and hasattr(dataset.dates, "strftime"):
        test_dates = dataset.dates[test_start:].strftime("%Y-%m-%d").tolist()

    emit({"type": "progress", "phase": "split",
          "n_train": len(X_train), "n_val": n_val, "n_test": len(X_test),
          "n_features": X.shape[1], "split_mode": split_mode})

    fit = adapter.fit(
        X_train, y_train, X_val, y_val,
        progress_cb=lambda p: emit({"type": "progress", "phase": "training", **p}),
    )

    eval_metrics = {}
    predictions = []
    if len(X_test) > 0:
        raw = adapter.predict(fit.model, X_test)
        score, pred_idx, proba = adapter.decode(raw, dataset.classes)
        if dataset.is_classification:
            eval_metrics = classification_metrics(
                y_test, pred_idx, proba, dataset.n_classes)
        else:
            eval_metrics = regression_metrics(y_test, score)
        predictions = predictions_payload(
            y_test, pred_idx, proba,
            score if not dataset.is_classification else None,
            dataset.is_classification, dataset.classes)

    adapter.save(fit.model, model_output)

    result = _make_result(
        dataset, adapter, fit.model, fit.best_rounds, fit.history,
        eval_metrics, predictions, X_test, test_dates,
        n_train=len(X_train), n_val=n_val, n_test=len(X_test),
        split_mode=split_mode,
    )
    result["model_path"] = model_output
    return result


def run_walk_forward(dataset, adapter, cv_cfg, label_period,
                     inner_val_ratio, model_output, emit):
    """walk-forward OOF 评估 + 全量 refit。"""
    if dataset.dates is None or len(dataset.dates) != len(dataset.X):
        raise ValueError("CV 需要 CSV 的 date 列，且日期数必须与样本数一致")
    if dataset.future is None or len(dataset.future) != len(dataset.X):
        raise ValueError("CV 需要与样本对齐的 future return 用于 IC 计算")

    scheme = cv_cfg.get("scheme", "walk_forward")
    if scheme != "walk_forward":
        raise ValueError(f"cv.scheme='{scheme}' 暂不支持，Phase 1 仅支持 walk_forward")
    final_model = cv_cfg.get("final_model", "refit_full")
    if final_model != "refit_full":
        raise ValueError(f"cv.final_model='{final_model}' 暂不支持，Phase 1 仅支持 refit_full")

    X, y, future = dataset.X, dataset.y, dataset.future
    n = len(X)
    unique_dates, date_codes = _date_codes(dataset.dates)
    n_dates = len(unique_dates)

    wf = cv_cfg.get("walk_forward") or {}
    train_len = int(wf.get("train_len", 0) or 0)
    oos_len = int(wf.get("oos_len", 0) or 0)
    if train_len <= 0 or oos_len <= 0:
        raise ValueError("cv.walk_forward.train_len / oos_len 必须为正整数")

    step_cfg = wf.get("step", None)
    window_mode = wf.get("window_mode", "rolling")
    n_folds = int(cv_cfg.get("n_folds", 5) or 5)
    inner_val_ratio = float(wf.get("inner_val_ratio", inner_val_ratio) or 0.0)
    inner_val_ratio = min(max(inner_val_ratio, 0.0), 0.5)

    nw_cfg = cv_cfg.get("nw") or {}
    nw_lag = int(nw_cfg.get("lag", 0) or 0)
    if nw_lag <= 0:
        nw_lag = max(10, 2 * int(label_period))

    gap_cfg = cv_cfg.get("gap") or {}
    gap_mode = gap_cfg.get("mode", "label")
    if "bars" in gap_cfg:
        gap_bars = int(gap_cfg.get("bars") or 0)
    elif gap_mode == "nw":
        gap_bars = max(int(label_period), nw_lag)
    else:
        gap_bars = int(label_period)
    if gap_bars < int(label_period):
        emit({"type": "warning",
              "message": f"gap_bars={gap_bars} < label_period={label_period}，已抬到 label_period"})
        gap_bars = int(label_period)

    folds = build_walk_forward_folds(
        n_dates=n_dates, train_len=train_len, oos_len=oos_len,
        gap=gap_bars, n_folds=n_folds, step=step_cfg,
        window_mode=window_mode,
    )
    if len(folds) < 2:
        raise ValueError(
            f"walk-forward 只生成 {len(folds)} 折；检查 n_dates={n_dates}, "
            f"train_len={train_len}, oos_len={oos_len}, gap={gap_bars}, n_folds={n_folds}")

    oof_score = np.full(n, np.nan)
    oof_pred_idx = np.full(n, -1, dtype=int)
    oof_future = np.full(n, np.nan)
    oof_seen = np.zeros(n, dtype=bool)
    oof_proba = None

    fold_results = []
    fold_rounds = []
    last_payload = None

    for f in folds:
        f_start, f_end = f["train_start"], f["train_end"]
        fit_mask = (date_codes >= f_start) & (date_codes < f_end)
        val_mask = None

        # 折内 early stopping：训练段末尾留 inner_val_ratio，并在 inner train
        # 和 inner val 之间留 gap，避免用 inner val 的标签挑轮数。
        if inner_val_ratio > 0:
            train_n = f_end - f_start
            val_n = int(train_n * inner_val_ratio)
            if val_n >= 1 and train_n - val_n > gap_bars + 1:
                val_start = f_end - val_n
                inner_train_end = val_start - gap_bars
                if inner_train_end > f_start:
                    fit_mask = (date_codes >= f_start) & (date_codes < inner_train_end)
                    val_mask = (date_codes >= val_start) & (date_codes < f_end)
                    if not np.any(val_mask):
                        val_mask = None

        if not np.any(fit_mask):
            emit({"type": "warning", "message": f"fold {f['index']} 训练集为空，跳过"})
            continue

        if dataset.is_classification:
            present = set(np.unique(y[fit_mask]).astype(int).tolist())
            missing_idx = [i for i in range(dataset.n_classes) if i not in present]
            if missing_idx:
                emit({"type": "warning",
                      "message": f"fold {f['index']} 训练集缺少类别索引 {missing_idx}"
                                 f"（classes={dataset.classes}）"})
                if len(present) < 2:
                    emit({"type": "warning", "message": f"fold {f['index']} 只有单一类别，跳过"})
                    continue

        X_fit, y_fit = X[fit_mask], y[fit_mask]
        X_val = y_val = None
        if val_mask is not None:
            X_val, y_val = X[val_mask], y[val_mask]

        fit = adapter.fit(X_fit, y_fit, X_val, y_val)
        fold_rounds.append(fit.best_rounds)

        oos_mask = (date_codes >= f["oos_start"]) & (date_codes < f["oos_end"])
        if not np.any(oos_mask):
            emit({"type": "warning", "message": f"fold {f['index']} OOS 为空，跳过"})
            continue
        if np.any(oof_seen[oos_mask]):
            raise ValueError(f"fold {f['index']} 的 OOS 与之前的折重叠，OOF 无法唯一归属")

        raw = adapter.predict(fit.model, X[oos_mask])
        score, pred_idx, proba = adapter.decode(raw, dataset.classes)

        if dataset.is_classification and oof_proba is None:
            if np.ndim(proba) == 2:
                oof_proba = np.full((n, np.asarray(proba).shape[1]), np.nan)
            else:
                oof_proba = np.full(n, np.nan)

        y_oos = y[oos_mask]
        fut_oos = future[oos_mask]

        oof_seen[oos_mask] = True
        oof_score[oos_mask] = score
        oof_pred_idx[oos_mask] = pred_idx
        oof_future[oos_mask] = fut_oos
        if dataset.is_classification:
            if oof_proba.ndim == 2:
                oof_proba[oos_mask, :] = proba
            else:
                oof_proba[oos_mask] = proba

        metrics = (classification_metrics(y_oos, pred_idx, proba, dataset.n_classes)
                   if dataset.is_classification else regression_metrics(y_oos, score))
        ic = ic_metrics(oof_score, oof_future, date_codes, oos_mask, nw_lag)

        fold_results.append({
            "index": int(f["index"]),
            "train_start": _date_str(unique_dates[f_start]),
            "train_end": _date_str(unique_dates[f_end - 1]),
            "oos_start": _date_str(unique_dates[f["oos_start"]]),
            "oos_end": _date_str(unique_dates[f["oos_end"] - 1]),
            "n_train": int(fit_mask.sum()),
            "n_inner_val": int(val_mask.sum()) if val_mask is not None else 0,
            "n_oos": int(oos_mask.sum()),
            "best_rounds": int(fit.best_rounds),
            "metrics": metrics,
            "ic": ic,
        })

        last_payload = {
            "mask": oos_mask,
            "y": y_oos,
            "pred_idx": pred_idx,
            "proba": proba,
            "score": score,
            "history": fit.history,
        }
        emit({"type": "info", "phase": "cv_fold_done", "fold": int(f["index"]),
              "n_folds": len(folds), "n_train": int(fit_mask.sum()),
              "n_oos": int(oos_mask.sum()), "best_rounds": int(fit.best_rounds),
              "metrics": metrics})

    if not fold_results or last_payload is None:
        raise ValueError("没有成功的 walk-forward 折")

    oof_mask = oof_seen
    n_oof = int(oof_mask.sum())
    if dataset.is_classification:
        oof_metrics = classification_metrics(
            y[oof_mask], oof_pred_idx[oof_mask],
            oof_proba[oof_mask], dataset.n_classes)
    else:
        oof_metrics = regression_metrics(y[oof_mask], oof_score[oof_mask])
    oof_ic = ic_metrics(oof_score, oof_future, date_codes, oof_mask, nw_lag)

    final_rounds = adapter.final_rounds(fold_rounds)
    final_model_obj = adapter.refit(X, y, final_rounds)
    adapter.save(final_model_obj, model_output)

    last_mask = last_payload["mask"]
    X_test = X[last_mask]
    test_dates = [_date_str(d) for d in np.asarray(dataset.dates)[last_mask]]
    predictions = predictions_payload(
        last_payload["y"], last_payload["pred_idx"],
        last_payload["proba"], last_payload["score"],
        dataset.is_classification, dataset.classes)

    step_used = (folds[1]["oos_start"] - folds[0]["oos_start"]) if len(folds) > 1 else oos_len
    cv_block = {
        "enabled": True,
        "scheme": "walk_forward",
        "n_folds_requested": n_folds,
        "n_folds": len(fold_results),
        "window_mode": window_mode,
        "train_len": train_len,
        "oos_len": oos_len,
        "step": int(step_used),
        "gap_bars": int(gap_bars),
        "gap_mode": gap_mode,
        "nw_lag": int(nw_lag),
        "inner_val_ratio": inner_val_ratio,
        "final_model": "refit_full",
        "final_rounds": int(final_rounds),
        "n_oof": n_oof,
        "folds": fold_results,
        "oof_metrics": oof_metrics,
        "oof_ic": oof_ic,
        "leakage_checks": {
            "gap_ge_label_period": bool(gap_bars >= int(label_period)),
            "overlap_rows": 0,
            "oof_rows": n_oof,
        },
    }

    result = _make_result(
        dataset, adapter, final_model_obj, final_rounds, {},
        oof_metrics, predictions, X_test, test_dates,
        n_train=len(X), n_val=n_oof, n_test=int(last_mask.sum()),
        split_mode="cv_walk_forward",
        learning_history=last_payload["history"],
    )
    result["model_path"] = model_output
    result["cv"] = cv_block
    return result
