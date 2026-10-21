"""XGBoost 离线训练入口（瘦 CLI）。

职责边界：
  - ml_dataset：CSV → Dataset（标签、matrix/vector、日期、全局类别编码）
  - ml_cv：单次切分 / walk-forward OOF / refit_full
  - ml_adapter.XGBoostAdapter：XGBoost 的 fit/predict/decode/save

本文件只负责解析参数、组装 XGBoost 参数、选择验证协议并输出 result JSON。
"""

import argparse
import json
import sys
import traceback
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xgb_util import add_label_args, label_meta
from ml_dataset import build_dataset, prepare_dataframe
from ml_adapter import XGBoostAdapter
from ml_cv import run_single_split, run_walk_forward


def emit(obj):
    """输出一行紧凑 JSON 到 stdout（无空格分隔符）。"""
    print(json.dumps(obj, ensure_ascii=False, default=str, separators=(",", ":")), flush=True)


def build_xgb_params(args, dataset, user_params):
    """把 CLI 参数和用户超参组装成 xgb.train 的 params + 轮数/早停。"""
    is_classification = dataset.is_classification
    if is_classification:
        if dataset.n_classes == 2:
            objective = "binary:logistic"
            num_class = 1
        else:
            objective = "multi:softprob"
            num_class = dataset.n_classes
    else:
        # 直接调用脚本时 --objective 默认是 multi:softprob；回归模式下换成默认回归目标
        objective = args.objective if args.objective != "multi:softprob" else "reg:squarederror"
        num_class = 0

    params = {
        "learning_rate": 0.1,
        "max_depth": 6,
        "n_estimators": 200,
        "early_stopping_rounds": 20,
        "subsample": 0.8,
        "colsample_bytree": 0.8,
        "objective": objective,
        "verbosity": 0,
    }
    if is_classification:
        params["eval_metric"] = "mlogloss" if num_class > 1 else "logloss"
    else:
        params["eval_metric"] = "rmse"

    params.update(user_params)
    # objective 必须与 adapter.decode 使用的目标一致，禁止 user_params 覆盖
    params["objective"] = objective
    params.pop("num_class", None)
    if is_classification and num_class > 1:
        params["num_class"] = num_class

    esr = 20
    for k in ("early_stopping_rounds", "early_stopping"):
        if k in params:
            esr = int(params.pop(k))
            break
    params.pop("early_stopping_rounds", None)
    n_estimators = int(params.pop("n_estimators", 200))

    return params, n_estimators, esr, objective


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True, help="特征数据 CSV 路径")
    parser.add_argument("--objective", default="multi:softprob")
    parser.add_argument("--num-class", type=int, default=3)
    parser.add_argument("--model-output", required=True, help="模型保存路径")
    parser.add_argument("--params", default="{}", help="XGBoost 超参数 JSON")
    parser.add_argument("--test-ratio", type=float, default=0.2)
    parser.add_argument("--val-ratio", type=float, default=0.15,
                        help="单次切分的验证集比例；CV 时作为折内 early-stopping 比例")
    parser.add_argument("--start-date", default="", help="训练数据开始日期 (YYYY-MM-DD)")
    parser.add_argument("--end-date", default="", help="训练数据结束日期 (YYYY-MM-DD)")
    parser.add_argument("--frequency", default="1d", help="数据频率 (1d/1w/1M)")
    parser.add_argument("--xtest-output", default="", help="X_test 保存路径（预留）")
    parser.add_argument("--ytest-output", default="", help="y_test 保存路径（预留）")
    parser.add_argument("--cv-json", default="",
                        help="walk-forward 交叉验证配置 JSON；为空或 enabled=false 时走单次切分")
    add_label_args(parser)
    args = parser.parse_args()

    try:
        user_params = json.loads(args.params) if args.params else {}
        cv_cfg = json.loads(args.cv_json) if args.cv_json else {}
    except json.JSONDecodeError as e:
        emit({"type": "error", "message": f"JSON 解析失败: {e}"})
        sys.exit(1)
    if not isinstance(user_params, dict) or not isinstance(cv_cfg, dict):
        emit({"type": "error", "message": "params / cv 必须是 JSON object"})
        sys.exit(1)

    cv_enabled = cv_cfg.get("enabled", False) is True
    if cv_enabled:
        scheme = cv_cfg.get("scheme", "walk_forward")
        final_model = cv_cfg.get("final_model", "refit_full")
        if scheme != "walk_forward":
            emit({"type": "error",
                  "message": f"cv.scheme='{scheme}' 暂不支持，Phase 1 仅支持 walk_forward"})
            sys.exit(1)
        if final_model != "refit_full":
            emit({"type": "error",
                  "message": f"cv.final_model='{final_model}' 暂不支持，Phase 1 仅支持 refit_full"})
            sys.exit(1)

    df = pd.read_csv(args.data)
    emit({"type": "progress", "phase": "load_data", "rows": len(df), "cols": len(df.columns)})

    has_date_col = "date" in df.columns
    df = prepare_dataframe(df, args.start_date, args.end_date)
    if args.start_date or args.end_date:
        if has_date_col:
            emit({"type": "info", "phase": "date_filter",
                  "start": args.start_date, "end": args.end_date,
                  "rows_after": len(df),
                  "date_range": f"{df.index.min()} ~ {df.index.max()}"})
        else:
            emit({"type": "warning", "message": "CSV 无 date 列，跳过日期过滤"})

    dataset = build_dataset(
        df,
        label_source=args.label_source,
        label_period=args.label_period,
        label_type=args.label_type,
        label_shape=args.label_shape,
        vol_k=args.vol_k,
        sigma_mode=args.label_sigma_mode,
        emit=emit,
    )

    params, n_estimators, esr, objective = build_xgb_params(args, dataset, user_params)
    adapter = XGBoostAdapter(
        params=params, n_estimators=n_estimators, esr=esr,
        objective=objective, feature_cols=dataset.feature_cols,
    )

    if cv_enabled:
        result = run_walk_forward(
            dataset, adapter, cv_cfg, args.label_period,
            inner_val_ratio=args.val_ratio,
            model_output=args.model_output, emit=emit,
        )
    else:
        result = run_single_split(
            dataset, adapter, args.test_ratio, args.val_ratio,
            model_output=args.model_output, emit=emit,
        )

    result["type"] = "result"
    result["label"] = label_meta(args)
    emit(result)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:
        emit({"type": "error", "message": f"未捕获异常: {e}",
              "traceback": traceback.format_exc()})
        traceback.print_exc()
        sys.exit(1)
