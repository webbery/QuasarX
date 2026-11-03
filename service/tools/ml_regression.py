#!/usr/bin/env python3
"""通用金融回归分析 —— Python 侧参考实现与 CLI。

与 C++ 内核 `service/src/Algorithms/regression.cpp` 是同一套口径的两个实现：

  - C++ 侧服务在线路径（图内节点 / REST 接口）
  - 本文件服务训练/研究侧（批量面板、评估）
  - 两者靠 service/test/testcases/test_regression_parity.py 通过 REST 接口对拍

spec 是两侧共享的契约（JSON）：

    {
      "mode": "cross_section" | "time_series" | "panel",
      "dates":   ["2024-01-02", ...],            // T
      "symbols": ["sh.600001", ...],             // N
      "y":       [[...], ...],                   // T×N，null = 缺测
      "x":       {"size": [[...], ...]},          // 截面因子：T×N
      "f":       {"mkt": [...]},                  // 时序因子：T
      "groups":  {"industry": ["C39", ...]},      // 每个标的的组标签
      "xnames":  ["size"],
      "fnames":  ["mkt"],
      "groupnames": ["industry"],
      "se": "iid" | "hac" | "cluster",
      "options": {
        "min_obs": 10, "min_group_size": 2, "hac_lag": 20,
        "add_const": true, "winsor_q": 0.0, "ridge_lambda": 0.0,
        "report_vif": true, "time_ordered": false, "fallback_group": "OTHER"
      }
    }

输出 schema 与 C++ 侧一致（见 run() 的返回值）。

用法:
  python ml_regression.py --spec spec.json --out result.json
  python ml_regression.py --self-test
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from ml_stats import (  # noqa: E402
    _is_constant,
    _solve_design,
    _sample_std,
    build_group_codes,
    greedy_rank_select,
    ols_fit,
    partial_residual,
    series_mean_test,
    student_t_two_sided_p,
    winsorize_cross_section,
)


# ============================================================
# spec 解析
# ============================================================

def _parse_matrix(obj, T: int, N: int) -> np.ndarray:
    m = np.full((T, N), np.nan)
    if not isinstance(obj, list):
        return m
    for i in range(min(T, len(obj))):
        row = obj[i]
        if not isinstance(row, list):
            continue
        for j in range(min(N, len(row))):
            v = row[j]
            if v is not None:
                m[i, j] = float(v)
    return m


def _parse_vector(obj, T: int) -> np.ndarray:
    v = np.full(T, np.nan)
    if not isinstance(obj, list):
        return v
    for i in range(min(T, len(obj))):
        if obj[i] is not None:
            v[i] = float(obj[i])
    return v


def _parse_options(raw) -> dict:
    o = raw or {}
    return {
        "min_obs": int(o.get("min_obs", 10)),
        "min_group_size": int(o.get("min_group_size", 2)),
        "hac_lag": int(o.get("hac_lag", 20)),
        "add_const": bool(o.get("add_const", True)),
        "winsor_q": float(o.get("winsor_q", 0.0)),
        "ridge_lambda": float(o.get("ridge_lambda", 0.0)),
        "report_vif": bool(o.get("report_vif", True)),
        "time_ordered": bool(o.get("time_ordered", False)),
        "fallback_group": str(o.get("fallback_group", "OTHER")),
    }


def _json_safe(obj):
    """NaN/Inf → None；numpy 标量 → Python 标量。"""
    if isinstance(obj, dict):
        return {k: _json_safe(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_json_safe(v) for v in obj]
    if isinstance(obj, (np.floating, float)):
        v = float(obj)
        return v if math.isfinite(v) else None
    if isinstance(obj, np.integer):
        return int(obj)
    if isinstance(obj, np.bool_):
        return bool(obj)
    return obj


# ============================================================
# 横截面回归
# ============================================================

def cross_section(panel: dict, xnames, fnames, groupnames,
                  se: str, opt: dict) -> dict:
    dates = panel["dates"]
    symbols = panel["symbols"]
    Y = panel["y"]
    T, N = len(dates), len(symbols)

    if T == 0 or N == 0 or Y.shape != (T, N):
        return {"mode": "cross_section", "dates": dates, "symbols": symbols,
                "terms": [], "vif": [], "cond": 0.0,
                "warnings": ["面板维度不一致"], "beta": [], "resid": [],
                "r2": [], "n_names": [], "dropped": []}

    # se 降级提示：逐日横截面回归没有聚类维度，cluster 退化为 iid。
    # 与 C++ regression.cpp::crossSection 逐字对称。
    warnings = []
    if se == "cluster":
        warnings.append("cross_section 模式不支持 cluster 标准误，已退化为 iid")

    # ── 列定义 ──
    cols = []   # (kind, name, idx, level)
    x_panels = []
    f_series = []
    group_defs = []

    if opt["add_const"]:
        cols.append(("const", "const", -1, -1))

    for xn in xnames:
        if xn not in panel["x"] or panel["x"][xn].shape != (T, N):
            warnings.append(f"截面因子缺失或维度不符: {xn}")
            continue
        cols.append(("x", xn, len(x_panels), -1))
        x_panels.append(panel["x"][xn])

    for fn in fnames:
        if fn not in panel["f"] or len(panel["f"][fn]) != T:
            warnings.append(f"时序因子缺失或维度不符: {fn}")
            continue
        cols.append(("f", fn, len(f_series), -1))
        f_series.append(panel["f"][fn])

    for gn in groupnames:
        labels = panel["groups"].get(gn)
        if labels is None or len(labels) != N:
            warnings.append(f"分组变量缺失或维度不符: {gn}")
            continue
        codes, levels = build_group_codes(labels, opt["min_group_size"],
                                          opt["fallback_group"])
        if len(levels) < 2:
            warnings.append(f"分组变量有效水平不足: {gn}")
            continue
        gidx = len(group_defs)
        group_defs.append((codes, levels))
        start = 1 if opt["add_const"] else 0
        for l in range(start, len(levels)):
            cols.append(("dummy", f"{gn}[{levels[l]}]", gidx, l))

    K = len(cols)
    names = [c[1] for c in cols]

    def valid_cell(t: int, i: int) -> bool:
        if not np.isfinite(Y[t, i]):
            return False
        for m in x_panels:
            if not np.isfinite(m[t, i]):
                return False
        for v in f_series:
            if not np.isfinite(v[t]):
                return False
        return True

    def fill_row(t: int, i: int) -> np.ndarray:
        row = np.empty(K)
        for j, (kind, _name, idx, level) in enumerate(cols):
            if kind == "const":
                row[j] = 1.0
            elif kind == "x":
                row[j] = x_panels[idx][t, i]
            elif kind == "f":
                row[j] = f_series[idx][t]
            else:
                row[j] = 1.0 if group_defs[idx][0][i] == level else 0.0
        return row

    # ── 全样本秩判定 ──
    gt, gi = [], []
    for t in range(T):
        for i in range(N):
            if valid_cell(t, i):
                gt.append(t)
                gi.append(i)

    global_drop = np.zeros(K, dtype=bool)
    global_kept = np.zeros((0, 0))
    if gt:
        Ag = np.vstack([fill_row(t, i) for t, i in zip(gt, gi)])
        gk, gd = greedy_rank_select(Ag)
        for j in gd:
            global_drop[j] = True
            warnings.append(f"全样本剔除共线列: {names[j]}")
        global_kept = Ag[:, gk] if gk else np.zeros((Ag.shape[0], 0))
    else:
        warnings.append("全样本无有效观测")
        global_drop[:] = True

    # ── 逐日回归 ──
    beta = np.full((T, K), np.nan)
    resid = np.full((T, N), np.nan)
    r2 = np.full(T, np.nan)
    n_names = np.zeros(T, dtype=int)
    dropped_mat = np.zeros((T, K), dtype=int)

    force = global_drop.astype(bool)
    for t in range(T):
        idx = [i for i in range(N) if valid_cell(t, i)]
        if len(idx) < opt["min_obs"]:
            warnings.append(f"日期 {dates[t]} 有效标的不足，已跳过")
            continue

        A = np.vstack([fill_row(t, i) for i in idx])
        b = Y[t, idx].astype(float)
        if opt["winsor_q"] > 0.0:
            b = winsorize_cross_section(b, opt["winsor_q"])

        s = _solve_design(A, b, opt["ridge_lambda"], force)
        n_names[t] = len(idx)
        for j in s["kept"]:
            beta[t, j] = s["beta"][j]
        for j in s["dropped"]:
            dropped_mat[t, j] = 1
        resid[t, idx] = s["resid"]

        sse = float(np.sum(s["resid"] ** 2))
        sst = float(np.sum((b - b.mean()) ** 2))
        r2[t] = 1.0 - sse / sst if sst > 1e-15 else np.nan

    # ── 汇总项 ──
    terms = []
    for j in range(K):
        if global_drop[j]:
            terms.append({"name": names[j], "beta": None, "se": None, "t": None,
                          "p": None, "std_beta": 0.0, "dropped": True,
                          "drop_reason": "collinear_global"})
            continue
        mt = series_mean_test(beta[:, j], se=se, hac_lag=opt["hac_lag"])
        terms.append({"name": names[j], "beta": mt["mean"], "se": mt["se"],
                      "t": mt["t"], "p": mt["p"], "std_beta": 0.0,
                      "dropped": False, "drop_reason": ""})

    # ── VIF / 条件数（全样本口径） ──
    from ml_stats import _compute_cond, _compute_vif
    cond = _compute_cond(global_kept) if global_kept.size else 0.0
    vif = [0.0] * K
    if opt["report_vif"] and global_kept.size:
        vif_kept = _compute_vif(global_kept)
        m = 0
        for j in range(K):
            if not global_drop[j]:
                vif[j] = float(vif_kept[m])
                m += 1

    return {
        "mode": "cross_section",
        "dates": dates,
        "symbols": symbols,
        "terms": terms,
        "vif": vif,
        "cond": cond,
        "warnings": warnings,
        "beta": beta.tolist(),
        "resid": resid.tolist(),
        "r2": r2.tolist(),
        "n_names": n_names.tolist(),
        "dropped": dropped_mat.tolist(),
    }


# ============================================================
# 时序回归
# ============================================================

def time_series(panel: dict, xnames, fnames, se: str, opt: dict) -> dict:
    dates = panel["dates"]
    symbols = panel["symbols"]
    Y = panel["y"]
    T, N = len(dates), len(symbols)

    if T == 0 or N == 0 or Y.shape != (T, N):
        return {"mode": "time_series", "dates": dates, "symbols": symbols,
                "terms": [], "vif": [], "cond": 0.0,
                "warnings": ["面板维度不一致"], "beta": [], "se": [], "t": [],
                "r2": [], "n_obs": [], "dropped": []}

    # se 降级提示：逐标的回归没有聚类 id，cluster 退化为 iid。
    # 与 C++ regression.cpp::timeSeries 逐字对称。
    warnings = []
    if se == "cluster":
        warnings.append("time_series 模式不支持 cluster 标准误，已退化为 iid")

    names = []
    x_panels = []
    f_series = []

    if opt["add_const"]:
        names.append("const")
    for xn in xnames:
        if xn not in panel["x"] or panel["x"][xn].shape != (T, N):
            warnings.append(f"截面因子缺失或维度不符: {xn}")
            continue
        names.append(xn)
        x_panels.append(panel["x"][xn])
    for fn in fnames:
        if fn not in panel["f"] or len(panel["f"][fn]) != T:
            warnings.append(f"时序因子缺失或维度不符: {fn}")
            continue
        names.append(fn)
        f_series.append(panel["f"][fn])

    K = len(names)
    if K == 0:
        return {"mode": "time_series", "dates": dates, "symbols": symbols,
                "terms": [], "vif": [], "cond": 0.0,
                "warnings": ["没有可用回归项"], "beta": [], "se": [], "t": [],
                "r2": [], "n_obs": [], "dropped": []}

    beta = np.full((N, K), np.nan)
    se_mat = np.full((N, K), np.nan)
    t_mat = np.full((N, K), np.nan)
    r2 = np.full(N, np.nan)
    n_obs = np.zeros(N, dtype=int)
    dropped_mat = np.zeros((N, K), dtype=int)
    beta_series = [[] for _ in range(K)]

    for i in range(N):
        rows = []
        for t in range(T):
            if not np.isfinite(Y[t, i]):
                continue
            if any(not np.isfinite(m[t, i]) for m in x_panels):
                continue
            if any(not np.isfinite(v[t]) for v in f_series):
                continue
            rows.append(t)

        n_obs[i] = len(rows)
        if len(rows) < opt["min_obs"]:
            warnings.append(f"标的 {symbols[i]} 有效样本不足，已跳过")
            continue

        cols = []
        if opt["add_const"]:
            cols.append(np.ones(len(rows)))
        for m in x_panels:
            cols.append(np.array([m[t, i] for t in rows]))
        for v in f_series:
            cols.append(np.array([v[t] for t in rows]))
        A = np.column_stack(cols)
        b = np.array([Y[t, i] for t in rows])

        s = _solve_design(A, b, opt["ridge_lambda"])
        sse = float(np.sum(s["resid"] ** 2))
        df = len(rows) - s["rank"]
        sigma2 = sse / df if df > 0 else np.nan
        sst = float(np.sum((b - b.mean()) ** 2))
        r2[i] = 1.0 - sse / sst if sst > 1e-15 else np.nan

        if se == "hac":
            from ml_stats import _cov_hac
            V = _cov_hac(s["Akept"], s["resid"], s["XtXinv"], opt["hac_lag"])
        else:
            V = s["XtXinv"] * sigma2

        for m, j in enumerate(s["kept"]):
            beta[i, j] = s["beta"][j]
            se_mat[i, j] = math.sqrt(max(0.0, float(V[m, m])))
            t_mat[i, j] = (beta[i, j] / se_mat[i, j]
                           if se_mat[i, j] > 0 else np.nan)
            if np.isfinite(beta[i, j]):
                beta_series[j].append(float(beta[i, j]))
        for j in s["dropped"]:
            dropped_mat[i, j] = 1

    terms = []
    for j in range(K):
        mt = series_mean_test(beta_series[j], se=se, hac_lag=opt["hac_lag"])
        terms.append({"name": names[j], "beta": mt["mean"], "se": mt["se"],
                      "t": mt["t"], "p": mt["p"], "std_beta": 0.0,
                      "dropped": False, "drop_reason": ""})

    # 全样本设计矩阵 → VIF / 条件数
    gt, gi = [], []
    for t in range(T):
        for i in range(N):
            if not np.isfinite(Y[t, i]):
                continue
            if any(not np.isfinite(m[t, i]) for m in x_panels):
                continue
            if any(not np.isfinite(v[t]) for v in f_series):
                continue
            gt.append(t)
            gi.append(i)

    from ml_stats import _compute_cond, _compute_vif
    cond = 0.0
    vif = [0.0] * K
    if gt:
        cols = []
        if opt["add_const"]:
            cols.append(np.ones(len(gt)))
        for m in x_panels:
            cols.append(np.array([m[t, i] for t, i in zip(gt, gi)]))
        for v in f_series:
            cols.append(np.array([v[t] for t in gt]))
        Ag = np.column_stack(cols)
        cond = _compute_cond(Ag)
        if opt["report_vif"]:
            vif = [float(x) for x in _compute_vif(Ag)]

    return {
        "mode": "time_series",
        "dates": dates,
        "symbols": symbols,
        "terms": terms,
        "vif": vif,
        "cond": cond,
        "warnings": warnings,
        "beta": beta.tolist(),
        "se": se_mat.tolist(),
        "t": t_mat.tolist(),
        "r2": r2.tolist(),
        "n_obs": n_obs.tolist(),
        "dropped": dropped_mat.tolist(),
    }


# ============================================================
# 面板回归（双向固定效应）
# ============================================================

def panel_regression(panel: dict, xnames, se: str, opt: dict) -> dict:
    dates = panel["dates"]
    symbols = panel["symbols"]
    Y = panel["y"]
    T, N = len(dates), len(symbols)

    names = []
    x_panels = []
    # se 降级提示：面板回归只实现了按日期聚类，hac 退化为 iid。
    # 与 C++ regression.cpp::panel 逐字对称。
    warnings = []
    if se == "hac":
        warnings.append("panel 模式不支持 hac 标准误，已退化为 iid")
    for xn in xnames:
        if xn not in panel["x"] or panel["x"][xn].shape != (T, N):
            warnings.append(f"截面因子缺失或维度不符: {xn}")
            continue
        names.append(xn)
        x_panels.append(panel["x"][xn])

    K = len(names)
    if K == 0:
        return {"mode": "panel", "dates": dates, "symbols": symbols,
                "terms": [], "vif": [], "cond": 0.0,
                "warnings": ["没有可用回归项"], "r2": None, "adj_r2": None,
                "n_obs": 0}

    rt, ri = [], []
    for t in range(T):
        for i in range(N):
            if not np.isfinite(Y[t, i]):
                continue
            if any(not np.isfinite(m[t, i]) for m in x_panels):
                continue
            rt.append(t)
            ri.append(i)

    n = len(rt)
    if n < opt["min_obs"]:
        return {"mode": "panel", "dates": dates, "symbols": symbols,
                "terms": [], "vif": [], "cond": 0.0,
                "warnings": ["有效样本不足"], "r2": None, "adj_r2": None,
                "n_obs": n}

    y = np.array([Y[t, i] for t, i in zip(rt, ri)])
    X = np.column_stack([[m[t, i] for t, i in zip(rt, ri)] for m in x_panels])
    date_code = np.array(rt)
    symbol_code = np.array(ri)

    from ml_stats import alternate_demean
    y, X = alternate_demean(y, X, [date_code, symbol_code], [T, N])

    s = _solve_design(X, y, opt["ridge_lambda"])
    sse = float(np.sum(s["resid"] ** 2))
    sst = float(np.sum((y - y.mean()) ** 2))
    r2 = 1.0 - sse / sst if sst > 1e-15 else np.nan
    df = n - s["rank"]
    adj_r2 = (1.0 - (1.0 - r2) * (n - 1.0) / df
              if df > 0 and np.isfinite(r2) else np.nan)
    sigma2 = sse / df if df > 0 else np.nan

    if se == "cluster":
        from ml_stats import _cov_cluster
        V = _cov_cluster(s["Akept"], s["resid"], s["XtXinv"], date_code, s["rank"])
    else:
        V = s["XtXinv"] * sigma2

    terms = [{"name": names[j], "beta": None, "se": None, "t": None, "p": None,
              "std_beta": 0.0, "dropped": True, "drop_reason": "collinear"}
             for j in range(K)]
    for m, j in enumerate(s["kept"]):
        b_j = float(s["beta"][j])
        se_j = math.sqrt(max(0.0, float(V[m, m])))
        t_j = b_j / se_j if se_j > 0 else np.nan
        terms[j] = {"name": names[j], "beta": b_j, "se": se_j, "t": t_j,
                    "p": student_t_two_sided_p(abs(t_j), float(df)),
                    "std_beta": 0.0, "dropped": False, "drop_reason": ""}

    return {
        "mode": "panel",
        "dates": dates,
        "symbols": symbols,
        "terms": terms,
        "vif": [0.0] * K,
        "cond": 0.0,
        "warnings": warnings,
        "r2": r2,
        "adj_r2": adj_r2,
        "n_obs": n,
    }


# ============================================================
# 入口
# ============================================================

def run(spec: dict) -> dict:
    """执行一份 spec，返回与 C++ 侧同 schema 的结果。"""
    mode = str(spec.get("mode", "cross_section"))
    dates = [str(x) for x in spec.get("dates", [])]
    symbols = [str(x) for x in spec.get("symbols", [])]
    T, N = len(dates), len(symbols)

    raw_x = spec.get("x") or {}
    raw_f = spec.get("f") or {}
    panel = {
        "dates": dates,
        "symbols": symbols,
        "y": _parse_matrix(spec.get("y"), T, N),
        "x": {k: _parse_matrix(v, T, N) for k, v in raw_x.items()},
        "f": {k: _parse_vector(v, T) for k, v in raw_f.items()},
        "groups": {k: [str(x) for x in v] for k, v in (spec.get("groups") or {}).items()},
    }

    xnames = [str(x) for x in spec.get("xnames", [])]
    fnames = [str(x) for x in spec.get("fnames", [])]
    groupnames = [str(x) for x in spec.get("groupnames", [])]
    se = str(spec.get("se", "iid"))
    opt = _parse_options(spec.get("options"))

    if mode == "cross_section":
        return cross_section(panel, xnames, fnames, groupnames, se, opt)
    if mode == "time_series":
        return time_series(panel, xnames, fnames, se, opt)
    if mode == "panel":
        return panel_regression(panel, xnames, se, opt)
    raise ValueError(f"未知 mode: {mode}")


def _self_test() -> int:
    """Python 侧自检（C++ 侧待 REST 接口就绪后由 parity 测试对拍）。"""
    checks = []

    def check(name, ok, detail=""):
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    # 1. 精确 OLS
    rng = np.random.default_rng(7)
    n = 200
    x = rng.normal(size=n)
    y = 2.0 + 3.0 * x
    X = np.column_stack([np.ones(n), x])
    f = ols_fit(y, X, ["const", "x"])
    check("ols_exact", abs(f["terms"][0]["beta"] - 2.0) < 1e-9
          and abs(f["terms"][1]["beta"] - 3.0) < 1e-9,
          f"{f['terms'][0]['beta']:.6f}, {f['terms'][1]['beta']:.6f}")

    # 2. 重复列剔除
    X2 = np.column_stack([np.ones(n), x, x])
    f2 = ols_fit(y, X2, ["const", "x", "x_dup"])
    check("rank_drop_duplicate", f2["terms"][2]["dropped"] is True,
          f2["terms"][2]["drop_reason"])

    # 3. NaN 行丢弃
    y3 = y.copy()
    X3 = X.copy()
    y3[3] = np.nan
    X3[5, 1] = np.nan
    f3 = ols_fit(y3, X3, ["const", "x"])
    check("nan_listwise", f3["n_dropped_rows"] == 2, str(f3["n_dropped_rows"]))

    # 4. FWL 与哑变量回归等价
    m = 300
    g = np.array(["A", "B", "C"])[rng.integers(0, 3, size=m)]
    eff = {"A": 0.0, "B": 1.0, "C": -0.5}
    xg = rng.normal(size=m)
    yg = 1.0 + np.array([eff[v] for v in g]) + 2.0 * xg + rng.normal(scale=0.1, size=m)
    codes, levels = build_group_codes(g.tolist(), 2, "OTHER")
    r_fwl = partial_residual(yg, xg[:, None], [codes], [len(levels)], True)
    D = np.column_stack([np.ones(m)] +
                        [(np.array(codes) == l).astype(float) for l in range(1, len(levels))] +
                        [xg])
    f4 = ols_fit(yg, D, None)
    r_dummy = yg - D @ np.array([t["beta"] for t in f4["terms"]])
    check("fwl_equals_dummy", float(np.max(np.abs(r_fwl - r_dummy))) < 1e-8,
          f"maxdiff={float(np.max(np.abs(r_fwl - r_dummy))):.2e}")

    # 5. 岭收缩
    beta_ols = ols_fit(y, X, ["const", "x"])["terms"][1]["beta"]
    beta_ridge = ols_fit(y, X, ["const", "x"], ridge_lambda=1000.0)["terms"][1]["beta"]
    check("ridge_shrinks", abs(beta_ridge) < abs(beta_ols), f"{beta_ols:.4f} -> {beta_ridge:.4f}")

    # 6. t 分布 p 值
    p1 = student_t_two_sided_p(2.228, 10.0)
    p2 = student_t_two_sided_p(1.959964, 1e6)
    check("student_t_p", abs(p1 - 0.05) < 1e-3 and abs(p2 - 0.05) < 1e-4,
          f"{p1:.6f}, {p2:.6f}")

    # 7. 小 |t| 的 p 值（补集分支）
    #    I_x(a,b) 在 x >= (a+1)/(a+b+2) 时走 betacf(b, a, 1-x) 补集分支，
    #    对应 |t| <= sqrt(3*df/(df+2))（df 越大阈值越接近 1.7303）。
    #    这两条断言正落在补集分支内：修复前会返回负 p 值。
    p3 = student_t_two_sided_p(0.5, 30.0)
    p4 = student_t_two_sided_p(0.2, 120.0)
    check("student_t_p_small_t", abs(p3 - 0.620723) < 1e-5 and abs(p4 - 0.841819) < 1e-5,
          f"{p3:.6f}, {p4:.6f}")

    # 8. p 值恒在 [0, 1]
    ps = [student_t_two_sided_p(t, df)
          for t in (0.0, 0.05, 0.3, 0.8, 1.2, 1.6, 1.73, 2.0, 3.5, 8.0)
          for df in (3.0, 10.0, 30.0, 60.0, 250.0, 1e6)]
    check("student_t_p_in_unit_interval",
          all(np.isfinite(v) and 0.0 <= v <= 1.0 for v in ps),
          f"min={min(ps):.6f}, max={max(ps):.6f}")

    ok = all(c["ok"] for c in checks)
    print(json.dumps({"ok": ok, "checks": checks}, ensure_ascii=False, indent=2))
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="通用金融回归分析（Python 参考实现）")
    ap.add_argument("--spec", help="spec JSON 路径")
    ap.add_argument("--out", default="", help="结果 JSON 输出路径（缺省打印到 stdout）")
    ap.add_argument("--self-test", action="store_true", help="运行 Python 侧自检")
    args = ap.parse_args()

    if args.self_test:
        return _self_test()

    if not args.spec:
        ap.error("需要 --spec 或 --self-test")

    spec = json.loads(Path(args.spec).read_text(encoding="utf-8"))
    result = _json_safe(run(spec))
    text = json.dumps(result, ensure_ascii=False, allow_nan=False,
                      separators=(",", ":"))

    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
