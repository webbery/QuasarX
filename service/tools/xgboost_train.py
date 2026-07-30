"""
XGBoost 离线训练脚本（QuasarX 后端调用）

输入参数：
  --data           特征数据 CSV 路径（C++ 端生成，列 = 节点输出变量名 + 时间序列）
  --label-source   标签来源变量名（如 "sh.600000.close"）
  --label-period   未来收益周期 N（默认 5）
  --label-type     classification / regression
  --vol-k          自适应阈值系数 vol_k (threshold = vol_k × σ × √N, 默认 0.5)
  --objective      XGBoost 目标函数：multi:softprob / binary:logistic / reg:squarederror（分类时自动推导）
  --model-output   模型保存路径（.json 格式）
  --params         超参数 JSON 字符串
  --test-ratio     测试集比例（默认 0.2，时序切分）
  --xtest-output   X_test 保存路径（供 SHAP 计算，可选）
  --ytest-output   y_test 保存路径（可选）

输出（stdout 每行 JSON）：
  {"type":"progress", "iteration":N, "train_loss":..., "eval_loss":...}
  {"type":"result", "model_path":"...", "learning_curve":[...], "feature_importance":[...],
   "eval_metrics":{...}, "predictions":[...], "n_train":N, "n_test":N}

依赖：xgboost, pandas, scikit-learn, numpy
"""

import argparse
import json
import os
import sys
import time
import traceback
import numpy as np
import pandas as pd

# 强制 stdout/stderr 无缓冲，防止管道模式下进程退出时数据丢失
sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

try:
    import xgboost as xgb
except ImportError:
    print(json.dumps({"type": "error", "message": "未安装 xgboost: pip install xgboost"}), flush=True)
    sys.exit(1)


def emit(obj):
    """输出一行紧凑 JSON 到 stdout（无空格分隔符）"""
    print(json.dumps(obj, ensure_ascii=False, default=str, separators=(",", ":")), flush=True)


