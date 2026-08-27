"""
XGBoost Optuna 超参数自动优化 + 快速回测评估

每个 Optuna trial：
  1. 用 Python xgboost 训练模型 → 保存到 experiments/
  2. 调 POST {base_url}/v0/backtest（快速模式：feature_cache + model_path）
  3. 解析 summary.{metric} 作为 Optuna objective

输入参数：
  --base-url              QuasarX 服务地址（默认 https://localhost:19107）
  --auth-token            JWT token
  --feature-cache         特征缓存 CSV 路径（/v0/ml collect 产出）
  --fast-backtest-strategy 回测策略 JSON 路径（XGBoost 节点 → CacheFeatureNode 链路）
  --label-source          标签来源列名（训练数据 CSV 中）
  --label-period          未来收益周期 N
  --label-type            classification / regression
  --label-shape           vector / matrix
  --vol-k                 自适应阈值系数
  --objective             XGBoost objective
  --num-class             分类数
  --test-ratio            测试集比例
  --param-domains         参数搜索域 JSON
  --n-trials              Optuna 试验次数（默认 50）
  --optimize-metric       优化目标（默认 sharpe）:
                            sharpe / total_return / annual_return
                            / max_drawdown / win_rate / calmar_ratio
  --start-date / --end-date / --frequency
  --output-dir            实验产物目录（默认 experiments/optimize_fast_{ts}/）

输出（stdout 每行 JSON）：
  {"type":"info",     "phase":"load_data", ...}
  {"type":"trial",    "number":0, "value":1.85, "params":{...}, "duration_ms":1200}
  ...
  {"type":"result",   "best_params":{...}, "best_value":2.10, "n_trials":N, ...}
"""

import argparse
import json
import os
import sys
import time
import traceback
import shutil
import warnings
from pathlib import Path

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

try:
    import requests
except ImportError:
    print(json.dumps({"type": "error", "message": "未安装 requests: pip install requests"}), flush=True)
    sys.exit(1)

optuna.logging.set_verbosity(optuna.logging.WARNING)
urllib3_disable = __import__("urllib3")
urllib3_disable.disable_warnings(urllib3_disable.exceptions.InsecureRequestWarning)


def emit(obj):
    print(json.dumps(obj, ensure_ascii=False, default=str, separators=(",", ":")), flush=True)


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

FIXED_PARAMS = {"verbosity": 0}

# 回测指标 → summary 字段映射
METRIC_TO_SUMMARY = {
    "sharpe":        "sharp",
    "total_return":  "total_return",
    "annual_return": "annual_return",
    "max_drawdown":  "max_drawdown",
    "win_rate":      "win_rate",
    "calmar_ratio":  "calmar_ratio",
}


def suggest_params(trial, domains):
    """根据搜索域从 Optuna trial 采样参数"""
    params = {}
    for name, domain in domains.items():
        lo, hi = domain.get("min", 0), domain.get("max", 1)
        is_log, step = domain.get("log", False), domain.get("step")

        if name in ("max_depth", "n_estimators", "min_child_weight"):
            params[name] = trial.suggest_int(name, int(lo), int(hi), step=int(step)) if step else trial.suggest_int(name, int(lo), int(hi))
        elif is_log:
            params[name] = trial.suggest_float(name, lo, hi, log=True)
        else:
            params[name] = trial.suggest_float(name, lo, hi)
    return params


