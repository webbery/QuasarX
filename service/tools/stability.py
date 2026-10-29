#!/usr/bin/env python3
"""三项稳定性分析的实现层（无 CLI）。

与 `ml_stats.py` 的分工
-----------------------
- `ml_stats.py`：统计原语（IC / NW-t / BH / fold / KS / PSI / block_bounds），
  numpy-only、无 pandas，单测覆盖。
- 本模块：宽表 (T, N) 上的组合分析，pandas + ml_stats，无 argparse / 无 IO。

三项分析
--------
1. `drift_scan`：特征分布漂移。PSI 是判决依据，KS 作形状佐证；
   缺失率跳变单独告警（覆盖率漂移常常比数值分布漂移更早出现）。
2. `block_stability`：把样本切成连续块（`ml_stats.block_bounds`，与门禁
   `fold_stats` 同一套切分），逐块报 IC / NW-t / top-k，汇总给
   sign_consistency / trend / decay_ratio。
3. `signal_curve`：信号条件前瞻收益曲线。逐日横截面分桶（或时序分桶），
   报 `Q × horizon` 的均值表 + top-bottom spread + 单调性。

所有返回的 dict 都可以直接 `json.dumps(..., allow_nan=False)`：非有限值
统一转 None（`_clean`）。
"""

from __future__ import annotations

import math

import numpy as np
import pandas as pd

from ml_stats import (bh_qvalues, block_bounds, daily_cs_ic_wide,
                      effective_gap, ks_2samp, newey_west_t, psi, row_ranks,
                      spearman_ic, topk_excess)

__all__ = [
    "drift_scan",
    "block_stability",
    "signal_curve",
    "detect_signals",
    "resolve_windows",
    "SIGNAL_CANDIDATES",
]


# ============================================================
# 通用
# ============================================================

# 自动探测信号列的候选名（非 ML 策略也覆盖）
SIGNAL_CANDIDATES = (
    "raw_strength", "filtered_strength", "filtered_zscore",
    "position", "target_position", "signal", "score",
    "prediction", "xgb_prediction", "xgb_probs_1", "hmm_state",
)


