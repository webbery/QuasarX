"""
符号回归波动率公式发现（PySR）

基于 ERRLESS 论文思路，用 PySR 从历史行情中自动搜索最优波动率预测公式。

输入参数：
  --csv-path       行情 CSV 路径（标准 OHLCV 格式）
  --symbol         标的代码（用于日志）
  --horizon        前瞻窗口 H（默认 5，即预测未来 5 日波动率）
  --operators      算子集（默认 "safe"，可选 "full" / "minimal" / 自定义逗号分隔）
  --niterations    PySR 搜索迭代数（默认 200）
  --populations    种群数（默认 30）
  --maxsize        表达式最大节点数（默认 15）
  --topk           输出 Top-K 公式（默认 10）
  --timeout        超时秒数（默认 300）
  --features       自定义特征列名（逗号分隔，可选，默认使用全部内置特征）

输出（stdout 每行 JSON）：
  {"type":"info", "message":"..."}
  {"type":"progress", "iteration":N, "best_score":..., "best_expr":"..."}
  {"type":"result", "equations":[...], "metadata":{...}}
  {"type":"error", "message":"..."}

依赖：pysr, pandas, numpy, scikit-learn
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


def emit(obj):
    print(json.dumps(obj, ensure_ascii=False, default=str, separators=(",", ":")), flush=True)


# ── 特征工程 ─────────────────────────────────────────────────

def compute_features(df, horizon):
    """从 OHLCV 数据计算波动率预测特征集"""
    c = df["close"].values.astype(np.float64)
    o = df["open"].values.astype(np.float64)
    h = df["high"].values.astype(np.float64)
    l = df["low"].values.astype(np.float64)
    v = df["volume"].values.astype(np.float64)
    n = len(c)

    feat = {}

    # ── 收益率类 ──
    ret = np.zeros(n)
    ret[1:] = c[1:] / c[:-1] - 1.0
    feat["return_1d"] = ret

    abs_ret = np.abs(ret)
    feat["abs_return"] = abs_ret

    # 5 日收益
    ret_5d = np.zeros(n)
    ret_5d[5:] = c[5:] / c[:-5] - 1.0
    feat["return_5d"] = ret_5d

    # 收益平方（GARCH 风格）
    feat["return_sq"] = ret ** 2

    # ── 波动率类 ──
    # 已实现波动率 5d
    rv5 = np.zeros(n)
    for i in range(5, n):
        rv5[i] = np.std(ret[i - 4:i + 1])
    feat["realized_vol_5d"] = rv5

    # 已实现波动率 20d
    rv20 = np.zeros(n)
    for i in range(20, n):
        rv20[i] = np.std(ret[i - 19:i + 1])
    feat["realized_vol_20d"] = rv20

    # ATR 归一化（14 日）
    tr = np.zeros(n)
    for i in range(1, n):
        tr[i] = max(h[i] - l[i], abs(h[i] - c[i - 1]), abs(l[i] - c[i - 1]))
    atr14 = np.zeros(n)
    for i in range(14, n):
        atr14[i] = np.mean(tr[i - 13:i + 1])
    feat["atr_norm"] = np.where(c > 0, atr14 / c, 0.0)

    # Parkinson 波动率（利用 high/low，5 日）
    park = np.zeros(n)
    coeff = 1.0 / (4.0 * np.log(2.0))
    for i in range(5, n):
        log_hl = np.log(h[i - 4:i + 1] / np.maximum(l[i - 4:i + 1], 1e-10))
        park[i] = np.sqrt(coeff * np.mean(log_hl ** 2))
    feat["parkinson_vol_5d"] = park

    # ── 量价类 ──
    # 成交量比率（当日 / 5 日均量）
    vol_ma5 = np.zeros(n)
    for i in range(5, n):
        vol_ma5[i] = np.mean(v[i - 4:i + 1])
    feat["volume_ratio"] = np.where(vol_ma5 > 0, v / vol_ma5, 1.0)

    # 成交量波动率
    vol_vol = np.zeros(n)
    for i in range(10, n):
        vol_vol[i] = np.std(v[i - 9:i + 1]) / max(np.mean(v[i - 9:i + 1]), 1.0)
    feat["volume_vol"] = vol_vol

    # ── 微观结构类 ──
    # 高低价差归一化
    feat["hl_spread"] = np.where(c > 0, (h - l) / c, 0.0)

    # 收盘位置（(close - low) / (high - low)）
    feat["close_location"] = np.where(h - l > 0, (c - l) / (h - l), 0.5)

    # ── 状态信号类 ──
    # Z-Score（20 日）
    mean20 = np.zeros(n)
    std20 = np.zeros(n)
    for i in range(20, n):
        mean20[i] = np.mean(c[i - 19:i + 1])
        std20[i] = np.std(c[i - 19:i + 1])
    feat["zscore_20d"] = np.where(std20 > 0, (c - mean20) / std20, 0.0)

    # 波动率比率（短期 / 长期）
    feat["vol_ratio"] = np.where(rv20 > 0, rv5 / rv20, 1.0)

    # ── 目标变量：前瞻已实现波动率 ──
    target = np.full(n, np.nan)
    for i in range(n - horizon):
        target[i] = np.std(ret[i + 1:i + 1 + horizon])

    return feat, target


# ── 算子库定义 ──────────────────────────────────────────────

OPERATOR_PRESETS = {
    "safe": ["+", "-", "*", "/", "sqrt", "square", "abs"],
    "minimal": ["+", "-", "*", "/"],
    "full": ["+", "-", "*", "/", "sqrt", "square", "abs", "cube", "exp", "log", "sin", "cos"],
}


def resolve_operators(name_or_csv):
    if name_or_csv in OPERATOR_PRESETS:
        return OPERATOR_PRESETS[name_or_csv]
    return [op.strip() for op in name_or_csv.split(",") if op.strip()]


# ── 主流程 ──────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Symbolic Regression for Volatility")
    parser.add_argument("--csv-path", required=True)
    parser.add_argument("--symbol", default="unknown")
    parser.add_argument("--horizon", type=int, default=5)
    parser.add_argument("--operators", default="safe")
    parser.add_argument("--niterations", type=int, default=200)
    parser.add_argument("--populations", type=int, default=30)
    parser.add_argument("--maxsize", type=int, default=15)
    parser.add_argument("--topk", type=int, default=10)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--features", default="")  # 空=全部
    args = parser.parse_args()

    emit({"type": "info", "message": f"符号回归启动: symbol={args.symbol}, horizon={args.horizon}"})

    # ── 1. 加载数据 ──
    if not os.path.exists(args.csv_path):
        emit({"type": "error", "message": f"CSV 不存在: {args.csv_path}"})
        sys.exit(1)

    try:
        df = pd.read_csv(args.csv_path)
    except Exception as e:
        emit({"type": "error", "message": f"CSV 读取失败: {e}"})
        sys.exit(1)

    required_cols = {"open", "close", "high", "low", "volume"}
    missing = required_cols - set(df.columns)
    if missing:
        emit({"type": "error", "message": f"CSV 缺少列: {missing}"})
        sys.exit(1)

    emit({"type": "info", "message": f"数据加载完成: {len(df)} 行, 列={list(df.columns)}"})

    # ── 2. 特征工程 ──
    feat, target = compute_features(df, args.horizon)

    # 构建 DataFrame（去除 NaN 行）
    all_feature_names = sorted(feat.keys())
    if args.features:
        selected = [f.strip() for f in args.features.split(",")]
        all_feature_names = [f for f in all_feature_names if f in selected]

    feat_df = pd.DataFrame({k: feat[k] for k in all_feature_names})
    feat_df["__target__"] = target

    # 去除 NaN
    clean = feat_df.dropna().reset_index(drop=True)
    # 去除 inf
    clean = clean.replace([np.inf, -np.inf], np.nan).dropna().reset_index(drop=True)

    if len(clean) < 30:
        emit({"type": "error", "message": f"有效样本不足: {len(clean)}（需 >= 30）"})
        sys.exit(1)

    X = clean[all_feature_names].values
    y = clean["__target__"].values

    emit({"type": "info", "message": f"特征矩阵: {X.shape}, 目标范围=[{y.min():.6f}, {y.max():.6f}]"})
    emit({"type": "info", "message": f"使用特征: {all_feature_names}"})

    # ── 3. PySR 搜索 ──
    try:
        from pysr import PySRRegressor
    except ImportError:
        emit({"type": "error", "message": "未安装 pysr，请执行: pip install pysr"})
        sys.exit(1)

    operators = resolve_operators(args.operators)
    emit({"type": "info", "message": f"算子库: {operators}"})

    model = PySRRegressor(
        niterations=args.niterations,
        populations=args.populations,
        maxsize=args.maxsize,
        binary_operators= [op for op in operators if op in ("+", "-", "*", "/")],
        unary_operators=[op for op in operators if op not in ("+", "-", "*", "/")],
        loss="L2DistLoss()",
        timeout_in_seconds=args.timeout,
        maxdepth=None,  # 由 maxsize 控制
        constraints=None,
        nesting_constraint=None,
        # 数值稳定性
        parsimony=0.003,
        # 输出控制
        progress=False,
        temp_equation_buffer=True,
        random_state=42,
    )

    emit({"type": "info", "message": f"PySR 开始搜索: niter={args.niterations}, pop={args.populations}, maxsize={args.maxsize}"})
    t0 = time.time()

    try:
        model.fit(X, y, variable_names=all_feature_names)
    except Exception as e:
        emit({"type": "error", "message": f"PySR 搜索失败: {e}\n{traceback.format_exc()}"})
        sys.exit(1)

    elapsed = time.time() - t0
    emit({"type": "info", "message": f"搜索完成: 耗时 {elapsed:.1f}s"})

    # ── 4. 提取结果 ──
    equations_df = model.equations_
    if equations_df is None or len(equations_df) == 0:
        emit({"type": "error", "message": "PySR 未找到任何公式"})
        sys.exit(1)

    # 按 score 排序（PySR 的 score 列是支配分数，越高越好）
    if "score" in equations_df.columns:
        equations_df = equations_df.sort_values("score", ascending=False)
    else:
        equations_df = equations_df.sort_values("loss", ascending=True)

    topk = equations_df.head(args.topk)

    equations_out = []
    for idx, (_, row) in enumerate(topk.iterrows()):
        eq = {
            "rank": idx + 1,
            "expression": str(row.get("equation", "")),
            "score": float(row.get("score", 0)),
            "loss": float(row.get("loss", 0)),
            "complexity": int(row.get("complexity", 0)),
            "r2": None,
        }
        # 计算 R²
        try:
            from pysr import sympy2jax  # noqa: F401
            import sympy
            expr_sym = sympy.sympify(str(row.get("equation", "0")))
            lambdified = sympy.lambdify(all_feature_names, expr_sym, modules=["numpy"])
            y_pred = lambdified(*[X[:, i] for i in range(len(all_feature_names))])
            ss_res = np.sum((y - y_pred) ** 2)
            ss_tot = np.sum((y - np.mean(y)) ** 2)
            eq["r2"] = float(1 - ss_res / ss_tot) if ss_tot > 0 else 0.0
        except Exception:
            pass
        equations_out.append(eq)

    # ── 5. 输出结果 ──
    result = {
        "type": "result",
        "equations": equations_out,
        "metadata": {
            "symbol": args.symbol,
            "n_samples": int(len(clean)),
            "n_features": int(len(all_feature_names)),
            "feature_names": all_feature_names,
            "horizon": args.horizon,
            "operators": operators,
            "niterations": args.niterations,
            "populations": args.populations,
            "maxsize": args.maxsize,
            "elapsed_seconds": round(elapsed, 1),
            "target_range": [round(float(y.min()), 8), round(float(y.max()), 8)],
            "target_mean": round(float(y.mean()), 8),
        },
    }
    emit(result)


if __name__ == "__main__":
    main()