def load_and_prepare_data(args):
    """加载训练数据 CSV，按 label-source/period/type 计算标签，按 test-ratio 时序切分"""
    df = pd.read_csv(args.data)
    emit({"type": "info", "phase": "load_data", "rows": len(df), "cols": len(df.columns)})

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

    if args.label_shape == "matrix":
        # 多标的 matrix 模式：从列名前缀解析 symbols
        symbol_prefixes = sorted({f"{c.split('.')[0]}." for c in df.columns if c != "date" and len(c.split('.')) >= 3})
        if not symbol_prefixes:
            emit({"type": "error", "message": "matrix 模式无法从列名解析标的前缀"})
            sys.exit(1)
        # 简化：取第一组前缀作为 feature names，其他按同结构展开
        first_prefix = symbol_prefixes[0]
        feature_names = [c[len(first_prefix):] for c in df.columns if c.startswith(first_prefix)]
        close_col = next((c for c in ["quote.close", "input.close", "close"] if c in feature_names), None)

        X_parts, y_parts = [], []
        for prefix in symbol_prefixes:
            sym_cols = [prefix + f for f in feature_names if (prefix + f) in df.columns]
            if close_col:
                sym_df_for_label = df[prefix + close_col]
                sym_cols = [c for c in sym_cols if c != prefix + close_col]
            else:
                continue
            sym_df = df[sym_cols].rename(columns=lambda c, p=prefix: c[len(p):])
            sym_label, _ = compute_label(sym_df_for_label, args.label_period, args.label_type, args.vol_k)
            valid = sym_label.notna() & sym_df.notna().all(axis=1)
            X_parts.append(sym_df.loc[valid].values)
            y_parts.append(sym_label[valid].values)
        X = np.vstack(X_parts)
        y = np.concatenate(y_parts)
        feature_cols = [f for f in feature_names if f != close_col]
    else:
        # vector 模式
        if args.label_source not in df.columns:
            emit({"type": "error", "message": f"标签来源列 '{args.label_source}' 不在数据中"})
            sys.exit(1)
        raw_label, _ = compute_label(df[args.label_source], args.label_period, args.label_type, args.vol_k)
        feature_cols = [c for c in df.columns if c not in {args.label_source, "date"}]
        all_nan = [c for c in feature_cols if df[c].notna().sum() == 0]
        feature_cols = [c for c in feature_cols if c not in set(all_nan)]
        valid = raw_label.notna() & df[feature_cols].notna().all(axis=1)
        X = df.loc[valid, feature_cols].values
        y = raw_label[valid].values

    split = int(len(X) * (1 - args.test_ratio))
    emit({"type": "info", "phase": "split",
          "n_train": split, "n_test": len(X) - split, "n_features": X.shape[1]})
    return X[:split], y[:split], X[split:], y[split:], feature_cols


def compute_label(s, period, label_type, vol_k):
    """计算标签：未来 period 周期收益率，按 vol_k 自适应阈值分类"""
    s = pd.Series(s) if not isinstance(s, pd.Series) else s.astype(float)
    future = s.shift(-period) / s - 1.0
    if label_type == "classification":
        log_ret = np.log(s / s.shift(1)).replace([np.inf, -np.inf], np.nan).dropna()
        sigma = log_ret.std() if len(log_ret) >= 20 else 0.0
        threshold = float(np.clip(vol_k * sigma * np.sqrt(period), 0.005, 0.10)) if sigma > 1e-12 else 0.015
        label = pd.Series(np.nan, index=s.index)
        label[future > threshold] = 0
        label[future < -threshold] = 2
        label[(future >= -threshold) & (future <= threshold)] = 1
    else:
        label = future
    return label, future


def train_xgb(X_train, y_train, X_test, y_test, params, args, feature_cols):
    """训练 XGBoost 模型，返回 booster + 训练集得分（早停触发时使用）"""
    xgb_params = {
        "objective": args.objective,
        "verbosity": 0,
        **params,
        **FIXED_PARAMS,
    }
    if args.label_type == "classification" and args.num_class > 1:
        xgb_params["num_class"] = args.num_class

    dtrain = xgb.DMatrix(X_train, label=y_train, feature_names=feature_cols)
    dtest = xgb.DMatrix(X_test, label=y_test, feature_names=feature_cols) if len(X_test) > 0 else None

    evals = [(dtrain, "train")] + ([(dtest, "eval")] if dtest is not None else [])
    booster = xgb.train(
        xgb_params, dtrain,
        num_boost_round=params.get("n_estimators", 200),
        evals=evals,
        verbose_eval=False,
    )
    return booster