def _clean(obj):
    """JSON 安全：numpy 标量 → python，NaN/Inf → None，Timestamp → str。"""
    if isinstance(obj, dict):
        return {str(k): _clean(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_clean(v) for v in obj]
    if isinstance(obj, (np.bool_,)):
        return bool(obj)
    if isinstance(obj, (np.integer,)):
        return int(obj)
    if isinstance(obj, (np.floating, float)):
        v = float(obj)
        return v if math.isfinite(v) else None
    if isinstance(obj, (pd.Timestamp,)):
        return str(obj)
    return obj


def _finite_flat(a) -> np.ndarray:
    x = np.asarray(a, dtype=float).ravel()
    return x[np.isfinite(x)]


def detect_signals(feature_names, explicit=None) -> list:
    """从特征名里挑出信号列。

    - explicit 非空：原样返回（并校验都存在）
    - 否则按 SIGNAL_CANDIDATES 做后缀匹配（`raw_strength` 或 `xgb.xgb_prediction`）
    """
    names = list(feature_names)
    if explicit:
        missing = [f for f in explicit if f not in names]
        if missing:
            raise SystemExit(f"--signals 里有不存在的特征: {missing}")
        return list(explicit)

    found = []
    for cand in SIGNAL_CANDIDATES:
        for name in names:
            if name == cand or name.endswith("." + cand):
                if name not in found:
                    found.append(name)
    return found


def resolve_windows(index: pd.DatetimeIndex, ref_cfg: dict,
                    windows_cfg=None) -> tuple:
    """解析漂移分析的参考窗/当前窗。

    ref_cfg:
      {"mode": "first_frac", "frac": 0.5}         前 frac 段为基线
      {"mode": "window", "start": "...", "end": "..."}
      {"mode": "dev", "end": "..."}               0 → end 为基线
    windows_cfg: [{"label": "...", "start": "...", "end": "..."}, ...]
      为空时当前窗 = ref 之后剩余全部。

    返回 (ref_dates, ref_info, cur_windows)：
      ref_dates   : DatetimeIndex
      ref_info    : {"mode", "start", "end", "days"}
      cur_windows : [{"label","start","end","days","dates"}, ...]
    """
    index = pd.DatetimeIndex(index).sort_values()
    mode = str(ref_cfg.get("mode", "first_frac"))

    if mode == "first_frac":
        frac = float(ref_cfg.get("frac", 0.5))
        if not (0.0 < frac < 1.0):
            raise SystemExit("drift.ref.frac 必须在 (0, 1)")
        n = int(len(index) * frac)
        n = max(1, min(n, len(index) - 1))
        ref_dates = index[:n]
    elif mode == "window":
        start = ref_cfg.get("start", "")
        end = ref_cfg.get("end", "")
        if not start or not end:
            raise SystemExit("drift.ref.mode=window 需要 start 和 end")
        ref_dates = index[(index >= pd.Timestamp(start)) & (index <= pd.Timestamp(end))]
    elif mode == "dev":
        end = ref_cfg.get("end", "")
        if not end:
            raise SystemExit("drift.ref.mode=dev 需要 end")
        ref_dates = index[index <= pd.Timestamp(end)]
    else:
        raise SystemExit(f"未知 drift.ref.mode: {mode}")

    if len(ref_dates) == 0:
        raise SystemExit("drift 参考窗口为空，检查 ref 配置")

    ref_info = {"mode": mode, "start": str(ref_dates[0].date()),
                "end": str(ref_dates[-1].date()), "days": int(len(ref_dates))}

    after = index[index > ref_dates[-1]]
    cur_windows = []
    if windows_cfg:
        for w in windows_cfg:
            start = w.get("start", "")
            end = w.get("end", "")
            if not start or not end:
                raise SystemExit("drift.windows 每项都需要 start/end")
            dates = index[(index >= pd.Timestamp(start)) & (index <= pd.Timestamp(end))]
            cur_windows.append({
                "label": str(w.get("label", f"{start}~{end}")),
                "start": str(dates[0].date()) if len(dates) else start,
                "end": str(dates[-1].date()) if len(dates) else end,
                "days": int(len(dates)), "dates": dates,
            })
    else:
        cur_windows.append({
            "label": "cur",
            "start": str(after[0].date()) if len(after) else "",
            "end": str(after[-1].date()) if len(after) else "",
            "days": int(len(after)), "dates": after,
        })
    return ref_dates, ref_info, cur_windows


# ============================================================
# 1) 分布漂移
# ============================================================

def _level_stats(x: pd.DataFrame) -> dict:
    v = _finite_flat(x.to_numpy(dtype=float))
    if len(v) == 0:
        return {"n": 0, "mean": None, "std": None,
                "p05": None, "p50": None, "p95": None}
    return {"n": int(len(v)),
            "mean": float(np.mean(v)),
            "std": float(np.std(v, ddof=1)) if len(v) > 1 else 0.0,
            "p05": float(np.percentile(v, 5)),
            "p50": float(np.percentile(v, 50)),
            "p95": float(np.percentile(v, 95))}


def _missing_rate(x: pd.DataFrame) -> float:
    total = x.size
    if total == 0:
        return float("nan")
    valid = np.isfinite(x.to_numpy(dtype=float)).sum()
    return float(1.0 - valid / total)


def _drift_verdict(psi_value, missing_jump, over_warn_frac, over_alert_frac,
                   cfg) -> tuple:
    reasons = []
    warn = float(cfg.get("psi_warn", 0.10))
    alert = float(cfg.get("psi_alert", 0.25))

    if psi_value is not None and psi_value > alert:
        reasons.append(f"psi>{alert:g}")
    if psi_value is not None and psi_value > warn:
        reasons.append(f"psi>{warn:g}")
    if over_alert_frac is not None and over_alert_frac >= 0.20:
        reasons.append("over_alert_symbols>=20%")
    if missing_jump is not None and missing_jump > 0.20:
        reasons.append("missing_jump>0.20")
    if over_warn_frac is not None and over_warn_frac >= 0.20:
        reasons.append("over_warn_symbols>=20%")
    if missing_jump is not None and missing_jump > 0.10:
        reasons.append("missing_jump>0.10")

    if any(r.startswith(("psi>" + f"{alert:g}", "over_alert", "missing_jump>0.20"))
           for r in reasons):
        return "alert", reasons
    if reasons:
        return "monitor", reasons
    return "stable", reasons


def _rolling_psi(x: pd.DataFrame, ref_values: np.ndarray, ref_dates,
                 all_dates, cfg: dict) -> dict:
    """固定 ref、滚动窗口的 PSI 序列。

    窗口 = days 个交易日，步长 = step；只对 ref 之后的日期计算
    （ref 内部 PSI 恒为 0，没有信息量）。
    """
    days = int(cfg.get("days", 60))
    step = int(cfg.get("step", 20))
    if days < 10 or step < 1 or len(all_dates) <= len(ref_dates) + days:
        return {"days": days, "step": step, "dates": [], "psi": []}

    ref_end = ref_dates[-1]
    dates_out, psi_out = [], []
    pos = len(ref_dates)
    while pos + days <= len(all_dates):
        win_dates = all_dates[pos:pos + days]
        cur_values = _finite_flat(x.loc[win_dates].to_numpy(dtype=float))
        if len(cur_values) >= 30:
            res = psi(ref_values, cur_values, bins=int(cfg.get("bins", 10)),
                      binning=str(cfg.get("binning", "auto")))
            dates_out.append(str(win_dates[-1].date()))
            psi_out.append(res["psi"])
        pos += step
    return {"days": days, "step": step, "dates": dates_out, "psi": psi_out}


def _drift_one(x: pd.DataFrame, ref_dates, cur_dates, cfg: dict) -> dict:
    ref = x.loc[ref_dates]
    cur = x.loc[cur_dates]
    ref_values = _finite_flat(ref.to_numpy(dtype=float))
    cur_values = _finite_flat(cur.to_numpy(dtype=float))
    min_obs = int(cfg.get("min_obs", 200))

    if len(ref_values) < min_obs or len(cur_values) < min_obs:
        return {"verdict": "insufficient",
                "reasons": [f"n<{min_obs}"],
                "n_ref": int(len(ref_values)), "n_cur": int(len(cur_values)),
                "psi": None, "ks": None, "ks_p": None,
                "missing_ref": _missing_rate(ref),
                "missing_cur": _missing_rate(cur),
                "level_ref": _level_stats(ref), "level_cur": _level_stats(cur)}

    psi_res = psi(ref_values, cur_values,
                  bins=int(cfg.get("bins", 10)),
                  binning=str(cfg.get("binning", "auto")),
                  eps=float(cfg.get("eps", 1e-6)))
    ks_res = ks_2samp(ref_values, cur_values)

    # 逐标的 PSI：池化 PSI 看不出"少数标的漂移"
    min_sym = int(cfg.get("min_symbol_obs", 30))
    sym_psis = []
    for sym in x.columns:
        a = _finite_flat(ref[sym].to_numpy(dtype=float))
        b = _finite_flat(cur[sym].to_numpy(dtype=float))
        if len(a) < min_sym or len(b) < min_sym:
            continue
        r = psi(a, b, bins=int(cfg.get("bins", 10)),
                binning=str(cfg.get("binning", "auto")),
                eps=float(cfg.get("eps", 1e-6)))
        if math.isfinite(r["psi"]):
            sym_psis.append(float(r["psi"]))

    by_symbol = {"n": len(sym_psis)}
    if sym_psis:
        arr = np.asarray(sym_psis, dtype=float)
        warn = float(cfg.get("psi_warn", 0.10))
        alert = float(cfg.get("psi_alert", 0.25))
        by_symbol.update({
            "median": float(np.median(arr)),
            "p10": float(np.percentile(arr, 10)),
            "p90": float(np.percentile(arr, 90)),
            "over_warn": float((arr > warn).mean()),
            "over_alert": float((arr > alert).mean()),
            "max": float(arr.max()),
        })
    else:
        by_symbol.update({"median": None, "p10": None, "p90": None,
                          "over_warn": None, "over_alert": None, "max": None})

    missing_ref = _missing_rate(ref)
    missing_cur = _missing_rate(cur)
    missing_jump = abs(missing_cur - missing_ref)

    verdict, reasons = _drift_verdict(
        psi_res["psi"], missing_jump,
        by_symbol.get("over_warn"), by_symbol.get("over_alert"), cfg)

    level_ref = _level_stats(ref)
    level_cur = _level_stats(cur)
    level_z = None
    if (level_ref["std"] not in (None, 0.0) and level_ref["mean"] is not None
            and level_cur["mean"] is not None):
        level_z = (level_cur["mean"] - level_ref["mean"]) / level_ref["std"]

    return {
        "verdict": verdict,
        "reasons": reasons,
        "psi": psi_res["psi"],
        "ks": ks_res["D"],
        "ks_p": ks_res["p"],
        "n_ref": int(len(ref_values)),
        "n_cur": int(len(cur_values)),
        "binning_used": psi_res["binning_used"],
        "n_bins": psi_res["n_bins"],
        "n_clipped": psi_res["n_clipped"],
        "bins": psi_res["bins"],
        "missing_ref": missing_ref,
        "missing_cur": missing_cur,
        "missing_jump": missing_jump,
        "level_ref": level_ref,
        "level_cur": level_cur,
        "level_z": level_z,
        "by_symbol": by_symbol,
    }


def drift_scan(feature_data: dict, ref_dates, cur_windows,
               cfg: dict = None, progress=None) -> dict:
    """特征分布漂移检查。

    feature_data: {feature: DataFrame(index=date, columns=symbol)}
    cur_windows : resolve_windows() 的第三个返回值
    cfg         : bins / binning / psi_warn / psi_alert / ks_alpha /
                  min_obs / min_symbol_obs / rolling{enabled,days,step,top}

    返回 {"ref", "config", "features": {name: {...}}, "summary": {...}}。
    ks 的 p 值在全部特征上做 BH 校正后写入 ks_q_bh。
    """
    cfg = dict(cfg or {})
    features = {}
    cfg_roll = dict(cfg.get("rolling") or {})
    rolling_enabled = bool(cfg_roll.get("enabled", True))

    total = len(feature_data)
    for i, (name, x) in enumerate(feature_data.items(), 1):
        x = x.sort_index()
        per_window = {}
        # 静态对比：ref vs 每个当前窗；主判决用第一个（默认 cur）
        for w in cur_windows:
            one = _drift_one(x, ref_dates, w["dates"], cfg)
            per_window[w["label"]] = one

        primary = per_window[cur_windows[0]["label"]] if cur_windows else {}
        entry = {"psi": primary.get("psi"), "ks": primary.get("ks"),
                 "ks_p": primary.get("ks_p"), "verdict": primary.get("verdict"),
                 "reasons": primary.get("reasons", []),
                 "n_ref": primary.get("n_ref"), "n_cur": primary.get("n_cur"),
                 "binning_used": primary.get("binning_used"),
                 "n_bins": primary.get("n_bins"),
                 "n_clipped": primary.get("n_clipped"),
                 "bins": primary.get("bins", []),
                 "missing_ref": primary.get("missing_ref"),
                 "missing_cur": primary.get("missing_cur"),
                 "missing_jump": primary.get("missing_jump"),
                 "level": {"ref": primary.get("level_ref"),
                          "cur": primary.get("level_cur"),
                          "z": primary.get("level_z")},
                 "by_symbol": primary.get("by_symbol", {})}
        if len(cur_windows) > 1:
            entry["by_window"] = per_window
        features[name] = entry

        if progress:
            progress(i, total, name, primary.get("psi"), primary.get("verdict"))

    # KS p 值的 BH 校正（跨特征）
    names = list(features)
    pvals = [features[n].get("ks_p") for n in names]
    if any(p is not None for p in pvals):
        qvals = bh_qvalues([p if p is not None else float("nan") for p in pvals])
        for n, q in zip(names, qvals):
            features[n]["ks_q_bh"] = None if not math.isfinite(q) else float(q)

    # 滚动 PSI：只对静态 PSI 最大的 top-N 特征算，控制 payload
    if rolling_enabled:
        top_n = int(cfg_roll.get("top", 8))
        ranked = sorted(
            [n for n in names if features[n].get("psi") is not None],
            key=lambda n: features[n]["psi"], reverse=True)[:top_n]
        all_dates = pd.DatetimeIndex(sorted(set().union(
            *[set(x.index) for x in feature_data.values()]))) if feature_data else pd.DatetimeIndex([])
        for n in ranked:
            ref_values = _finite_flat(feature_data[n].loc[ref_dates].to_numpy(dtype=float))
            features[n]["rolling"] = _rolling_psi(
                feature_data[n], ref_values, ref_dates, all_dates, cfg_roll)

    counts = {"alert": 0, "monitor": 0, "stable": 0, "insufficient": 0}
    worst = None
    for n, e in features.items():
        v = e.get("verdict")
        if v in counts:
            counts[v] += 1
        if e.get("psi") is not None and (worst is None or e["psi"] > worst["psi"]):
            worst = {"name": n, "psi": e["psi"], "verdict": v}

    return {
        "ref": None,  # 由调用方填入 ref_info
        "config": {"bins": cfg.get("bins", 10),
                   "binning": cfg.get("binning", "auto"),
                   "psi_warn": cfg.get("psi_warn", 0.10),
                   "psi_alert": cfg.get("psi_alert", 0.25),
                   "ks_alpha": cfg.get("ks_alpha", 0.01),
                   "min_obs": cfg.get("min_obs", 200)},
        "features": features,
        "summary": {"n_features": len(features), **counts, "worst": worst},
    }


# ============================================================
# 2) 分块稳定性
# ============================================================

def _block_verdict(blocks, overall_ic, cfg) -> tuple:
    min_days = int(cfg.get("min_block_days", 20))
    min_sign_frac = float(cfg.get("min_sign_frac", 0.7))
    min_ic = float(cfg.get("min_ic", 0.01))
    decay_trend = float(cfg.get("decay_trend", -0.5))
    decay_ratio_thr = float(cfg.get("decay_ratio", 0.5))

    if not blocks or any(b["days"] < min_days for b in blocks):
        return "insufficient", {"reason": "block_days_below_min"}

    ics = np.asarray([b["ic"] for b in blocks if b["ic"] is not None], dtype=float)
    if len(ics) < 2:
        return "insufficient", {"reason": "less_than_two_blocks"}

    sign = 1.0 if overall_ic >= 0 else -1.0
    consistency = int(np.sum(np.sign(ics) == sign))
    need = int(math.ceil(min_sign_frac * len(blocks)))
    half = max(1, len(ics) // 2)
    first = float(np.mean(ics[:half]))
    second = float(np.mean(ics[half:]))
    decay_ratio = (second / first) if abs(first) > 1e-12 else None
    trend = spearman_ic(np.arange(1, len(ics) + 1), ics)

    basis = {"sign_consistency": consistency, "need": need,
             "trend": trend, "first_half_ic": first,
             "second_half_ic": second, "decay_ratio": decay_ratio}

    if consistency < need or abs(overall_ic) < min_ic:
        return "unstable", basis
    if (trend is not None and math.isfinite(trend) and trend <= decay_trend
            and decay_ratio is not None and decay_ratio < decay_ratio_thr):
        return "decaying", basis
    return "stable", basis


def block_stability(signal: pd.DataFrame, fwd: pd.DataFrame, mask,
                    dates, cfg: dict = None, progress=None) -> dict:
    """连续分块稳定性（非 ML 的 walk-forward 等价物）。

    signal/fwd : (T, N) 宽表
    mask       : (T, N) 布尔或 None
    dates      : 参与分析的日期（DatetimeIndex，升序）
    cfg        : n_blocks / gap / min_block_days / min_names / nw_lag /
                 topk / min_sign_frac / min_ic / decay_trend / decay_ratio
    """
    cfg = dict(cfg or {})
    n_blocks = int(cfg.get("n_blocks", 5))
    gap, gap_raised = effective_gap(int(cfg.get("gap", 0)),
                                    int(cfg.get("horizon", 5)))
    min_names = int(cfg.get("min_names", 15))
    nw_lag = int(cfg.get("nw_lag", 0)) or 0
    topk = [int(k) for k in (cfg.get("topk") or [3, 5])]
    min_days = int(cfg.get("min_block_days", 20))

    dates = pd.DatetimeIndex(dates).sort_values()
    dates = dates.intersection(signal.index)
    x_all = signal.loc[dates]
    y_all = fwd.reindex(index=dates)
    m_all = mask.reindex(index=dates) if mask is not None else None

    def _ic_series(x, y, m):
        xa = x.to_numpy(dtype=float)
        ya = y.to_numpy(dtype=float)
        ma = m.to_numpy(dtype=bool) if m is not None else None
        ic, _ = daily_cs_ic_wide(xa, ya, ma, min_names)
        return pd.Series(ic, index=x.index).dropna()

    overall_ic_s = _ic_series(x_all, y_all, m_all)
    overall_ic = float(overall_ic_s.mean()) if len(overall_ic_s) else float("nan")
    overall_t, _ = newey_west_t(overall_ic_s, nw_lag or max(10, 2 * int(cfg.get("horizon", 5))))
    sign = 1.0 if (not math.isfinite(overall_ic) or overall_ic >= 0) else -1.0

    bounds = block_bounds(len(dates), n_blocks, gap)
    if not bounds:
        return {"verdict": "insufficient", "blocks": [],
                "sign_consistency": 0, "n_blocks": n_blocks,
                "gap": int(gap), "gap_raised": bool(gap_raised),
                "ic_mean": overall_ic, "ic_std": None, "overall_t": overall_t,
                "trend": None, "first_half_ic": None, "second_half_ic": None,
                "decay_ratio": None, "worst_block_ic": None,
                "best_block_ic": None, "verdict_basis": {"reason": "no_bounds"}}

    blocks = []
    for i, (start, stop) in enumerate(bounds):
        b_dates = dates[start:stop]
        xb = x_all.loc[b_dates]
        yb = y_all.loc[b_dates]
        mb = m_all.loc[b_dates] if m_all is not None else None
        ic_s = _ic_series(xb, yb, mb)
        b_ic = float(ic_s.mean()) if len(ic_s) else None
        b_t, _ = newey_west_t(ic_s, nw_lag or max(10, 2 * int(cfg.get("horizon", 5))))
        b_hit = float((ic_s > 0).mean()) if len(ic_s) else None
        b_topk = {}
        if b_ic is not None and math.isfinite(b_ic):
            rows = np.arange(len(b_dates))
            xa = xb.to_numpy(dtype=float)
            ya = yb.to_numpy(dtype=float)
            ma = mb.to_numpy(dtype=bool) if mb is not None else None
            for k in topk:
                b_topk[str(k)] = topk_excess(xa, ya, ma, sign, k,
                                             min_names, nw_lag, dates=rows)
        blocks.append({
            "index": i,
            "start": str(b_dates[0].date()) if len(b_dates) else "",
            "end": str(b_dates[-1].date()) if len(b_dates) else "",
            "days": int(len(b_dates)),
            "ic_days": int(len(ic_s)),
            "coverage": float(len(ic_s) / len(b_dates)) if len(b_dates) else None,
            "ic": b_ic,
            "nw_t": b_t if math.isfinite(b_t) else None,
            "hit": b_hit,
            "topk": b_topk,
        })
        if progress:
            progress(i + 1, len(bounds))

    ics = [b["ic"] for b in blocks if b["ic"] is not None]
    arr = np.asarray(ics, dtype=float)
    verdict, basis = _block_verdict(blocks, overall_ic, cfg)

    return {
        "verdict": verdict,
        "verdict_basis": basis,
        "n_blocks": int(n_blocks),
        "gap": int(gap),
        "gap_raised": bool(gap_raised),
        "blocks": blocks,
        "sign_consistency": int(basis.get("sign_consistency", 0)),
        "ic_mean": overall_ic,
        "ic_std": float(np.std(arr, ddof=1)) if len(arr) > 1 else None,
        "overall_t": overall_t if math.isfinite(overall_t) else None,
        "trend": basis.get("trend"),
        "first_half_ic": basis.get("first_half_ic"),
        "second_half_ic": basis.get("second_half_ic"),
        "decay_ratio": basis.get("decay_ratio"),
        "worst_block_ic": float(arr.min()) if len(arr) else None,
        "best_block_ic": float(arr.max()) if len(arr) else None,
    }


# ============================================================
# 3) 信号条件前瞻收益曲线
# ============================================================

def _bucket_mode(signal: pd.DataFrame, min_names: int) -> str:
    """逐日唯一值数 ≈1 且标的数 >1 → time_series，否则 cross_section。

    单标的场景（n_symbols=1）：无法做横截面排名，必须用 time_series 模式。
    """
    n_symbols = signal.shape[1]
    if n_symbols < 2:
        return "time_series"

    valid = np.isfinite(signal.to_numpy(dtype=float))
    n_per_day = valid.sum(axis=1)
    uniq = np.array([
        len(np.unique(signal.to_numpy(dtype=float)[i][valid[i]])) if n_per_day[i] else 0
        for i in range(signal.shape[0])
    ])
    ok = n_per_day >= max(2, min_names // 2)
    if not ok.any():
        return "cross_section"
    return "time_series" if float(np.median(uniq[ok])) <= 1.0 else "cross_section"


def _bucket_daily_means(x: np.ndarray, y: np.ndarray, valid: np.ndarray,
                        bucket: np.ndarray, q: int,
                        min_bucket_names: int) -> np.ndarray:
    """按日聚合每个桶的 mean forward return → (T, q)，样本不足为 NaN。"""
    t = x.shape[0]
    out = np.full((t, q), np.nan)
    f = np.where(valid, y, np.nan)
    for j in range(q):
        sel = valid & (bucket == j)
        cnt = sel.sum(axis=1)
        with np.errstate(invalid="ignore"):
            mean = np.nanmean(np.where(sel, f, np.nan), axis=1)
        mean = np.where(cnt >= min_bucket_names, mean, np.nan)
        out[:, j] = mean
    return out


def _curve_shape(bucket_means, spread_mean, spread_t, min_days) -> tuple:
    finite = [m for m in bucket_means if m is not None]
    if len(finite) < 3 or spread_t is None or not math.isfinite(spread_t):
        return "insufficient", None
    q = len(finite)
    rho = spearman_ic(np.arange(1, q + 1), np.asarray(finite, dtype=float))
    if rho is not None and math.isfinite(rho) and abs(rho) >= 0.8 and abs(spread_t) >= 2.0:
        return ("monotone_up" if spread_mean > 0 else "monotone_down"), rho
    if abs(spread_t) >= 2.0:
        return "threshold", rho
    return "weak", rho


def signal_curve(signal: pd.DataFrame, fwd_by_h: dict, mask_by_h: dict | None,
                 dates, cfg: dict = None, progress=None) -> dict:
    """信号条件前瞻收益曲线。

    signal   : (T, N) 宽表
    fwd_by_h : {horizon: (T, N) 宽表}
    mask_by_h: {horizon: (T, N) 布尔} 或 None
    dates    : 参与分析的日期
    cfg      : quantiles / bucket_mode(auto|cross_section|time_series) /
               min_bucket_names / min_names / nw_lag / primary_horizon
    """
    cfg = dict(cfg or {})
    q = int(cfg.get("quantiles", 5))
    if q < 2:
        raise SystemExit("signal.quantiles 必须 >= 2")
    min_bucket = int(cfg.get("min_bucket_names", 5))
    min_names = int(cfg.get("min_names", 15))
    nw_lag = int(cfg.get("nw_lag", 0))
    primary = int(cfg.get("primary_horizon", 5))

    dates = pd.DatetimeIndex(dates).sort_values().intersection(signal.index)
    x = signal.loc[dates].to_numpy(dtype=float)
    mode = str(cfg.get("bucket_mode", "auto"))
    if mode == "auto":
        mode = _bucket_mode(signal.loc[dates], min_names)

    valid_sig = np.isfinite(x)
    if mode == "cross_section":
        # 逐日横截面排名分桶：bucket = ceil(rank / n_valid * q) - 1
        ranks = row_ranks(np.where(valid_sig, x, np.nan))
        n_valid = valid_sig.sum(axis=1)[:, None]
        with np.errstate(divide="ignore", invalid="ignore"):
            bucket = np.ceil(ranks / np.maximum(n_valid, 1) * q) - 1.0
        bucket = np.clip(bucket, 0, q - 1)
        bucket = np.where(valid_sig, bucket, np.nan)
    else:
        # 时序分桶：全样本信号分位边界，按值分档
        pool = x[valid_sig]
        if len(pool) < q * 2:
            return {"bucket_mode": mode, "quantiles": q,
                    "verdict": "insufficient", "buckets": [],
                    "spread": {}, "monotonicity": {}, "curve_shape": "insufficient",
                    "ic": {}}
        edges = np.unique(np.percentile(pool, np.linspace(0, 100, q + 1)))
        if len(edges) < 3:
            return {"bucket_mode": mode, "quantiles": q,
                    "verdict": "insufficient", "buckets": [],
                    "spread": {}, "monotonicity": {}, "curve_shape": "insufficient",
                    "ic": {}}
        bucket = np.digitize(x, edges[1:-1]).astype(float)  # 0..len(edges)-2
        q = len(edges) - 1
        bucket = np.where(valid_sig, bucket, np.nan)

    horizons = sorted(int(h) for h in fwd_by_h)
    bucket_daily = {}   # h -> (T, q)
    bucket_stats = {}   # h -> [ {mean,t,hit,days}, ... ]
    spread_stats = {}   # h -> {mean,t,hit,days}
    ic_stats = {}       # h -> {mean,t,days} 仅 cross_section

    for hi, h in enumerate(horizons, 1):
        y = fwd_by_h[h].reindex(index=dates).to_numpy(dtype=float)
        m = None
        if mask_by_h and h in mask_by_h and mask_by_h[h] is not None:
            m = mask_by_h[h].reindex(index=dates).to_numpy(dtype=bool)
        valid = np.isfinite(x) & np.isfinite(y)
        if m is not None:
            valid &= m

        bd = _bucket_daily_means(x, y, valid, bucket, q, min_bucket)
        bucket_daily[h] = bd

        stats = []
        for j in range(q):
            s = pd.Series(bd[:, j]).dropna()
            if len(s) < 10:
                stats.append({"bucket": j + 1, "mean": None, "t": None,
                              "hit": None, "days": int(len(s))})
                continue
            t, _ = newey_west_t(s, nw_lag or max(10, 2 * h))
            stats.append({"bucket": j + 1,
                          "mean": float(s.mean()),
                          "t": t if math.isfinite(t) else None,
                          "hit": float((s > 0).mean()),
                          "days": int(len(s))})
        bucket_stats[h] = stats

        sp = pd.Series(bd[:, -1] - bd[:, 0]).dropna()
        if len(sp) >= 10:
            t, _ = newey_west_t(sp, nw_lag or max(10, 2 * h))
            spread_stats[h] = {"mean": float(sp.mean()),
                               "t": t if math.isfinite(t) else None,
                               "hit": float((sp > 0).mean()),
                               "days": int(len(sp))}
        else:
            spread_stats[h] = {"mean": None, "t": None, "hit": None,
                               "days": int(len(sp))}

        if mode == "cross_section":
            ma = m if m is not None else None
            ic_arr, _ = daily_cs_ic_wide(x, y, ma, min_names)
            ic_s = pd.Series(ic_arr).dropna()
            if len(ic_s) >= 10:
                t, _ = newey_west_t(ic_s, nw_lag or max(10, 2 * h))
                ic_stats[h] = {"mean": float(ic_s.mean()),
                               "t": t if math.isfinite(t) else None,
                               "days": int(len(ic_s))}
            else:
                ic_stats[h] = {"mean": None, "t": None, "days": int(len(ic_s))}

        if progress:
            progress(hi, len(horizons), h)

    finite_means = [s["mean"] for s in bucket_stats.get(primary, [])]
    spread_primary = spread_stats.get(primary, {})
    shape, rho = _curve_shape(finite_means, spread_primary.get("mean"),
                              spread_primary.get("t"), min_bucket)

    buckets = []
    for j in range(q):
        buckets.append({
            "bucket": j + 1,
            "label": f"Q{j + 1}",
            "by_horizon": {str(h): bucket_stats[h][j] for h in horizons},
        })

    # 主 horizon 的凸性判定
    if shape == "insufficient":
        verdict = "insufficient"
    elif shape == "weak":
        verdict = "weak"
    elif shape.startswith("monotone"):
        verdict = "monotone"
    else:
        verdict = "threshold"

    return {
        "bucket_mode": mode,
        "quantiles": int(q),
        "primary_horizon": primary,
        "buckets": buckets,
        "spread": {str(h): spread_stats[h] for h in horizons},
        "monotonicity": {str(h): {"spearman": _curve_shape(
            [s["mean"] for s in bucket_stats[h]], spread_stats[h].get("mean"),
            spread_stats[h].get("t"), min_bucket)[1],
            "n_up_steps": sum(1 for a, b in zip(
                [s["mean"] for s in bucket_stats[h]],
                [s["mean"] for s in bucket_stats[h]][1:])
                if a is not None and b is not None and b > a)}
            for h in horizons},
        "curve_shape": shape,
        "ic": {str(h): ic_stats.get(h) for h in horizons} if mode == "cross_section" else {},
        "verdict": verdict,
    }
