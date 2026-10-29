#!/usr/bin/env python3
"""策略稳定性扫描（瘦 CLI）。

职责边界
--------
- `wide_cache`：载入宽表 + 前瞻收益/可交易掩码（与特征门禁同一口径）
- `stability`：三项分析的实现（drift / block / signal）
- 本文件：解析参数、组织日期窗口、跑检查、输出**一份 JSON**

产物只有 JSON（--json-out），不写 md/csv、不画图：前端直接消费该文件，
历史对比靠 run_id + 文件自身。stdout 仍然是"一行一个 JSON 事件"，
供服务端 SSE 转发，约定同 feature_screen.py / xgboost_train.py。

用法
----
  python service/tools/stability_scan.py \
    --csv /abs/xgb_debug.csv \
    --json-out /abs/reports/stability/stab_.../stability.json \
    --run-id stab_20261029_120000_0042 \
    --emit-events

输出 JSON 顶层
--------------
  schema  : "stability/v1"
  meta    : run_id / created_at / elapsed_ms / inputs / params
  summary : 判决计数与主信号结论（list 接口直接用它）
  drift   : 每特征 PSI/KS/缺失率/逐箱/滚动 PSI
  block   : 每信号逐块 IC/top-k 与汇总
  signal  : 每信号分桶前瞻收益表与单调性
  warnings: 结构化告警
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import time
import traceback
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from stability import (_clean, block_stability, detect_signals, drift_scan,
                       resolve_windows, signal_curve)
from wide_cache import (build_trade_mask, compute_forward, load_feature_cache,
                        load_ohlcv, vol_scale_forward, winsorize_cs)


def emit(obj: dict) -> None:
    """输出一行紧凑 JSON（C++ 侧按 type 字段路由成 SSE 事件）。"""
    print(json.dumps(obj, ensure_ascii=False, default=str,
                     separators=(",", ":")), flush=True)


def parse_int_list(s: str) -> list:
    return [int(x) for x in str(s).split(",") if str(x).strip()]


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="策略稳定性扫描（只输出 JSON）")
    ap.add_argument("--csv", required=True, help="宽表特征缓存/DebugNode 导出 CSV")
    ap.add_argument("--json-out", required=True, help="结果 JSON 路径")
    ap.add_argument("--run-id", default="", help="服务端生成的 run_id（写进 meta）")
    ap.add_argument("--strategy-id", default="", help="策略标识，用于历史对比分组")
    ap.add_argument("--ohlcv-cache", default="", help="OHLCV 宽表/长表/目录")
    ap.add_argument("--entry", choices=["close", "next_open"], default="close")
    ap.add_argument("--return-horizon", type=int, default=5,
                    help="分块稳定性的主前瞻期")
    ap.add_argument("--horizons", default="1,5,10,20",
                    help="信号条件曲线的横轴（主前瞻期会自动并入）")
    ap.add_argument("--checks", default="drift,block,signal",
                    help="drift,block,signal 的任意组合")
    ap.add_argument("--features", default="", help="drift 范围；空=全部")
    ap.add_argument("--signals", default="", help="block/curve 对象；空=自动探测")

    # 漂移
    ap.add_argument("--ref-mode", choices=["first_frac", "window", "dev"],
                    default="first_frac")
    ap.add_argument("--ref-frac", type=float, default=0.5)
    ap.add_argument("--ref-start", default="")
    ap.add_argument("--ref-end", default="")
    ap.add_argument("--dev-end", default="")
    ap.add_argument("--bins", type=int, default=10)
    ap.add_argument("--binning", choices=["auto", "quantile", "width"], default="auto")
    ap.add_argument("--psi-warn", type=float, default=0.10)
    ap.add_argument("--psi-alert", type=float, default=0.25)
    ap.add_argument("--ks-alpha", type=float, default=0.01)
    ap.add_argument("--rolling-days", type=int, default=60)
    ap.add_argument("--rolling-step", type=int, default=20)
    ap.add_argument("--rolling-top", type=int, default=8)
    ap.add_argument("--no-rolling", action="store_true")

    # 分块
    ap.add_argument("--n-blocks", type=int, default=5)
    ap.add_argument("--block-gap", type=int, default=0)
    ap.add_argument("--min-block-days", type=int, default=20)
    ap.add_argument("--min-sign-frac", type=float, default=0.7)
    ap.add_argument("--topk", default="3,5")

    # 信号曲线
    ap.add_argument("--quantiles", type=int, default=5)
    ap.add_argument("--bucket-mode", choices=["auto", "cross_section", "time_series"],
                    default="auto")
    ap.add_argument("--min-bucket-names", type=int, default=5)

    # 通用
    ap.add_argument("--min-names", type=int, default=15)
    ap.add_argument("--nw-lag", type=int, default=0)
    ap.add_argument("--min-obs", type=int, default=200)
    ap.add_argument("--winsorize", type=float, default=0.01)
    ap.add_argument("--limit-eps", type=float, default=0.002)
    ap.add_argument("--min-turnover", type=float, default=0.0)
    ap.add_argument("--emit-events", action="store_true")
    return ap.parse_args()


def sha256_file(path: Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def main() -> None:
    args = parse_args()
    t0 = time.time()

    def step(name: str, status: str, **kw) -> None:
        if args.emit_events:
            emit({"type": "step", "step": name, "status": status, **kw})

    warnings = []

    def warn(code: str, message: str) -> None:
        warnings.append({"code": code, "message": message})
        if args.emit_events:
            emit({"type": "warning", "code": code, "message": message})

    # ---- 校验 ----
    step("validate", "start")
    csv_path = Path(args.csv)
    if not csv_path.exists():
        raise SystemExit(f"CSV 不存在: {csv_path}")
    if args.entry == "next_open" and not args.ohlcv_cache:
        raise SystemExit("--entry next_open 需要 --ohlcv-cache")
    checks = [c.strip() for c in args.checks.split(",") if c.strip()]
    bad = [c for c in checks if c not in ("drift", "block", "signal")]
    if bad or not checks:
        raise SystemExit(f"--checks 非法或为空: {bad or checks}")
    if args.psi_warn <= 0 or args.psi_alert <= args.psi_warn:
        raise SystemExit("需要 0 < psi_warn < psi_alert")
    if args.return_horizon < 1 or args.return_horizon > 250:
        raise SystemExit("--return-horizon 必须在 [1, 250]")
    if args.min_obs < 20:
        raise SystemExit("--min-obs 必须 >= 20")
    step("validate", "done")

    # ---- 载入 ----
    step("load_cache", "start")
    df, symbols, feature_data, close = load_feature_cache(csv_path)
    all_feature_data = dict(feature_data)

    if args.features:
        wanted = [f.strip() for f in args.features.split(",") if f.strip()]
        missing = [f for f in wanted if f not in feature_data]
        if missing:
            raise SystemExit(f"缓存里没有这些特征: {missing}")
        drift_data = {f: feature_data[f] for f in wanted}
    else:
        drift_data = dict(feature_data)

    ohlcv = None
    if args.ohlcv_cache:
        ohlcv = load_ohlcv(Path(args.ohlcv_cache), close.index)
        if ohlcv:
            for f in list(ohlcv):
                ohlcv[f] = ohlcv[f].reindex(columns=symbols)
    step("load_cache", "done", rows=len(df), symbols=len(symbols),
         features=len(feature_data))

    # ---- 前瞻收益 / 掩码 ----
    step("compute_target", "start")
    horizons = sorted(set(parse_int_list(args.horizons)) | {args.return_horizon})
    for h in horizons:
        if h < 1 or h > 250:
            raise SystemExit(f"horizon 超出 [1, 250]: {h}")
    fwd_by_h, mask_by_h = {}, {}
    for h in horizons:
        fwd = compute_forward(close, h, args.entry, ohlcv)
        fwd = winsorize_cs(fwd, args.winsorize)
        fwd_by_h[h] = fwd
        mask_by_h[h] = build_trade_mask(close, h, args.entry, ohlcv,
                                        args.limit_eps, args.min_turnover)
    step("compute_target", "done", horizons=horizons,
         primary_horizon=args.return_horizon)

    # ---- 信号识别 ----
    signals = detect_signals(list(all_feature_data), 
                             [s.strip() for s in args.signals.split(",") if s.strip()])
    if not signals and ("block" in checks or "signal" in checks):
        warn("no_signal_detected",
             "缓存里没有识别到信号列（raw_strength/filtered_zscore/xgb_prediction 等），"
             "block/signal 检查已跳过；可用 --signals 显式指定")
        checks = [c for c in checks if c not in ("block", "signal")]
    if signals:
        step("detect_signals", "done", signals=signals)

    # ---- 窗口 ----
    ref_cfg = {"mode": args.ref_mode, "frac": args.ref_frac,
               "start": args.ref_start, "end": args.ref_end}
    if args.ref_mode == "dev":
        ref_cfg["end"] = args.dev_end
        if not args.dev_end:
            raise SystemExit("--ref-mode dev 需要 --dev-end")
    ref_dates, ref_info, cur_windows = resolve_windows(close.index, ref_cfg, None)
    if not cur_windows or cur_windows[0]["days"] == 0:
        raise SystemExit("当前窗口为空（数据全部落在参考窗内）")

    outputs = {"drift": None, "block": None, "signal": None}

    # ---- drift ----
    if "drift" in checks:
        step("drift", "start", total=len(drift_data))
        drift_cfg = {
            "bins": args.bins, "binning": args.binning,
            "psi_warn": args.psi_warn, "psi_alert": args.psi_alert,
            "ks_alpha": args.ks_alpha, "min_obs": args.min_obs,
            "rolling": {"enabled": not args.no_rolling,
                        "days": args.rolling_days, "step": args.rolling_step,
                        "top": args.rolling_top},
        }

        def drift_progress(i, total, name, psi_value, verdict):
            if args.emit_events:
                emit({"type": "progress", "phase": "drift", "current": i,
                      "total": total, "name": name,
                      "psi": _clean(psi_value), "verdict": verdict})

        res = drift_scan(drift_data, ref_dates, cur_windows, drift_cfg,
                         progress=drift_progress)
        res["ref"] = ref_info
        res["windows"] = [{k: v for k, v in w.items() if k != "dates"}
                          for w in cur_windows]
        outputs["drift"] = res
        step("drift", "done", **res["summary"])

    # ---- block ----
    if "block" in checks:
        step("block", "start", total=len(signals))
        primary_fwd = fwd_by_h[args.return_horizon]
        primary_mask = mask_by_h[args.return_horizon]
        block_cfg = {
            "n_blocks": args.n_blocks, "gap": args.block_gap,
            "horizon": args.return_horizon, "min_block_days": args.min_block_days,
            "min_names": args.min_names, "nw_lag": args.nw_lag,
            "topk": parse_int_list(args.topk),
            "min_sign_frac": args.min_sign_frac,
        }
        block_out = {}
        for si, name in enumerate(signals, 1):
            if args.emit_events:
                emit({"type": "progress", "phase": "block", "current": si,
                      "total": len(signals), "name": name})
            block_out[name] = block_stability(
                all_feature_data[name], primary_fwd, primary_mask,
                close.index, block_cfg)
        outputs["block"] = {"signals": block_out}
        step("block", "done", n_signals=len(block_out))

    # ---- signal curve ----
    if "signal" in checks:
        step("signal", "start", total=len(signals))
        curve_cfg = {
            "quantiles": args.quantiles, "bucket_mode": args.bucket_mode,
            "min_bucket_names": args.min_bucket_names,
            "min_names": args.min_names, "nw_lag": args.nw_lag,
            "primary_horizon": args.return_horizon,
        }
        curve_out = {}
        for si, name in enumerate(signals, 1):
            def curve_progress(i, total, h, _name=name):
                if args.emit_events:
                    emit({"type": "progress", "phase": "signal", "current": i,
                          "total": total, "name": _name, "horizon": h})

            curve_out[name] = signal_curve(
                all_feature_data[name], fwd_by_h, mask_by_h, close.index,
                curve_cfg, progress=curve_progress)
        outputs["signal"] = curve_out
        step("signal", "done", n_signals=len(curve_out))

    # ---- summary ----
    step("report", "start")
    verdicts = {}
    if outputs["drift"] is not None:
        s = outputs["drift"]["summary"]
        verdicts["drift"] = {
            "alert": s.get("alert", 0), "monitor": s.get("monitor", 0),
            "stable": s.get("stable", 0), "insufficient": s.get("insufficient", 0),
            "worst": s.get("worst"),
        }
    if outputs["block"] is not None:
        first = next(iter(outputs["block"]["signals"].items()), None)
        if first:
            name, b = first
            verdicts["block"] = {
                "signal": name, "verdict": b.get("verdict"),
                "sign_consistency": b.get("sign_consistency"),
                "n_blocks": b.get("n_blocks"),
                "trend": b.get("trend"),
                "ic_mean": b.get("ic_mean"),
                "worst_block_ic": b.get("worst_block_ic"),
            }
    if outputs["signal"] is not None:
        first = next(iter(outputs["signal"].items()), None)
        if first:
            name, c = first
            sp = (c.get("spread") or {}).get(str(args.return_horizon), {})
            verdicts["signal"] = {
                "name": name, "verdict": c.get("verdict"),
                "at_horizon": args.return_horizon,
                "spread": sp.get("mean"), "spread_t": sp.get("t"),
                "curve_shape": c.get("curve_shape"),
            }

    summary = {"checks_run": checks, "verdicts": verdicts,
               "warnings": len(warnings)}

    meta = {
        "run_id": args.run_id,
        "created_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "elapsed_ms": int((time.time() - t0) * 1000),
        "tool": "stability_scan.py",
        "inputs": {
            "csv_path": str(csv_path),
            "csv_sha256": sha256_file(csv_path),
            "csv_bytes": int(csv_path.stat().st_size),
            "csv_mtime": datetime.fromtimestamp(
                csv_path.stat().st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
            "strategy_id": args.strategy_id,
            "ohlcv_cache": args.ohlcv_cache,
            "entry": args.entry,
            "rows": int(len(df)),
            "symbols": int(len(symbols)),
            "features": int(len(feature_data)),
            "signals_detected": signals,
            "date_start": str(close.index.min().date()),
            "date_end": str(close.index.max().date()),
        },
        "params": {
            "return_horizon": args.return_horizon,
            "horizons": horizons,
            "checks": checks,
            "features": args.features,
            "signals": signals,
            "ref": ref_cfg,
            "drift": {"bins": args.bins, "binning": args.binning,
                      "psi_warn": args.psi_warn, "psi_alert": args.psi_alert,
                      "ks_alpha": args.ks_alpha,
                      "rolling_days": args.rolling_days,
                      "rolling_step": args.rolling_step,
                      "rolling_top": args.rolling_top,
                      "rolling": not args.no_rolling},
            "block": {"n_blocks": args.n_blocks, "gap": args.block_gap,
                      "min_block_days": args.min_block_days,
                      "min_sign_frac": args.min_sign_frac,
                      "topk": parse_int_list(args.topk)},
            "signal": {"quantiles": args.quantiles,
                       "bucket_mode": args.bucket_mode,
                       "min_bucket_names": args.min_bucket_names},
            "min_names": args.min_names, "nw_lag": args.nw_lag,
            "min_obs": args.min_obs, "winsorize": args.winsorize,
            "limit_eps": args.limit_eps, "min_turnover": args.min_turnover,
        },
    }

    payload = {
        "schema": "stability/v1",
        "meta": meta,
        "summary": summary,
        "drift": outputs["drift"],
        "block": outputs["block"],
        "signal": outputs["signal"],
        "warnings": warnings,
    }
    payload = _clean(payload)

    out_path = Path(args.json_out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    # allow_nan=False：JSON 里不允许出现 NaN，前端与 C++ 都按严格 JSON 解析
    text = json.dumps(payload, ensure_ascii=False, allow_nan=False)
    out_path.write_text(text, encoding="utf-8")
    step("report", "done", json_out=str(out_path), bytes=len(text.encode("utf-8")))

    if args.emit_events:
        emit({"type": "result", "run_id": args.run_id,
              "json_out": str(out_path), "summary": summary})


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
