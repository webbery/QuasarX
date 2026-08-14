"""
XGBoost Optuna 超参数自动优化脚本（QuasarX 后端调用）

复用 xgboost_train.py 的标签生成与数据加载逻辑，核心替换为 Optuna TPE 搜索。

输入参数：
  --data             特征数据 CSV 路径（C++ 端生成）
  --label-source     标签来源变量名（vector 模式必填）
  --label-period     未来收益周期 N（默认 5）
  --label-type       classification / regression
  --label-shape      vector / matrix（默认 matrix）
  --vol-k            自适应阈值系数（默认 0.5）
  --objective        目标函数（分类时自动推导）
  --num-class        分类数（默认 3）
  --test-ratio       测试集比例（默认 0.2，时序切分）
  --param-domains    参数搜索域 JSON，格式:
                     {"learning_rate": {"min": 0.005, "max": 0.3, "log": true}, ...}
                     未出现在 JSON 中的参数保持默认值不参与搜索
  --n-trials         Optuna 试验次数（默认 100）
  --optimize-metric  优化目标: logloss / rmse（默认根据 label-type 自动选择）
  --start-date       训练数据开始日期
  --end-date         训练数据结束日期
  --frequency        数据频率

输出（stdout 每行 JSON）：
  {"type":"info",     "phase":"load_data", "rows":N, "cols":N}
  {"type":"info",     "phase":"split", "n_train":N, "n_test":N, "n_features":N}
  {"type":"trial",    "number":0, "value":0.612, "best":0.612, "params":{...}, "duration_ms":1200}
  {"type":"trial",    "number":1, "value":0.589, "best":0.589, "params":{...}, "duration_ms":980}
  ...
  {"type":"result",   "best_params":{...}, "best_value":0.523, "n_trials":100,
                      "trials":[...], "importance":[...], "optimization_duration_ms":45000,
                      "n_train":N, "n_test":N, "n_features":N, "features":[...]}

依赖：xgboost, optuna, pandas, numpy
"""

import argparse
import json
import os
import sys
import time
import traceback
import warnings
import numpy as np
import pandas as pd

sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)
warnings.filterwarnings("ignore")

try:
    import xgboost as xgb
except ImportError:
    print(json.dumps({"type": "error", "message": "未安装 xgboost: pip install xgboost"}), flush=True)
    sys.exit(1)

try:
    import optuna
    from optuna.samplers import TPESampler
except ImportError:
    print(json.dumps({"type": "error", "message": "未安装 optuna: pip install optuna"}), flush=True)
    sys.exit(1)

# 关闭 Optuna 自身的日志（我们用 JSON 行输出）
optuna.logging.set_verbosity(optuna.logging.WARNING)


def emit(obj):
    print(json.dumps(obj, ensure_ascii=False, default=str, separators=(",", ":")), flush=True)


def compute_label(s, period, label_type, vol_k):
    """计算标签，与 xgboost_train.py 完全一致"""
    s = pd.Series(s) if not isinstance(s, pd.Series) else s.astype(float)
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
        label = pd.Series(np.nan, index=s.index)
        label[future > threshold] = 0
        label[future < -threshold] = 2
        label[(future >= -threshold) & (future <= threshold)] = 1
    else:
        label = future
    return label, future


# ── 参数搜索域默认值 ──────────────────────────────────────────────

DEFAULT_DOMAINS = {
    "learning_rate":    {"min": 0.005, "max": 0.3,  "log": True},
    "max_depth":        {"min": 3,     "max": 10},
    "n_estimators":     {"min": 50,    "max": 500,  "step": 50},
    "subsample":        {"min": 0.5,   "max": 1.0},
    "colsample_bytree": {"min": 0.5,   "max": 1.0},
    "gamma":            {"min": 0.0,   "max": 5.0},
    "min_child_weight": {"min": 1,     "max": 20},
    "reg_alpha":        {"min": 0.0,   "max": 10.0},
    "reg_lambda":       {"min": 0.0,   "max": 10.0},
}

# 固定参数（不参与搜索）
FIXED_PARAMS = {
    "early_stopping_rounds": 50,
    "verbosity": 0,
}


