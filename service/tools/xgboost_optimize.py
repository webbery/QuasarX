"""
XGBoost Optuna 超参数自动优化 + 快速回测评估

每个 Optuna trial：
  1. 用 Python xgboost 训练模型（只用训练段）→ 保存到 experiments/
  2. 调 POST {base_url}/v0/backtest（快速模式：feature_cache + model_path）
  3. 在**选择窗口**（DEV 段）上重算指标，作为 Optuna objective
  4. 对各 trial 的 DEV 显著性做 BH-FDR 校正
  5. 搜索结束后，对选中的 trial 在**留出窗口**（HOLDOUT 段）上评一次

为什么要留出窗口
----------------
此前每个 trial 的 objective 是"整段历史回测的 summary"。回测覆盖训练数据
之外的时间，于是**测试期参与了超参选择** —— 这是最典型的选择偏差：挑出来
的"最优超参"里有一部分优势只是它对测试期的记忆。

现在改成三段式（按唯一日期切，单位是交易日）：

    TRAIN  = [0, T-gap)              训练模型
    DEV    = [T, V-gap)              选超参（objective 只看这段）
    HOLDOUT= [V, N)                  最终只评一次，不参与任何选择

留出段是唯一无偏的读数。`selection_bias = best_dev_value - holdout_value`
直接给出"选超参这件事本身乐观了多少"。

多重检验校正
------------
n_trials 个 trial 是 n_trials 次"这个配置有正收益"的检验。不校正的话，
最优 trial 的指标里有相当一部分来自运气。这里对每个 trial 的 DEV 窗口
日均收益做 Newey-West t 检验，再对 p 值做 BH-FDR（口径与特征门禁
`feature_screen.py` 一致），并可用 `--select-by q` 只在通过校正的 trial 里选。

注意：TPE 会把新样本采到当前最优点附近，各 trial 之间并不独立，BH 在这里
偏保守；它给出的是"在 n 次搜索下，有多少 trial 的优势站得住"的粗略判断。

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
  --test-ratio            留出段比例
  --val-ratio             选择段（DEV）比例
  --gap-bars              段间隔离长度（唯一交易日数）；0 = 取 label-period
  --select-window         val=按 DEV 段选（默认）；full=整段历史（旧行为，有泄漏）
  --fdr-alpha             BH-FDR 显著性水平（默认 0.10）
  --select-by             dev_metric=按 DEV 指标选（默认）；q=只在 FDR 通过的 trial 里选
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
  {"type":"result",   "best_params":{...}, "best_value":2.10, "n_trials":N,
                      "selection":{...}}
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

# 公共实现：标签计算、标签参数、meta 抽取
sys.path.insert(0, str(Path(__file__).resolve().parent))
from xgb_util import add_label_args, compute_label, label_meta
from cv_util import sort_samples_by_date, split_indices_by_date
from backtest_metrics import compute_metrics, slice_window
from ml_stats import bh_qvalues, newey_west_t

sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)
warnings.filterwarnings("ignore")


def _emit_error(msg):
    """输出协议错误行。

    格式必须与 C++ 端 MLHandler 的解析约定一致：紧凑分隔符（无空格）且
    不转义非 ASCII。json.dumps 默认会输出 {"type": "error", ...}（冒号后有空格），
    服务端的子串匹配会漏判，真实错误就此丢失，最终只报出
    "optimize script 未输出 result" 这种无信息量的兜底文案。
    """
    print(json.dumps({"type": "error", "message": msg}, ensure_ascii=False,
                     separators=(",", ":")), flush=True)


try:
    import xgboost as xgb
except ImportError:
    _emit_error("未安装 xgboost: pip install xgboost")
    sys.exit(1)

try:
    import optuna
    from optuna.samplers import TPESampler
except ImportError:
    _emit_error("未安装 optuna: pip install optuna")
    sys.exit(1)

try:
    import requests
except ImportError:
    _emit_error("未安装 requests: pip install requests")
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
        # 列名格式: {exchange}.{code}.{feature}，如 sh.600111.breakout
        # 标的前缀 = 前两段: sh.600111.
        symbol_prefixes = sorted({".".join(c.split(".")[:2]) + "." for c in df.columns if c != "date" and len(c.split(".")) >= 3})
        if not symbol_prefixes:
            emit({"type": "error", "message": "matrix 模式无法从列名解析标的前缀"})
            sys.exit(1)
        # 简化：取第一组前缀作为 feature names，其他按同结构展开
        first_prefix = symbol_prefixes[0]
        feature_names = [c[len(first_prefix):] for c in df.columns if c.startswith(first_prefix)]
        close_col = next((c for c in ["quote.close", "input.close", "close"] if c in feature_names), None)

        X_parts, y_parts, date_parts = [], [], []
        sample_dates = None
        for prefix in symbol_prefixes:
            sym_cols = [prefix + f for f in feature_names if (prefix + f) in df.columns]
            if close_col:
                sym_df_for_label = df[prefix + close_col]
                sym_cols = [c for c in sym_cols if c != prefix + close_col]
            else:
                continue
            sym_df = df[sym_cols].rename(columns=lambda c, p=prefix: c[len(p):])
            sym_label, _ = compute_label(sym_df_for_label, args.label_period, args.label_type,
                                          args.vol_k, args.label_sigma_mode)
            valid = sym_label.notna() & sym_df.notna().all(axis=1)
            X_parts.append(sym_df.loc[valid].values)
            y_parts.append(sym_label[valid].values)
            date_parts.append(sym_df.index[valid])
        X = np.vstack(X_parts)
        y = np.concatenate(y_parts)
        if len(X) == 0:
            emit({"type": "error", "message": "matrix 模式：标签/特征过滤后无有效样本"})
            sys.exit(1)
        if isinstance(df.index, pd.DatetimeIndex):
            sample_dates = pd.DatetimeIndex(
                np.concatenate([np.asarray(d) for d in date_parts]))
        feature_cols = [f for f in feature_names if f != close_col]
    else:
        # vector 模式
        if args.label_source not in df.columns:
            emit({"type": "error", "message": f"标签来源列 '{args.label_source}' 不在数据中"})
            sys.exit(1)
        raw_label, _ = compute_label(df[args.label_source], args.label_period, args.label_type,
                                      args.vol_k, args.label_sigma_mode)
        feature_cols = [c for c in df.columns if c not in {args.label_source, "date"}]
        all_nan = [c for c in feature_cols if df[c].notna().sum() == 0]
        feature_cols = [c for c in feature_cols if c not in set(all_nan)]
        valid = raw_label.notna() & df[feature_cols].notna().all(axis=1)
        X = df.loc[valid, feature_cols].values
        y = raw_label[valid].values
        sample_dates = df.index[valid.to_numpy()] if isinstance(df.index, pd.DatetimeIndex) else None

    n = len(X)
    use_val = args.val_ratio > 0 and n >= 1000

    # 段间隔离长度：默认取 label_period（标签是 label_period 根 bar 的前瞻
    # 收益，训练段的标签窗口不能伸进下一段）。
    gap_dates = args.gap_bars if args.gap_bars and args.gap_bars > 0 else int(args.label_period)

    # 与 xgboost_train.py 同一口径：按唯一日期切分，避免多标的按行切分时
    # 把最后几个标的当成测试集。
    X, y, sample_dates, has_sample_dates = sort_samples_by_date(X, y, sample_dates)

    split = None
    if has_sample_dates:
        split = split_indices_by_date(sample_dates, use_val, args.val_ratio,
                                      args.test_ratio, gap_dates)
        if split is None and use_val:
            emit({"type": "warning",
                  "code": "no_dev_segment",
                  "message": "日期数不足以切出 train/val/test 三段，退化为 train/test 两段，"
                             "此时没有 DEV 选择段"})
            use_val = False
            split = split_indices_by_date(sample_dates, False, args.val_ratio,
                                          args.test_ratio, gap_dates)
        if split is None:
            emit({"type": "warning",
                  "code": "row_split_fallback",
                  "message": "日期切分失败，退化为按行切分（多标的场景可能出现同日跨集）"})

    segments = {"gap_dates": gap_dates, "gap_shrunk": False,
                "train_start": None, "train_end": None,
                "dev_start": None, "dev_end": None,
                "holdout_start": None, "holdout_end": None}

    if split is not None:
        train_end = split["train_end"]
        val_start = split["val_start"]
        val_end = split["val_end"]
        test_start = split["test_start"]
        split_mode = "date"
        segments["gap_shrunk"] = bool(split.get("gap_shrunk", False))
        segments["gap_dates"] = int(split.get("gap_dates", gap_dates))

        uniq = pd.DatetimeIndex(sorted(set(sample_dates)))

        def _day(k):
            """第 k 个唯一日期的 YYYY-MM-DD。"""
            k = int(k)
            if k < 0:
                k = 0
            if k >= len(uniq):
                k = len(uniq) - 1
            return str(uniq[k].date())

        segments["train_start"] = _day(0)
        segments["train_end"] = _day(split["train_date_end"] - 1)
        if use_val and split["val_date_end"] > split["val_date_start"]:
            segments["dev_start"] = _day(split["val_date_start"])
            segments["dev_end"] = _day(split["val_date_end"] - 1)
        segments["holdout_start"] = _day(split["test_date_start"])
        segments["holdout_end"] = _day(split["n_dates"] - 1)
    else:
        if use_val:
            train_end = int(n * (1 - args.val_ratio - args.test_ratio))
            val_end = int(n * (1 - args.test_ratio))
        else:
            train_end = int(n * (1 - args.test_ratio))
            val_end = None
        val_start = train_end
        test_start = val_end if val_end is not None else train_end
        split_mode = "row"

    if use_val:
        n_val = val_end - val_start
        emit({"type": "info", "phase": "split", "split_mode": split_mode,
              "n_train": train_end, "n_val": n_val, "n_test": n - test_start,
              "n_features": X.shape[1], "gap_dates": segments["gap_dates"],
              "dev": [segments["dev_start"], segments["dev_end"]],
              "holdout": [segments["holdout_start"], segments["holdout_end"]]})
        return (X[:train_end], y[:train_end],
                X[val_start:val_end], y[val_start:val_end],
                X[test_start:], y[test_start:], feature_cols, segments)

    emit({"type": "info", "phase": "split", "split_mode": split_mode,
          "n_train": train_end, "n_val": 0, "n_test": n - test_start,
          "n_features": X.shape[1], "gap_dates": segments["gap_dates"],
          "dev": None, "holdout": [segments["holdout_start"], segments["holdout_end"]]})
    return (X[:train_end], y[:train_end], None, None,
            X[test_start:], y[test_start:], feature_cols, segments)


def train_xgb(X_train, y_train, X_val, y_val, X_test, y_test, params, args, feature_cols):
    """训练 XGBoost 模型，返回 booster"""
    xgb_params = {
        "objective": args.objective,
        "verbosity": 0,
        **params,
        **FIXED_PARAMS,
    }
    if args.label_type == "classification" and args.num_class > 1:
        xgb_params["num_class"] = args.num_class

    dtrain = xgb.DMatrix(X_train, label=y_train, feature_names=feature_cols)
    dval = xgb.DMatrix(X_val, label=y_val, feature_names=feature_cols) if X_val is not None else None
    dtest = xgb.DMatrix(X_test, label=y_test, feature_names=feature_cols) if len(X_test) > 0 else None

    # eval 集只能是验证集。留出段（X_test）绝不能进 eval —— 那等于把
    # 留出集喂给训练过程。（本函数不做 early stopping，eval 只用于监控，
    # 但口径仍要摆正，免得以后加了 early stopping 就变成真泄漏。）
    eval_set = dval
    evals = [(dtrain, "train")] + ([(eval_set, "eval")] if eval_set is not None else [])
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
        "label": label_meta(args),
    }
    meta_path = out_dir / f"trial_{trial_number}.meta.json"
    meta_path.write_text(json.dumps(meta, indent=2))
    return str(model_path)


def call_fast_backtest(args, trial_model_path, trial_meta_path):
    """调 POST /v0/backtest（快速模式），返回 (响应 dict, 错误信息)。

    返回整个响应而不只是 summary：按窗口重算指标需要 daily_dates /
    daily_returns，而 summary 是整段历史的汇总。
    """
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
    return resp.json(), None


def score_of(metrics: dict, metric: str) -> float:
    """把指标转成"越大越好"的分数。

    回撤在服务端是正数（max_drawdown_ratio 返回回撤幅度），直接拿它做
    maximize 会挑出回撤最大的那组参数 —— 必须取负号统一方向。
    """
    v = float(metrics.get(metric, 0.0) or 0.0)
    if metric == "max_drawdown":
        return -v
    return v


def safe_round(value, digits=6):
    """JSON 安全的四舍五入：NaN/Inf → None。

    Python 的 json 会把 NaN/Inf 写成 ``NaN`` / ``Infinity``，这不是合法 JSON，
    服务端 nlohmann::json 会直接解析失败 —— 整个 trial 的结果就丢了。
    """
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return round(f, digits) if np.isfinite(f) else None


def window_metrics(response: dict, start_date, end_date):
    """按日期窗口重算回测指标；窗口内没有样本时返回 None。"""
    if not response:
        return None
    dates = response.get("daily_dates") or []
    rets = response.get("daily_returns") or []
    if not dates or not rets:
        return None
    try:
        window = slice_window(dates, rets, start_date, end_date)
    except ValueError:
        return None
    if window.size < 2:
        return None
    return compute_metrics(window)


def apply_fdr(trials_record, fdr_alpha: float) -> int:
    """对各 trial 的 DEV 显著性做 BH-FDR 校正，回填 dev_q / q_pass。

    n_trials 个 trial 就是 n_trials 次"这个配置有正收益"的检验。不校正的话，
    最优 trial 的指标里混着运气。

    注意 TPE 会把新样本采到当前最优点附近，各 trial 之间并不独立，BH 在这里
    偏保守；它给出的是"在 n 次搜索下有多少 trial 的优势站得住"的粗略判断。

    返回通过校正的 trial 数。
    """
    for t in trials_record:
        t.setdefault("dev_q", None)
        t.setdefault("q_pass", False)
    scored = [t for t in trials_record
              if t.get("status") == "ok" and t.get("dev_p") is not None]
    if scored:
        qs = bh_qvalues([t["dev_p"] for t in scored], m_tests=len(scored))
        for t, q in zip(scored, qs):
            t["dev_q"] = safe_round(q)
            t["q_pass"] = bool(np.isfinite(q) and q <= fdr_alpha)
    return int(sum(1 for t in trials_record if t.get("q_pass")))


def select_trial(trials_record, select_by: str):
    """按口径选出最终 trial；选不出来时返回 None。

    select_by="dev_metric" —— 取 DEV 指标最高的 trial。
    select_by="q"          —— 只在通过 BH-FDR 的 trial 里取 DEV 指标最高的；
                               一个都没通过时返回 None（由调用方决定是否退回）。
    """
    ok = [t for t in trials_record if t.get("status") == "ok"]
    if not ok:
        return None
    if select_by == "q":
        candidates = [t for t in ok if t.get("q_pass")]
        if not candidates:
            return None
        return max(candidates, key=lambda t: t["dev_value"])
    return max(ok, key=lambda t: t["dev_value"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True, help="训练数据 CSV（Python 训练用）")
    parser.add_argument("--objective", default="multi:softprob")
    parser.add_argument("--num-class", type=int, default=3)
    parser.add_argument("--test-ratio", type=float, default=0.2,
                        help="留出段（HOLDOUT）比例，只用于最终评估")
    parser.add_argument("--val-ratio", type=float, default=0.15,
                        help="选择段（DEV）比例，用于挑选超参；数据量 < 1000 时退化为 0")
    parser.add_argument("--gap-bars", type=int, default=0,
                        help="段间隔离长度（唯一交易日数）；0 = 取 label-period")
    parser.add_argument("--select-window", default="val", choices=["val", "full"],
                        help="val=按 DEV 段选超参（默认，无泄漏）；full=整段历史（旧行为，测试期参与选择）")
    parser.add_argument("--fdr-alpha", type=float, default=0.10,
                        help="BH-FDR 显著性水平")
    parser.add_argument("--select-by", default="dev_metric", choices=["dev_metric", "q"],
                        help="dev_metric=按 DEV 指标选（默认）；q=只在 FDR 通过的 trial 里选")
    parser.add_argument("--param-domains", default="{}")
    parser.add_argument("--n-trials", type=int, default=50)
    parser.add_argument("--optimize-metric", default="sharpe",
                        choices=list(METRIC_TO_SUMMARY.keys()))
    parser.add_argument("--start-date", default="")
    parser.add_argument("--end-date", default="")
    parser.add_argument("--frequency", default="1d")

    # 快速回测参数
    parser.add_argument("--base-url", required=True, help="服务地址（C++ 传入）")
    parser.add_argument("--auth-token", default="")
    parser.add_argument("--feature-cache", required=True,
                        help="/v0/ml collect 产出的特征 CSV 路径")
    parser.add_argument("--fast-backtest-strategy", required=True,
                        help="回测策略 JSON 路径（XGBoost 节点 → CacheFeatureNode 链路）")
    parser.add_argument("--output-dir", default="")

    add_label_args(parser)
    args = parser.parse_args()

    # 输出目录：data/models/experiments/optimize_fast_{ts}/
    # XGBoostNode 从 {dbPath}/models/ 解析 modelFile，所以模型必须保存在 data/models/ 下
    if not args.output_dir:
        from datetime import datetime
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output_dir = f"data/models/experiments/optimize_fast_{ts}"

    try:
        domains = json.loads(args.param_domains) if args.param_domains else {}
    except json.JSONDecodeError as e:
        emit({"type": "error", "message": f"param-domains JSON 解析失败: {e}"})
        sys.exit(1)

    domains = dict(DEFAULT_DOMAINS) if not domains else {**DEFAULT_DOMAINS, **domains}

    metric = args.optimize_metric
    summary_field = METRIC_TO_SUMMARY[metric]

    # ── 加载训练数据 ──
    (X_train, y_train, X_val, y_val, X_test, y_test,
     feature_cols, segments) = load_and_prepare_data(args)
    if len(X_train) == 0:
        emit({"type": "error", "message": "训练集为空"})
        sys.exit(1)

    # 选择窗口：DEV 段。选超参只看这段，留出段不参与任何选择。
    select_window = args.select_window
    dev_start = segments.get("dev_start")
    dev_end = segments.get("dev_end")
    if select_window == "val" and not (dev_start and dev_end):
        emit({"type": "warning",
              "code": "no_dev_window",
              "message": "没有可用的 DEV 选择段（样本数不足 1000 或未按日期切分），"
                         "退化为整段历史选择 —— 该口径下测试期参与了超参选择，"
                         "报出的指标是乐观的，不能据此下结论",
              "detail": {"n_samples": int(len(X_train)),
                         "min_samples": 1000,
                         "fallback": "full"}})
        select_window = "full"

    is_classification = args.label_type == "classification"
    if is_classification:
        n_classes = len(set(y_train[~np.isnan(y_train)]))
        if n_classes <= 1:
            emit({"type": "error", "message": "训练集标签只有 {n_classes} 个类别"})
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
          "search_params": list(domains.keys()),
          "select_window": select_window,
          "dev": [dev_start, dev_end] if select_window == "val" else None,
          "holdout": [segments.get("holdout_start"), segments.get("holdout_end")],
          "gap_dates": segments.get("gap_dates")})

    # ── Optuna 搜索 ──
    total_start = time.time()
    trials_record = []
    best_value_so_far = -float("inf")
    nw_lag = max(10, 2 * int(args.label_period))

    def objective(trial):
        nonlocal best_value_so_far

        trial_start = time.time()
        params = suggest_params(trial, domains)

        # 进度心跳：trial 结束前 Python 无任何输出，若单个阶段的静默时长超过
        # C++ 侧 readLine 空闲超时，会被误判为"脚本未输出 result"。每个阶段开始
        # 时 emit 一次，既维持输出又供前端显示进度。
        emit({"type": "trial_progress", "number": trial.number, "phase": "training"})

        # 训练
        booster = train_xgb(X_train, y_train, X_val, y_val, X_test, y_test, params, args, feature_cols)

        # 保存模型
        trial_model_path = save_model_and_get_path(booster, feature_cols, args, trial.number)

        emit({"type": "trial_progress", "number": trial.number, "phase": "backtesting"})

        # 快速回测
        response, err = call_fast_backtest(args, trial_model_path,
                                           str(Path(trial_model_path).with_suffix(".meta.json")))
        duration_ms = int((time.time() - trial_start) * 1000)

        if err or response is None:
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

        summary = response.get("summary", {}) or {}

        # 选择指标：DEV 段的窗口指标；拿不到窗口就退回整段 summary
        dev_stats = None
        if select_window == "val":
            dev_stats = window_metrics(response, dev_start, dev_end)
        if dev_stats is None:
            value = score_of(summary, metric)
            dev_stats = {"n_days": 0}
        else:
            value = score_of(dev_stats, metric)

        if not np.isfinite(value):
            trial_info = {
                "number": trial.number,
                "value": None,
                "best": round(best_value_so_far, 6) if best_value_so_far > -float("inf") else None,
                "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in params.items()},
                "duration_ms": duration_ms,
                "status": "failed",
                "error": f"指标 {metric} 非有限值（{value}）",
            }
            trials_record.append(trial_info)
            emit({"type": "trial", **trial_info})
            return -1e9

        # 留出段的指标（最终只报一次，不参与选择）
        holdout_stats = window_metrics(response,
                                       segments.get("holdout_start"),
                                       segments.get("holdout_end"))
        holdout_value = (score_of(holdout_stats, metric)
                         if holdout_stats is not None else None)

        # 显著性：DEV 窗口日均收益的 Newey-West t 与 p 值
        dev_t = None
        dev_p = None
        if select_window == "val":
            dates = response.get("daily_dates") or []
            rets = response.get("daily_returns") or []
            if dates and rets:
                try:
                    dev_rets = slice_window(dates, rets, dev_start, dev_end)
                except ValueError:
                    dev_rets = None
                if dev_rets is not None and dev_rets.size >= 10:
                    dev_t, dev_p = newey_west_t(dev_rets, nw_lag)

        if value > best_value_so_far:
            best_value_so_far = value

        trial_info = {
            "number": trial.number,
            "value": round(value, 6),
            "best": round(best_value_so_far, 6),
            "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in params.items()},
            "duration_ms": duration_ms,
            "status": "ok",
            "summary": summary,
            "dev_value": safe_round(value),
            "dev_n_days": int(dev_stats.get("n_days", 0)),
            "dev_t": safe_round(dev_t, 4),
            "dev_p": safe_round(dev_p),
            "holdout_value": safe_round(holdout_value),
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

    # ── 多重检验校正 ──
    # n_trials 个 trial 就是 n_trials 次"这个配置有正收益"的检验。不校正的话，
    # 最优 trial 的指标里混着运气。这里对每个 trial 的 DEV 窗口日均收益做
    # Newey-West t 检验，再对 p 值做 BH-FDR（口径与特征门禁一致）。
    n_q_pass = apply_fdr(trials_record, args.fdr_alpha)

    ok_trials = [t for t in trials_record if t.get("status") == "ok"]

    # ── 选优 ──
    selected = select_trial(trials_record, args.select_by)
    if selected is None and args.select_by == "q":
        emit({"type": "warning",
              "code": "no_trial_passed_fdr",
              "message": f"没有 trial 通过 BH-FDR（alpha={args.fdr_alpha}，"
                         f"n_trials={len(ok_trials)}），退回按 DEV 指标选 —— "
                         f"选出的最优 trial 的优势未必站得住",
              "detail": {"fdr_alpha": args.fdr_alpha,
                         "n_trials": len(ok_trials)}})
        selected = select_trial(trials_record, "dev_metric")

    if selected is None:
        emit({"type": "error", "message": "没有成功的 trial"})
        sys.exit(1)

    best_params = {k: (round(v, 6) if isinstance(v, float) else v)
                   for k, v in selected["params"].items()}
    best_value = float(selected["dev_value"])
    holdout_value = selected.get("holdout_value")
    selection_bias = (best_value - float(holdout_value)
                      if holdout_value is not None else None)

    # 拷贝选中的模型到 best.json（连同 meta —— XGBoostNode 靠 meta 里的
    # features 字段恢复推理顺序，只拷 .json 会得到一个装不上的模型）
    best_dest = Path(args.output_dir) / "best.json"
    selected_model = Path(args.output_dir) / f"trial_{selected['number']}.json"
    if selected_model.exists():
        shutil.copy(selected_model, best_dest)
        meta_src = Path(args.output_dir) / f"trial_{selected['number']}.meta.json"
        if meta_src.exists():
            shutil.copy(meta_src, Path(args.output_dir) / "best.meta.json")

    emit({
        "type": "result",
        "best_params": best_params,
        "best_value": safe_round(best_value),
        "best_trial_number": selected["number"],
        "best_model": str(best_dest) if best_dest.exists() else None,
        "n_trials": args.n_trials,
        "n_completed": len(study.trials),
        "trials": trials_record,
        "importance": importance,
        "optimization_duration_ms": total_duration_ms,
        "metric": metric,
        "output_dir": args.output_dir,
        "selection": {
            "window": select_window,
            "metric": metric,
            "select_by": args.select_by,
            "fdr_alpha": float(args.fdr_alpha),
            "n_trials_scored": len(ok_trials),
            "n_trials_q_pass": int(n_q_pass),
            "dev_start": dev_start if select_window == "val" else None,
            "dev_end": dev_end if select_window == "val" else None,
            "holdout_start": segments.get("holdout_start"),
            "holdout_end": segments.get("holdout_end"),
            "gap_dates": segments.get("gap_dates"),
            "best_dev_value": safe_round(best_value),
            "holdout_value": safe_round(holdout_value),
            "selection_bias": safe_round(selection_bias),
            "optuna_best_trial_number": study.best_trial.number,
        },
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