def compute_label(values, period, label_type, vol_k):
    """计算标签：future_return = source[t+N]/source[t] - 1

    classification (自适应三分类):
      threshold = vol_k × σ × √N  (σ = 对数收益率标准差)
      future > +threshold  → 0 (UP)
      future < -threshold  → 2 (DOWN)
      otherwise            → 1 (FLAT)
    regression: label = future_return
    """
    s = pd.Series(values, dtype=float)
    future = s.shift(-period) / s - 1.0
    if label_type == "classification":
        log_ret = np.log(s / s.shift(1))
        log_ret = log_ret.replace([np.inf, -np.inf], np.nan).dropna()
        sigma = log_ret.std()
        if len(log_ret) < 20 or sigma < 1e-12:
            threshold = 0.015
        else:
            threshold = vol_k * sigma * np.sqrt(period)
            threshold = float(np.clip(threshold, 0.005, 0.10))
        label = pd.Series(np.nan, index=future.index)
        label[future > threshold] = 0    # UP
        label[future < -threshold] = 2   # DOWN
        label[(future >= -threshold) & (future <= threshold)] = 1  # FLAT
    else:
        label = future
    return label, future


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True, help="特征数据 CSV 路径")
    parser.add_argument("--label-source", required=True, help="标签来源列名")
    parser.add_argument("--label-period", type=int, default=5, help="未来收益周期 N")
    parser.add_argument("--label-type", choices=["classification", "regression"],
                        default="classification")
    parser.add_argument("--vol-k", type=float, default=0.5, help="自适应阈值系数 vol_k (threshold = vol_k × σ × √N)")
    parser.add_argument("--objective", default="multi:softprob")
    parser.add_argument("--num-class", type=int, default=3)
    parser.add_argument("--model-output", required=True, help="模型保存路径")
    parser.add_argument("--params", default="{}", help="XGBoost 超参数 JSON")
    parser.add_argument("--test-ratio", type=float, default=0.2)
    parser.add_argument("--start-date", default="", help="训练数据开始日期 (YYYY-MM-DD)")
    parser.add_argument("--end-date", default="", help="训练数据结束日期 (YYYY-MM-DD)")
    parser.add_argument("--frequency", default="1d", help="数据频率 (1d/1w/1M)")
    parser.add_argument("--xtest-output", default="", help="X_test 保存路径")
    parser.add_argument("--ytest-output", default="", help="y_test 保存路径")
    args = parser.parse_args()

    try:
        params = json.loads(args.params) if args.params else {}
    except json.JSONDecodeError as e:
        emit({"type": "error", "message": f"params JSON 解析失败: {e}"})
        sys.exit(1)

    df = pd.read_csv(args.data)
    emit({"type": "progress", "phase": "load_data", "rows": len(df), "cols": len(df.columns)})

    # 日期过滤（如果 CSV 包含 date 列且指定了日期范围）
    if "date" in df.columns and (args.start_date or args.end_date):
        df["date"] = pd.to_datetime(df["date"])
        if args.start_date:
            df = df[df["date"] >= args.start_date]
        if args.end_date:
            df = df[df["date"] <= args.end_date]
        df = df.drop(columns=["date"])
        emit({"type": "info", "phase": "date_filter",
              "start": args.start_date, "end": args.end_date, "rows_after": len(df)})
    elif args.start_date or args.end_date:
        emit({"type": "info", "phase": "date_filter",
              "message": "CSV 无 date 列，跳过日期过滤。需 C++ 端 writeCsv 输出日期列"})

    # 频率重采样（如果指定了非日级频率且 CSV 有日期列）
    # TODO: 需要 C++ 端在 CSV 中输出日期列才能支持

    if args.label_source not in df.columns:
        emit({"type": "error", "message": f"标签来源列 '{args.label_source}' 不在数据中。可用列: {list(df.columns)}"})
        sys.exit(1)

    raw_label, future = compute_label(
        df[args.label_source].values,
        args.label_period,
        args.label_type,
        args.vol_k,
    )

    # 排除标签列和日期列
    exclude_cols = {args.label_source, "date"}
    feature_cols = [c for c in df.columns if c not in exclude_cols]
    if len(feature_cols) == 0:
        emit({"type": "error", "message": "没有可用的特征列"})
        sys.exit(1)

    # NaN 诊断：统计每列有效值数量
    nan_stats = {c: int(df[c].notna().sum()) for c in feature_cols}
    total_rows = len(df)
    all_nan_cols = [c for c, cnt in nan_stats.items() if cnt == 0]
    partial_nan_cols = [c for c, cnt in nan_stats.items() if cnt < total_rows]

    emit({"type": "info", "phase": "nan_stats",
          "total_rows": total_rows,
          "all_nan_cols": all_nan_cols,
          "partial_nan_cols_count": len(partial_nan_cols),
          "fully_valid_cols": total_rows - len(all_nan_cols)})

    # 丢弃全 NaN 列（该特征对所有样本都无效，无法使用）
    if all_nan_cols:
        feature_cols = [c for c in feature_cols if c not in set(all_nan_cols)]
        emit({"type": "info", "phase": "drop_all_nan",
              "dropped": all_nan_cols, "remaining_features": len(feature_cols)})

    if len(feature_cols) == 0:
        emit({"type": "error", "message": f"所有 {total_rows} 行特征数据均为 NaN，无法训练。"
              f"原始列: {list(nan_stats.keys())}"})
        sys.exit(1)

    valid_mask = raw_label.notna() & df[feature_cols].notna().all(axis=1)
    X = df.loc[valid_mask, feature_cols].values
    y = raw_label[valid_mask].values

    if len(X) == 0:
        # 诊断：报告每列剩余有效行数
        col_valid = {c: int(df[c].notna().sum()) for c in feature_cols}
        min_col = min(col_valid, key=col_valid.get)
        emit({"type": "error", "message": (
            f"标签过滤后无有效样本。标签有效: {int(raw_label.notna().sum())}/{total_rows}。"
            f"特征列最少有效值: {min_col}={col_valid[min_col]}/{total_rows}。"
            f"可能原因：特征间时间对齐不一致，或标签来源列与特征列日期范围不重叠。"
        )})
        sys.exit(1)

    split = int(len(X) * (1 - args.test_ratio))
    X_train, X_test = X[:split], X[split:]
    y_train, y_test = y[:split], y[split:]

    emit({"type": "progress", "phase": "split", "n_train": len(X_train), "n_test": len(X_test), "n_features": X.shape[1]})

    is_classification = args.label_type == "classification"

    # 自适应分类数：分类模式下自动检测实际标签数
    if is_classification:
        unique_labels = sorted(set(y[~np.isnan(y)]))
        n_classes = len(unique_labels)
        if n_classes <= 1:
            emit({"type": "error", "message": f"标签只有 {n_classes} 个类别，无法训练分类模型"})
            sys.exit(1)
        if n_classes == 2:
            args.objective = "binary:logistic"
            args.num_class = 1  # XGBoost binary 不需要 num_class
        else:
            args.objective = "multi:softprob"
            args.num_class = n_classes
        emit({"type": "info", "phase": "label_stats", "n_classes": n_classes,
              "classes": [int(c) for c in unique_labels], "objective": args.objective})

    default_params = {
        "learning_rate": 0.1,
        "max_depth": 6,
        "n_estimators": 200,
        "early_stopping_rounds": 20,
        "subsample": 0.8,
        "colsample_bytree": 0.8,
        "objective": args.objective,
        "verbosity": 0,
    }
    if is_classification:
        default_params["eval_metric"] = "mlogloss" if args.num_class > 1 else "logloss"
    else:
        default_params["eval_metric"] = "rmse"

    default_params.update(params)

    # 多分类时强制设置 num_class
    if is_classification and args.num_class > 1:
        default_params["num_class"] = args.num_class

    if "early_stopping_rounds" in default_params:
        del default_params["early_stopping_rounds"]
    if "n_estimators" in default_params:
        n_estimators = default_params.pop("n_estimators")
    else:
        n_estimators = 200

    esr = 20
    for k in ("early_stopping_rounds", "early_stopping"):
        if k in default_params:
            esr = int(default_params.pop(k))
            break

    dtrain = xgb.DMatrix(X_train, label=y_train, feature_names=feature_cols)
    dtest = xgb.DMatrix(X_test, label=y_test, feature_names=feature_cols)

    progress_state = {"it": 0}
    last_emit = [time.time()]

    class ProgressCallback(xgb.callback.TrainingCallback):
        def after_iteration(self, model, epoch, evals_log):
            now = time.time()
            if now - last_emit[0] < 0.2 and epoch != model.num_boosted_rounds() - 1:
                return False
            last_emit[0] = now
            train_loss = None
            eval_loss = None
            if evals_log and "train" in evals_log:
                for metric_vals in evals_log["train"].values():
                    train_loss = float(metric_vals[-1])
                    break
            if evals_log and "eval" in evals_log:
                for metric_vals in evals_log["eval"].values():
                    eval_loss = float(metric_vals[-1])
                    break
            emit({
                "type": "progress",
                "phase": "training",
                "iteration": epoch,
                "train_loss": train_loss,
                "eval_loss": eval_loss,
            })
            return False

    booster = xgb.train(
        default_params,
        dtrain,
        num_boost_round=n_estimators,
        evals=[(dtrain, "train"), (dtest, "eval")],
        early_stopping_rounds=esr if len(X_test) > 0 else None,
        verbose_eval=False,
        callbacks=[ProgressCallback()],
    )

    best_iteration = booster.best_iteration if hasattr(booster, "best_iteration") else n_estimators - 1

    learning_curve = []
    history = booster.evals_result() if hasattr(booster, "evals_result") else {}
    if history:
        def _first_metric(d):
            """取第一个可用指标的列表（优先 logloss/mlogloss/rmse）"""
            for key in ("logloss", "mlogloss", "rmse"):
                if key in d:
                    return d[key]
            return next(iter(d.values()), []) if d else []
        train_losses = _first_metric(history.get("train", {}))
        eval_losses = _first_metric(history.get("eval", {}))
        for i in range(min(len(train_losses), len(eval_losses))):
            learning_curve.append({
                "iteration": i,
                "train_loss": float(train_losses[i]),
                "eval_loss": float(eval_losses[i]),
            })

    importance = booster.get_score(importance_type="gain")
    feature_importance = []
    for feat in feature_cols:
        g = importance.get(feat, 0.0)
        feature_importance.append({"feature": feat, "gain": float(g), "weight": 0.0, "cover": 0.0})

    fi_w = booster.get_score(importance_type="weight")
    fi_c = booster.get_score(importance_type="cover")
    for item in feature_importance:
        item["weight"] = float(fi_w.get(item["feature"], 0))
        item["cover"] = float(fi_c.get(item["feature"], 0))

    predictions = []
    eval_metrics = {}
    if len(X_test) > 0:
        preds_proba = booster.predict(dtest)
        if is_classification:
            if args.objective == "binary:logistic":
                preds_class = (preds_proba > 0.5).astype(int)
            else:
                preds_class = np.argmax(preds_proba, axis=1)
            for i in range(len(y_test)):
                predictions.append({
                    "actual": int(y_test[i]),
                    "predicted": float(preds_proba[i]) if preds_proba.ndim == 1 else float(np.max(preds_proba[i])),
                    "pred_class": int(preds_class[i]),
                })
            try:
                from sklearn.metrics import accuracy_score, f1_score, precision_score, recall_score, roc_auc_score
                eval_metrics["accuracy"] = float(accuracy_score(y_test, preds_class))
                eval_metrics["f1"] = float(f1_score(y_test, preds_class, average="binary" if args.num_class == 2 else "weighted", zero_division=0))
                eval_metrics["precision"] = float(precision_score(y_test, preds_class, average="binary" if args.num_class == 2 else "weighted", zero_division=0))
                eval_metrics["recall"] = float(recall_score(y_test, preds_class, average="binary" if args.num_class == 2 else "weighted", zero_division=0))
                if args.objective == "binary:logistic":
                    try:
                        eval_metrics["auc"] = float(roc_auc_score(y_test, preds_proba))
                    except Exception:
                        pass
            except ImportError:
                pass
        else:
            for i in range(len(y_test)):
                predictions.append({
                    "actual": float(y_test[i]),
                    "predicted": float(preds_proba[i]),
                    "pred_class": 0,
                })
            try:
                from sklearn.metrics import mean_squared_error, mean_absolute_error, r2_score
                eval_metrics["rmse"] = float(np.sqrt(mean_squared_error(y_test, preds_proba)))
                eval_metrics["mae"] = float(mean_absolute_error(y_test, preds_proba))
                eval_metrics["r2"] = float(r2_score(y_test, preds_proba))
            except ImportError:
                pass

    os.makedirs(os.path.dirname(args.model_output) or ".", exist_ok=True)
    booster.save_model(args.model_output)

    # X_test 序列化为 JSON 列表传给 C++，供 SHAP 计算
    # 大量样本时这个 JSON 可能很大；如有性能问题可改用文件
    xtest_json = X_test.tolist() if len(X_test) > 0 else []

    emit({
        "type": "result",
        "model_path": args.model_output,
        "best_iteration": int(best_iteration),
        "n_train": len(X_train),
        "n_test": len(X_test),
        "n_features": len(feature_cols),
        "features": feature_cols,
        "learning_curve": learning_curve,
        "feature_importance": feature_importance,
        "eval_metrics": eval_metrics,
        "predictions": predictions,
        "X_test": xtest_json,
    })


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:
        emit({"type": "error", "message": f"未捕获异常: {e}", "traceback": traceback.format_exc()})
        traceback.print_exc()
        sys.exit(1)