def suggest_params(trial, domains):
    """根据搜索域定义从 Optuna trial 中采样参数"""
    params = {}
    for name, domain in domains.items():
        lo = domain.get("min", 0)
        hi = domain.get("max", 1)
        is_log = domain.get("log", False)
        step = domain.get("step")

        if name in ("max_depth", "n_estimators", "min_child_weight"):
            # 整数参数
            if step and step > 1:
                params[name] = trial.suggest_int(name, int(lo), int(hi), step=int(step))
            else:
                params[name] = trial.suggest_int(name, int(lo), int(hi))
        elif is_log:
            # 对数分布浮点参数
            params[name] = trial.suggest_float(name, lo, hi, log=True)
        else:
            params[name] = trial.suggest_float(name, lo, hi)
    return params


def load_and_prepare_data(args):
    """加载 CSV、计算标签、划分数据集——返回 X_train, X_test, y_train, y_test, feature_cols"""

    df = pd.read_csv(args.data)
    emit({"type": "info", "phase": "load_data", "rows": len(df), "cols": len(df.columns)})

    # 日期过滤
    if "date" in df.columns and (args.start_date or args.end_date):
        df["date"] = pd.to_datetime(df["date"])
        if args.start_date:
            df = df[df["date"] >= args.start_date]
        if args.end_date:
            df = df[df["date"] <= args.end_date]
        df = df.set_index("date").sort_index()
    elif "date" in df.columns:
        df["date"] = pd.to_datetime(df["date"])
        df = df.set_index("date").sort_index()

    # ── matrix 模式 ──
    if args.label_shape == "matrix":
        symbol_prefixes = set()
        for col in df.columns:
            if col == "date":
                continue
            parts = col.split(".")
            if len(parts) >= 3:
                prefix = f"{parts[0]}.{parts[1]}."
                symbol_prefixes.add(prefix)
        symbols = sorted(symbol_prefixes)

        if not symbols:
            emit({"type": "error", "message": "matrix 模式无法从列名解析标的前缀"})
            sys.exit(1)

        def strip_prefix(col, prefix):
            return col[len(prefix):] if col.startswith(prefix) else None

        feature_names = []
        for col in df.columns:
            if col == "date":
                continue
            feat = strip_prefix(col, symbols[0])
            if feat is not None:
                feature_names.append(feat)

        X_parts, y_parts = [], []
        for sym in symbols:
            sym_cols = [sym + f for f in feature_names if (sym + f) in df.columns]
            if not sym_cols:
                continue

            sym_df = df[sym_cols].rename(columns=lambda c, p=sym: c[len(p):])

            close_col = None
            for candidate in ["quote.close", "input.close", "close"]:
                if candidate in sym_df.columns:
                    close_col = candidate
                    break
            if close_col is None:
                raw_close = sym + "close"
                if raw_close in df.columns:
                    sym_df_for_label = df[raw_close]
                else:
                    continue
            else:
                sym_df_for_label = sym_df[close_col]
                sym_df = sym_df.drop(columns=[close_col], errors="ignore")
                feature_names_no_close = [f for f in feature_names if f != close_col]
                sym_cols = [sym + f for f in feature_names_no_close if (sym + f) in df.columns]
                sym_df = df[sym_cols].rename(columns=lambda c, p=sym: c[len(p):])

            sym_label, _ = compute_label(sym_df_for_label, args.label_period, args.label_type, args.vol_k)
            valid = sym_label.notna() & sym_df.notna().all(axis=1)
            X_parts.append(sym_df.loc[valid].values)
            y_parts.append(sym_label[valid].values)

        if not X_parts:
            emit({"type": "error", "message": "matrix 模式：所有标的均无有效数据"})
            sys.exit(1)

        X = np.vstack(X_parts)
        y = np.concatenate(y_parts)
        feature_cols = [f for f in feature_names if f != "quote.close" and f != "input.close" and f != "close"]
        # 确保 feature_cols 与实际列数对齐
        if X.shape[1] != len(feature_cols):
            feature_cols = [f"f{i}" for i in range(X.shape[1])]

        emit({"type": "info", "phase": "matrix_reshape",
              "symbols": len(symbols), "total_samples": len(X),
              "n_features": len(feature_cols)})
    else:
        # ── vector 模式 ──
        if not args.label_source:
            emit({"type": "error", "message": "vector 模式需要指定 --label-source"})
            sys.exit(1)
        if args.label_source not in df.columns:
            emit({"type": "error", "message": f"标签来源列 '{args.label_source}' 不在数据中"})
            sys.exit(1)

        raw_label, _ = compute_label(df[args.label_source], args.label_period, args.label_type, args.vol_k)
        exclude_cols = {args.label_source}
        feature_cols = [c for c in df.columns if c not in exclude_cols]

        # NaN 处理
        nan_stats = {c: int(df[c].notna().sum()) for c in feature_cols}
        all_nan_cols = [c for c, cnt in nan_stats.items() if cnt == 0]
        if all_nan_cols:
            feature_cols = [c for c in feature_cols if c not in set(all_nan_cols)]

        if not feature_cols:
            emit({"type": "error", "message": "所有特征列均为 NaN"})
            sys.exit(1)

        valid_mask = raw_label.notna() & df[feature_cols].notna().all(axis=1)
        X = df.loc[valid_mask, feature_cols].values
        y = raw_label[valid_mask].values

    # 时序切分
    split = int(len(X) * (1 - args.test_ratio))
    X_train, X_test = X[:split], X[split:]
    y_train, y_test = y[:split], y[split:]

    emit({"type": "info", "phase": "split",
          "n_train": len(X_train), "n_test": len(X_test), "n_features": X.shape[1]})

    return X_train, X_test, y_train, y_test, feature_cols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True)
    parser.add_argument("--label-source", default="")
    parser.add_argument("--label-period", type=int, default=5)
    parser.add_argument("--label-type", choices=["classification", "regression"], default="classification")
    parser.add_argument("--label-shape", choices=["vector", "matrix"], default="matrix")
    parser.add_argument("--vol-k", type=float, default=0.5)
    parser.add_argument("--objective", default="multi:softprob")
    parser.add_argument("--num-class", type=int, default=3)
    parser.add_argument("--test-ratio", type=float, default=0.2)
    parser.add_argument("--param-domains", default="{}", help="参数搜索域 JSON")
    parser.add_argument("--n-trials", type=int, default=100)
    parser.add_argument("--optimize-metric", default="", help="优化目标: logloss / rmse（默认自动）")
    parser.add_argument("--start-date", default="")
    parser.add_argument("--end-date", default="")
    parser.add_argument("--frequency", default="1d")
    args = parser.parse_args()

    try:
        domains = json.loads(args.param_domains) if args.param_domains else {}
    except json.JSONDecodeError as e:
        emit({"type": "error", "message": f"param-domains JSON 解析失败: {e}"})
        sys.exit(1)

    # 合并默认域（用户未指定的参数用默认域，用户指定了空 {} 表示用全部默认）
    if not domains:
        domains = dict(DEFAULT_DOMAINS)
    else:
        merged = dict(DEFAULT_DOMAINS)
        merged.update(domains)
        domains = merged

    # ── 加载数据 ──
    X_train, X_test, y_train, y_test, feature_cols = load_and_prepare_data(args)

    if len(X_train) == 0:
        emit({"type": "error", "message": "训练集为空"})
        sys.exit(1)

    is_classification = args.label_type == "classification"

    # 自适应分类数
    if is_classification:
        unique_labels = sorted(set(y_train[~np.isnan(y_train)]))
        n_classes = len(unique_labels)
        if n_classes <= 1:
            emit({"type": "error", "message": f"训练集标签只有 {n_classes} 个类别"})
            sys.exit(1)
        if n_classes == 2:
            args.objective = "binary:logistic"
            args.num_class = 1
        else:
            args.objective = "multi:softprob"
            args.num_class = n_classes
        emit({"type": "info", "phase": "label_stats",
              "n_classes": n_classes, "classes": [int(c) for c in unique_labels]})

    # 优化目标
    if args.optimize_metric:
        opt_metric = args.optimize_metric
    elif is_classification:
        opt_metric = "logloss"
    else:
        opt_metric = "rmse"

    eval_metric = opt_metric
    # XGBoost 多分类用 mlogloss
    xgb_eval_metric = eval_metric
    if is_classification and args.num_class > 1 and eval_metric == "logloss":
        xgb_eval_metric = "mlogloss"

    direction = "minimize"

    dtrain = xgb.DMatrix(X_train, label=y_train, feature_names=feature_cols)
    dtest = xgb.DMatrix(X_test, label=y_test, feature_names=feature_cols)

    emit({"type": "info", "phase": "optimize_start",
          "n_trials": args.n_trials, "metric": opt_metric, "direction": direction,
          "search_params": list(domains.keys())})

    # ── Optuna 搜索 ──
    total_start = time.time()
    trials_record = []
    best_value_so_far = float("inf")

    def objective(trial):
        nonlocal best_value_so_far

        trial_start = time.time()
        params = suggest_params(trial, domains)

        # 构建 XGBoost 参数
        xgb_params = {
            "objective": args.objective,
            "eval_metric": xgb_eval_metric,
            "verbosity": 0,
        }
        if is_classification and args.num_class > 1:
            xgb_params["num_class"] = args.num_class

        # 搜索参数覆盖
        for k, v in params.items():
            xgb_params[k] = v

        # 固定参数
        for k, v in FIXED_PARAMS.items():
            xgb_params[k] = v

        n_estimators = params.get("n_estimators", 200)
        esr = FIXED_PARAMS["early_stopping_rounds"]

        booster = xgb.train(
            xgb_params,
            dtrain,
            num_boost_round=n_estimators,
            evals=[(dtrain, "train"), (dtest, "eval")],
            early_stopping_rounds=esr if len(X_test) > 0 else None,
            verbose_eval=False,
        )

        # 获取验证集指标
        if len(X_test) > 0:
            preds = booster.predict(dtest)
            if is_classification:
                if args.objective == "binary:logistic":
                    preds_class = (preds > 0.5).astype(int)
                else:
                    preds_class = np.argmax(preds, axis=1)

                # 计算目标指标
                if opt_metric == "logloss":
                    from sklearn.metrics import log_loss
                    value = float(log_loss(y_test, preds, labels=list(range(args.num_class if args.num_class > 1 else 2))))
                elif opt_metric == "accuracy":
                    from sklearn.metrics import accuracy_score
                    value = -float(accuracy_score(y_test, preds_class))  # 取负因为 minimize
                elif opt_metric == "f1":
                    from sklearn.metrics import f1_score
                    value = -float(f1_score(y_test, preds_class, average="weighted", zero_division=0))
                else:
                    from sklearn.metrics import log_loss
                    value = float(log_loss(y_test, preds, labels=list(range(args.num_class if args.num_class > 1 else 2))))
            else:
                if opt_metric == "rmse":
                    value = float(np.sqrt(np.mean((y_test - preds) ** 2)))
                elif opt_metric == "mae":
                    value = float(np.mean(np.abs(y_test - preds)))
                else:
                    value = float(np.sqrt(np.mean((y_test - preds) ** 2)))
        else:
            # 无测试集时用 eval 结果
            ev = booster.eval(dtest) if len(X_test) > 0 else booster.eval(dtrain)
            value = float(ev.split(":")[-1]) if ":" in str(ev) else 0.0

        duration_ms = int((time.time() - trial_start) * 1000)

        if value < best_value_so_far:
            best_value_so_far = value

        trial_info = {
            "number": trial.number,
            "value": round(value, 6),
            "best": round(best_value_so_far, 6),
            "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in params.items()},
            "duration_ms": duration_ms,
        }
        trials_record.append(trial_info)

        emit({"type": "trial", **trial_info})
        return value

    study = optuna.create_study(direction=direction, sampler=TPESampler(seed=42))
    study.optimize(objective, n_trials=args.n_trials, show_progress_bar=False)

    total_duration_ms = int((time.time() - total_start) * 1000)

    # ── 参数重要性 ──
    importance = []
    try:
        importances = optuna.importance.get_param_importances(study)
        importance = [{"name": k, "importance": round(v, 4)} for k, v in
                       sorted(importances.items(), key=lambda x: -x[1])]
    except Exception:
        # optuna.importance 需要 sklearn，不可用时降级
        pass

    # ── 输出最终结果 ──
    best = study.best_trial
    best_params = {k: (round(v, 6) if isinstance(v, float) else v)
                   for k, v in best.params.items()}

    emit({
        "type": "result",
        "best_params": best_params,
        "best_value": round(best.value, 6),
        "best_trial_number": best.number,
        "n_trials": args.n_trials,
        "n_completed": len(study.trials),
        "trials": trials_record,
        "importance": importance,
        "optimization_duration_ms": total_duration_ms,
        "n_train": len(X_train),
        "n_test": len(X_test),
        "n_features": len(feature_cols),
        "features": feature_cols,
        "metric": opt_metric,
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
