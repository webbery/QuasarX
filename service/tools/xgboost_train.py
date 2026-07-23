"""
XGBoost 离线训练脚本（QuasarX 后端调用）

输入参数：
  --data           特征数据 CSV 路径（C++ 端生成，列 = 节点输出变量名 + 时间序列）
  --label-source   标签来源变量名（如 "sh.600000.close"）
  --label-period   未来收益周期 N（默认 5）
  --label-type     classification / regression
  --threshold      分类阈值（默认 0.0）
  --objective      XGBoost 目标函数：binary:logistic / multi:softprob / reg:squarederror
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
import numpy as np
import pandas as pd

try:
    import xgboost as xgb
except ImportError:
    print(json.dumps({"type": "error", "message": "未安装 xgboost: pip install xgboost"}), flush=True)
    sys.exit(1)


def emit(obj):
    """输出一行 JSON 到 stdout"""
    print(json.dumps(obj, ensure_ascii=False, default=str), flush=True)


def compute_label(values, period, label_type, threshold):
    """计算标签：future_return = source[t+N]/source[t] - 1
    classification: label = (future_return > threshold).astype(int)
    regression:     label = future_return
    """
    s = pd.Series(values, dtype=float)
    future = s.shift(-period) / s - 1.0
    if label_type == "classification":
        label = (future > threshold).astype(int)
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
    parser.add_argument("--threshold", type=float, default=0.0, help="分类阈值")
    parser.add_argument("--objective", default="binary:logistic")
    parser.add_argument("--num-class", type=int, default=2)
    parser.add_argument("--model-output", required=True, help="模型保存路径")
    parser.add_argument("--params", default="{}", help="XGBoost 超参数 JSON")
    parser.add_argument("--test-ratio", type=float, default=0.2)
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

    if args.label_source not in df.columns:
        emit({"type": "error", "message": f"标签来源列 '{args.label_source}' 不在数据中。可用列: {list(df.columns)}"})
        sys.exit(1)

    raw_label, future = compute_label(
        df[args.label_source].values,
        args.label_period,
        args.label_type,
        args.threshold,
    )

    feature_cols = [c for c in df.columns if c != args.label_source]
    if len(feature_cols) == 0:
        emit({"type": "error", "message": "没有可用的特征列"})
        sys.exit(1)

    valid_mask = raw_label.notna() & df[feature_cols].notna().all(axis=1)
    X = df.loc[valid_mask, feature_cols].values
    y = raw_label[valid_mask].values

    if len(X) == 0:
        emit({"type": "error", "message": "数据全部为 NaN，请检查输入"})
        sys.exit(1)

    split = int(len(X) * (1 - args.test_ratio))
    X_train, X_test = X[:split], X[split:]
    y_train, y_test = y[:split], y[split:]

    emit({"type": "progress", "phase": "split", "n_train": len(X_train), "n_test": len(X_test), "n_features": X.shape[1]})

    is_classification = args.label_type == "classification"

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
        default_params["eval_metric"] = "logloss"
    else:
        default_params["eval_metric"] = "rmse"

    default_params.update(params)

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

    def progress_cb(env):
        now = time.time()
        if now - last_emit[0] < 0.2 and env.iteration != env.end_iteration - 1:
            return
        last_emit[0] = now
        train_loss = env.evaluation_result_list[0][1] if env.evaluation_result_list else None
        emit({
            "type": "progress",
            "phase": "training",
            "iteration": env.iteration,
            "train_loss": float(train_loss) if train_loss is not None else None,
        })

    booster = xgb.train(
        default_params,
        dtrain,
        num_boost_round=n_estimators,
        evals=[(dtrain, "train"), (dtest, "eval")],
        early_stopping_rounds=esr if len(X_test) > 0 else None,
        verbose_eval=False,
        callbacks=[progress_cb],
    )

    best_iteration = booster.best_iteration if hasattr(booster, "best_iteration") else n_estimators - 1

    learning_curve = []
    history = booster.evals_result() if hasattr(booster, "evals_result") else {}
    if history:
        train_losses = history.get("train", {}).get("logloss") or history.get("train", {}).get("rmse") or []
        eval_losses = history.get("eval", {}).get("logloss") or history.get("eval", {}).get("rmse") or []
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
    main()