def save_model_and_get_path(booster, feature_cols, args, trial_number):
    """保存 booster 到 experiments/optimize_fast_{ts}/trial_{n}.json，返回路径"""
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    model_path = out_dir / f"trial_{trial_number}.json"
    booster.save_model(str(model_path))
    # meta.json：XGBoostNode 需要 features 字段才能正确恢复推理顺序
    meta = {
        "strategy_id": f"optuna_fast_{args.optimize_metric}",
        "source": "experiment",
        "features": list(feature_cols),
        "objective": args.objective,
        "num_class": args.num_class if args.label_type == "classification" else 0,
    }
    meta_path = out_dir / f"trial_{trial_number}.meta.json"
    meta_path.write_text(json.dumps(meta, indent=2))
    return str(model_path)


def call_fast_backtest(args, trial_model_path, trial_meta_path):
    """调 POST /v0/backtest（快速模式），返回 summary dict"""
    strategy = json.loads(Path(args.fast_backtest_strategy).read_text())

    # 改写 XGBoost 节点的 modelFile 为 trial 模型路径（绝对路径 → 逻辑路径）
    logical_path = trial_model_path
    idx = trial_model_path.find("models/")
    if idx >= 0:
        logical_path = trial_model_path[idx + len("models/"):]

    for node in strategy.get("nodes", []):
        if node.get("data", {}).get("nodeType") == "xgboost":
            node["data"]["params"]["modelFile"]["value"] = logical_path
            break

    url = args.base_url.rstrip("/") + "/v0/backtest"
    body = {
        "script": json.dumps(strategy),
        "validate": False,
        "feature_cache": args.feature_cache,
        "model_path": logical_path,
    }
    headers = {"Authorization": args.auth_token} if args.auth_token else {}
    try:
        resp = requests.post(url, json=body, headers=headers, verify=False, timeout=120)
    except requests.exceptions.Timeout:
        return None, "timeout"
    except requests.exceptions.RequestException as e:
        return None, str(e)

    if resp.status_code != 200:
        return None, f"HTTP {resp.status_code}: {resp.text[:200]}"
    data = resp.json()
    return data.get("summary", {}), None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True, help="训练数据 CSV（Python 训练用）")
    parser.add_argument("--label-source", default="")
    parser.add_argument("--label-period", type=int, default=5)
    parser.add_argument("--label-type", choices=["classification", "regression"], default="classification")
    parser.add_argument("--label-shape", choices=["vector", "matrix"], default="matrix")
    parser.add_argument("--vol-k", type=float, default=0.5)
    parser.add_argument("--objective", default="multi:softprob")
    parser.add_argument("--num-class", type=int, default=3)
    parser.add_argument("--test-ratio", type=float, default=0.2)
    parser.add_argument("--param-domains", default="{}")
    parser.add_argument("--n-trials", type=int, default=50)
    parser.add_argument("--optimize-metric", default="sharpe",
                        choices=list(METRIC_TO_SUMMARY.keys()))
    parser.add_argument("--start-date", default="")
    parser.add_argument("--end-date", default="")
    parser.add_argument("--frequency", default="1d")

    # 快速回测参数
    parser.add_argument("--base-url", default="https://localhost:19107")
    parser.add_argument("--auth-token", default="")
    parser.add_argument("--feature-cache", required=True,
                        help="/v0/ml collect 产出的特征 CSV 路径")
    parser.add_argument("--fast-backtest-strategy", required=True,
                        help="回测策略 JSON 路径（XGBoost 节点 → CacheFeatureNode 链路）")
    parser.add_argument("--output-dir", default="")

    args = parser.parse_args()

    # 输出目录：experiments/optimize_fast_{ts}/
    if not args.output_dir:
        from datetime import datetime
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output_dir = f"experiments/optimize_fast_{ts}"

    try:
        domains = json.loads(args.param_domains) if args.param_domains else {}
    except json.JSONDecodeError as e:
        emit({"type": "error", "message": f"param-domains JSON 解析失败: {e}"})
        sys.exit(1)

    domains = dict(DEFAULT_DOMAINS) if not domains else {**DEFAULT_DOMAINS, **domains}

    metric = args.optimize_metric
    summary_field = METRIC_TO_SUMMARY[metric]

    # ── 加载训练数据 ──
    X_train, y_train, X_test, y_test, feature_cols = load_and_prepare_data(args)
    if len(X_train) == 0:
        emit({"type": "error", "message": "训练集为空"})
        sys.exit(1)

    is_classification = args.label_type == "classification"
    if is_classification:
        n_classes = len(set(y_train[~np.isnan(y_train)]))
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
              "n_classes": n_classes, "objective": args.objective, "num_class": args.num_class})

    emit({"type": "info", "phase": "optimize_start",
          "n_trials": args.n_trials, "metric": metric, "summary_field": summary_field,
          "base_url": args.base_url, "output_dir": args.output_dir,
          "search_params": list(domains.keys())})

    # ── Optuna 搜索 ──
    total_start = time.time()
    trials_record = []
    best_value_so_far = -float("inf")
    best_model_path = None

    def objective(trial):
        nonlocal best_value_so_far, best_model_path

        trial_start = time.time()
        params = suggest_params(trial, domains)

        # 训练
        booster = train_xgb(X_train, y_train, X_test, y_test, params, args, feature_cols)

        # 保存模型
        trial_model_path = save_model_and_get_path(booster, feature_cols, args, trial.number)

        # 快速回测
        summary, err = call_fast_backtest(args, trial_model_path, str(Path(trial_model_path).with_suffix(".meta.json")))
        duration_ms = int((time.time() - trial_start) * 1000)

        if err or summary is None:
            trial_info = {
                "number": trial.number,
                "value": None,
                "best": round(best_value_so_far, 6) if best_value_so_far > -float("inf") else None,
                "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in params.items()},
                "duration_ms": duration_ms,
                "status": "failed",
                "error": err,
            }
            trials_record.append(trial_info)
            emit({"type": "trial", **trial_info})
            # 返回 -inf 让 Optuna 标记为最差但不报错
            return -1e9

        value = float(summary.get(summary_field, 0.0))
        if value > best_value_so_far:
            best_value_so_far = value
            best_model_path = trial_model_path

        trial_info = {
            "number": trial.number,
            "value": round(value, 6),
            "best": round(best_value_so_far, 6),
            "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in params.items()},
            "duration_ms": duration_ms,
            "status": "ok",
            "summary": summary,
        }
        trials_record.append(trial_info)
        emit({"type": "trial", **trial_info})
        return value

    study = optuna.create_study(direction="maximize", sampler=TPESampler(seed=42))
    study.optimize(objective, n_trials=args.n_trials, show_progress_bar=False)

    total_duration_ms = int((time.time() - total_start) * 1000)

    # 参数重要性
    importance = []
    try:
        importances = optuna.importance.get_param_importances(study)
        importance = [{"name": k, "importance": round(v, 4)}
                      for k, v in sorted(importances.items(), key=lambda x: -x[1])]
    except Exception:
        pass

    best = study.best_trial
    best_params = {k: (round(v, 6) if isinstance(v, float) else v)
                   for k, v in best.params.items()}

    # 拷贝最优模型到 best.json
    best_dest = Path(args.output_dir) / "best.json"
    if best_model_path and Path(best_model_path).exists():
        shutil.copy(best_model_path, best_dest)

    emit({
        "type": "result",
        "best_params": best_params,
        "best_value": round(best.value, 6),
        "best_trial_number": best.number,
        "best_model": str(best_dest) if best_model_path else None,
        "n_trials": args.n_trials,
        "n_completed": len(study.trials),
        "trials": trials_record,
        "importance": importance,
        "optimization_duration_ms": total_duration_ms,
        "metric": metric,
        "output_dir": args.output_dir,
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