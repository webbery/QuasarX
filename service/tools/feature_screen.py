#!/usr/bin/env python3
"""统一特征筛选（Step 1）

目标
----
把"某个特征 IC 高不高"从一次性脚本变成统一门禁。所有候选特征走同一套口径：

1) 目标：连续 forward return 的 rank IC（不是三分类标签）
   - 默认 close-to-close；有 OHLCV 时可用 --entry next_open 改成次日开盘入场
   - 可选 --vol-scale 做波动率标准化
2) 两个 IC 口径分开报
   - 日频横截面 IC：同一天内特征对股票的排序能力（top-k 选股真正兑现的部分）
   - pooled IC：所有股票日堆在一起算，混入时序/市场择时效应，仅作对照
3) 统计显著性
   - Newey-West t（默认 lag=max(10, 2*horizon)，处理重叠标签）
   - ICIR、胜率、覆盖天数
4) 稳定性
   - DEV 内切 n_folds 个连续折，报每折 IC 与同号折数
   - 按年 IC
5) 与组合目标对齐
   - top-k 超额（top-k 均值 − 当日全池均值）和 top-k vs bottom-k
   - 方向由 DEV IC 符号决定，TEST 不参与定向
6) 增量信息
   - 候选特征对现有特征做逐日横截面回归，取残差再算 IC
7) 多重检验
   - BH FDR q 值、Bonferroni t 门槛；--trial-count 可计入历史搜索次数
8) 滞后敏感性（诊断，不参与选择）
   - 特征平移 k 日后的 IC（k=-2..2），看信号随数据变旧衰减多快
   - 同时报特征自身 lag-1 自相关作为参照
   - 注意：这不是前视偏差检验。前视需要"截断到 T 重算特征、比较 X_T"
9) 预注册选择规则（只看 DEV）
   - |IC| >= --min-ic
   - |NW t| >= --min-t
   - 同号折数 >= --min-sign-folds
   - top-k 超额 > 0（--gate-topk）
   - BH q <= --fdr-q
   - 覆盖 >= --min-coverage
   TEST 只做最终验证，任何选择列都不使用 TEST。

统计口径
--------
本脚本只做"特征工程 + 报告"，所有统计量的计算都委托给同目录的
ml_stats.py（仓库内 IC / Newey-West / 多重检验 / 稳定性的唯一实现）。
不要再在本文件里重写 Newey-West 或横截面 IC——历史上仓库里因此出现过
4 份 NW-t、5 份横截面 IC，各自归一化方式不同，t 值无法横向比较。

输入格式
--------
特征缓存（必需）：宽表 CSV，index 为日期，列名 `<symbol>.<feature>`，
至少包含 `<symbol>.close`。这是 C++ 引擎 /v0/ml action=collect 的产出格式。

OHLCV（可选）：用于可交易性过滤和次日开盘入场。支持三种格式
  A. 宽表 CSV：列名 `<symbol>.open/.high/.low/.close/.volume/.turnover`
  B. 长表 CSV：列 `date,symbol,open,high,low,close,volume,turnover`
  C. 目录：每个标的一个 CSV，文件名即代码（如 sh.600176.csv 或 600176.SH.csv）
没有 OHLCV 时退回 close-to-close，并在报告里明确标注"未做涨跌停/停牌/流动性过滤"。

用法
----
  /mnt/disk/third_party/anaconda3/bin/python service/tools/feature_screen.py
  python service/tools/feature_screen.py --feature-cache <cache.csv>
  python service/tools/feature_screen.py --ohlcv-cache /path/to/ohlcv --entry next_open
  python service/tools/feature_screen.py --features trend_str,vpcorr --no-incremental

输出
----
  默认写到数据根目录的 reports/feature_screen/ 下：
    feature_screen.md / .csv / .json
    feature_screen_ic.png / _stability.png / _year.png / _decay.png
    feature_screen_lag_sensitivity.png（--no-lag-sensitivity 时不生成）

注意
----
- DEV 最后 max(horizons) 根 bar 会被 purge，避免 forward label 跨入 TEST。
- 若这些特征来自历史搜索，请用 --trial-count 传入真实尝试次数，否则 FDR 偏乐观。
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd

# matplotlib 是可选依赖：服务端通过 REST 调用时由前端绘图（--no-plot），
# 部署环境可能根本没装 matplotlib。缺少时降级为"只出 md/csv/json"，
# 而不是让整个筛选任务失败。
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    _HAS_MPL = True
except Exception:  # ImportError 或后端初始化失败
    plt = None
    _HAS_MPL = False

SCRIPT_DIR = Path(__file__).resolve().parent
SERVICE_ROOT = SCRIPT_DIR.parent
# ml_stats 是统计量的唯一实现，必须能 import 到
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from ml_stats import (MIN_FOLD_OBS, autocorr_1, bh_qvalues, bonferroni_t_crit,
                      daily_cs_ic_wide, effective_gap, fold_stats as _fold_stats,
                      newey_west_t, pooled_ic as _pooled_ic, spearman_ic,
                      topk_excess, ts_ic as _ts_ic)


def resolve_data_root() -> Path:
    """数据根目录。

    优先取环境变量 QS_DATA_DIR；否则按服务实际布局探测——部署时是
    <service>/data，开发时是 <service>/build/data（服务的工作目录是
    build/，config.json 里的 "quote": "data" 等相对路径都以此为基准）。
    """
    env = os.environ.get("QS_DATA_DIR", "").strip()
    if env:
        return Path(env).expanduser().resolve()
    for cand in (SERVICE_ROOT / "build" / "data", SERVICE_ROOT / "data"):
        if cand.is_dir():
            return cand
    return SERVICE_ROOT / "build" / "data"


DATA_ROOT = resolve_data_root()
# 默认输出根：<data>/reports/feature_screen/
DEFAULT_OUT = DATA_ROOT / "reports" / "feature_screen"

if _HAS_MPL:
    plt.rcParams["font.sans-serif"] = ["Noto Sans CJK JP", "Droid Sans Fallback",
                                       "WenQuanYi Zen Hei", "SimHei", "DejaVu Sans"]
    plt.rcParams["axes.unicode_minus"] = False

OHLCV_FIELDS = ("open", "high", "low", "close", "volume", "turnover")


def log(msg: str) -> None:
    print(msg, flush=True)


def emit(obj: dict) -> None:
    """输出一行紧凑 JSON 到 stdout（供 C++ 侧解析，约定同 xgboost_train.py）。"""
    print(json.dumps(obj, ensure_ascii=False, default=str,
                     separators=(",", ":")), flush=True)


# ============================================================
# 符号与数据加载
# ============================================================

def normalize_symbol(s: str) -> str:
    """统一成 sh.600176 / sz.000001 / bj.430047 形式"""
    s = str(s).strip()
    m = re.fullmatch(r"(\d{6})\.(SH|SZ|BJ)", s, re.IGNORECASE)
    if m:
        return f"{m.group(2).lower()}.{m.group(1)}"
    m = re.fullmatch(r"(SH|SZ|BJ)[.\-_]?(\d{6})", s, re.IGNORECASE)
    if m:
        return f"{m.group(1).lower()}.{m.group(2)}"
    # 裸 6 位代码：按板块推断交易所
    if re.fullmatch(r"\d{6}", s):
        if s.startswith(("6", "9")):
            return f"sh.{s}"
        if s.startswith(("0", "3")):
            return f"sz.{s}"
        if s.startswith(("4", "8")):
            return f"bj.{s}"
    return s


def load_feature_cache(path: Path):
    """返回 df, symbols, feature_data

    feature_data: {feature: DataFrame(index=date, columns=symbols)}
    列名解析必须兼容 emd.volume_regime 这种带点的特征名，因此用已知 symbol 前缀匹配。
    """
    df = pd.read_csv(path, index_col=0, parse_dates=True).sort_index()
    if df.index.has_duplicates:
        log(f"  警告: 特征缓存有重复日期 {int(df.index.duplicated().sum())} 行，保留最后一条")
        df = df[~df.index.duplicated(keep="last")]

    symbols = sorted({c[:-len(".close")] for c in df.columns if c.endswith(".close")})
    if not symbols:
        raise SystemExit(f"特征缓存里没有 <symbol>.close 列: {path}")

    # 用 symbol 前缀解析特征名；按长度倒序避免短前缀误匹配
    prefixes = sorted(((s + ".", s) for s in symbols), key=lambda x: -len(x[0]))
    feature_cols: dict[str, dict[str, str]] = {}
    for col in df.columns:
        for pref, sym in prefixes:
            if col.startswith(pref) and col != pref + "close":
                feat = col[len(pref):]
                if feat in ("label", "close") or feat.startswith("label_"):
                    break
                feature_cols.setdefault(feat, {})[sym] = col
                break

    feature_data: dict[str, pd.DataFrame] = {}
    for feat, colmap in feature_cols.items():
        fd = pd.DataFrame({s: df[c] for s, c in colmap.items()})
        fd = fd.reindex(columns=symbols)
        # 至少一半标的有数据才纳入
        if fd.notna().any().sum() < max(1, len(symbols) // 2):
            log(f"  跳过特征 {feat}: 仅 {int(fd.notna().any().sum())}/{len(symbols)} 个标的有数据")
            continue
        feature_data[feat] = fd.astype(float)

    if not feature_data:
        raise SystemExit("没有可筛选的特征列")

    close = pd.DataFrame({s: df[f"{s}.close"] for s in symbols}, index=df.index).astype(float)
    log(f"  特征缓存: {len(df)} 行 × {len(symbols)} 标的 × {len(feature_data)} 特征")
    log(f"  日期: {df.index.min().date()} ~ {df.index.max().date()}")
    return df, symbols, feature_data, close


def _wide_ohlcv(df: pd.DataFrame, index: pd.Index) -> dict[str, pd.DataFrame] | None:
    lower = {str(c).lower(): c for c in df.columns}
    date_col = None
    for cand in ("date", "datetime", "trade_date", "time"):
        if cand in lower:
            date_col = lower[cand]
            break
    if date_col is None:
        first = df.columns[0]
        if str(first).startswith("Unnamed") or str(first).strip() == "":
            try:
                pd.to_datetime(df[first])
                date_col = first
            except (ValueError, TypeError):
                date_col = None

    work = df.drop(columns=[date_col]) if date_col is not None else df
    cols = [c for c in work.columns if "." in str(c)]
    out: dict[str, pd.DataFrame] = {}
    for field in OHLCV_FIELDS:
        suffix = "." + field
        matched = [c for c in cols if str(c).endswith(suffix)]
        if not matched:
            continue
        data = {}
        for c in matched:
            sym = normalize_symbol(str(c)[:-len(suffix)])
            data[sym] = pd.to_numeric(work[c], errors="coerce").to_numpy()
        if date_col is not None:
            dates = pd.DatetimeIndex(pd.to_datetime(df[date_col]))
            out[field] = pd.DataFrame(data, index=dates)
        else:
            if len(work) != len(index):
                raise SystemExit("OHLCV 宽表既没有日期列，行数也与特征缓存不一致")
            out[field] = pd.DataFrame(data, index=index)
    return out or None


def _long_ohlcv(df: pd.DataFrame, index: pd.Index,
                default_symbol: str | None) -> dict[str, pd.DataFrame] | None:
    low = {c.lower(): c for c in df.columns}
    if "date" not in low:
        return None
    date = pd.to_datetime(df[low["date"]])
    if "symbol" in low:
        syms = df[low["symbol"]].map(normalize_symbol)
    elif default_symbol:
        syms = pd.Series(default_symbol, index=df.index)
    else:
        return None

    out: dict[str, pd.DataFrame] = {}
    for field in OHLCV_FIELDS:
        if field not in low:
            continue
        tmp = pd.DataFrame({
            "date": date.values,
            "symbol": syms.values,
            "value": pd.to_numeric(df[low[field]], errors="coerce").values,
        })
        piv = tmp.pivot_table(index="date", columns="symbol", values="value",
                              aggfunc="last")
        out[field] = piv.reindex(index=index)
    return out or None


def load_ohlcv(path: Path, index: pd.Index) -> dict[str, pd.DataFrame] | None:
    """读取 OHLCV，返回 {field: DataFrame(index=date, columns=symbol)}"""
    if not path.exists():
        raise SystemExit(f"OHLCV 路径不存在: {path}")

    merged: dict[str, list[pd.DataFrame]] = {f: [] for f in OHLCV_FIELDS}
    if path.is_dir():
        files = sorted(path.glob("*.csv"))
        if not files:
            raise SystemExit(f"目录下没有 CSV: {path}")
        for p in files:
            one = pd.read_csv(p)
            parsed = (_wide_ohlcv(one, index)
                      or _long_ohlcv(one, index, normalize_symbol(p.stem)))
            if parsed:
                for f, d in parsed.items():
                    merged[f].append(d)
        log(f"  OHLCV: 目录 {path} ({len(files)} 个文件)")
    else:
        one = pd.read_csv(path)
        parsed = _wide_ohlcv(one, index) or _long_ohlcv(one, index, None)
        if not parsed:
            raise SystemExit(
                f"无法识别 OHLCV 格式: {path}。需要宽表 <symbol>.open 等，"
                "或长表 date,symbol,open,...，或每标的 CSV 目录")
        for f, d in parsed.items():
            merged[f].append(d)
        log(f"  OHLCV: {path}")

    out = {}
    for f, frames in merged.items():
        if not frames:
            continue
        # 同一 field 多文件时按列合并（后出现的覆盖前面的 NaN）
        combined = pd.concat(frames, axis=1)
        combined = combined.loc[:, ~combined.columns.duplicated(keep="last")]
        out[f] = combined.sort_index()
    return out or None


# ============================================================
# 标签 / 可交易性
# ============================================================

def limit_pct(symbol: str) -> float:
    """涨跌停幅度。ST 的 5% 没有元数据，暂不处理。"""
    code = symbol.split(".")[-1]
    if code.startswith(("300", "301", "688", "689")):
        return 0.20
    if code.startswith(("8", "4")):
        return 0.30
    return 0.10


def compute_forward(close: pd.DataFrame, horizon: int, entry: str,
                    ohlcv: dict[str, pd.DataFrame] | None) -> pd.DataFrame:
    """从 t 日特征预测的 forward return

    entry=close     : close[t+h] / close[t] - 1
    entry=next_open : open[t+1+h] / open[t+1] - 1
    """
    if entry == "close":
        fwd = close.shift(-horizon) / close - 1.0
    else:
        if ohlcv is None or "open" not in ohlcv:
            raise SystemExit("--entry next_open 需要 --ohlcv-cache 且包含 open 列")
        op = ohlcv["open"].reindex(index=close.index, columns=close.columns)
        fwd = op.shift(-(horizon + 1)) / op.shift(-1) - 1.0
    return fwd.replace([np.inf, -np.inf], np.nan)


def build_trade_mask(close: pd.DataFrame, horizon: int, entry: str,
                     ohlcv: dict[str, pd.DataFrame] | None,
                     limit_eps: float, min_turnover: float) -> pd.DataFrame | None:
    """可交易性掩码：停牌、涨跌停、低流动性。

    没有 OHLCV 时返回 None（报告里会标注未过滤）。
    """
    if ohlcv is None:
        return None

    index, symbols = close.index, close.columns
    mask = pd.DataFrame(True, index=index, columns=symbols)

    volume = ohlcv.get("volume")
    if volume is not None:
        v = volume.reindex(index=index, columns=symbols)
        # 入场那根 bar 必须有成交量
        mask &= v.shift(-1 if entry == "next_open" else 0) > 0

    turnover = ohlcv.get("turnover")
    if turnover is not None and min_turnover > 0:
        to = turnover.reindex(index=index, columns=symbols)
        mask &= to.shift(-1 if entry == "next_open" else 0) >= min_turnover

    for sym in symbols:
        lim = limit_pct(sym) - limit_eps
        if entry == "next_open":
            op = ohlcv["open"].reindex(index=index)[sym]
            prev_close = close[sym]
            # 次日开盘一字涨停买不到
            mask[sym] &= op.shift(-1) < prev_close * (1.0 + lim)
            # 到期开盘一字跌停卖不掉
            mask[sym] &= op.shift(-(horizon + 1)) > close[sym].shift(-horizon) * (1.0 - lim)
        else:
            # close 入场：当日收盘涨停买不到
            mask[sym] &= close[sym] < close[sym].shift(1) * (1.0 + lim)
            # 到期收盘跌停卖不掉
            mask[sym] &= close[sym].shift(-horizon) > close[sym].shift(-(horizon - 1)) * (1.0 - lim)
    return mask


def winsorize_cs(df: pd.DataFrame, q: float) -> pd.DataFrame:
    """按日横截面缩尾，降低极端收益对 top-k 均值的影响"""
    if q <= 0:
        return df
    lo = df.quantile(q, axis=1)
    hi = df.quantile(1 - q, axis=1)
    return df.clip(lower=lo, upper=hi, axis=0)


def vol_scale_forward(fwd: pd.DataFrame, close: pd.DataFrame, horizon: int,
                      window: int = 20) -> pd.DataFrame:
    """把 forward return 除以 trailing 日波动 × sqrt(h)，变成风险调整后收益"""
    ret = close.pct_change()
    vol = ret.rolling(window, min_periods=max(5, window // 2)).std()
    scale = vol * math.sqrt(horizon)
    return (fwd / scale.replace(0.0, np.nan)).replace([np.inf, -np.inf], np.nan)


# ============================================================
# 统计
#
# 下面全是 ml_stats 的 pandas 适配层：真正的实现（Newey-West、横截面 IC、
# pooled / 时序 IC、top-k、折稳定性）都在同目录的 ml_stats.py 里。
# 仓库内不再保留第二份实现——历史上这里和 ml_metrics.py、lab/ 下两个脚本
# 各写了一套 NW-t，归一化方式不同，t 值无法横向比较。
# ============================================================

def _mask_array(mask: pd.DataFrame | None, like: pd.DataFrame):
    """把 DataFrame 掩码转成 numpy 布尔数组；None 原样返回。"""
    if mask is None:
        return None
    return mask.reindex_like(like).fillna(False).to_numpy(dtype=bool)


def daily_cs_ic(x: pd.DataFrame, y: pd.DataFrame, mask: pd.DataFrame | None,
                min_names: int) -> tuple[pd.Series, pd.Series]:
    """日频横截面 Spearman IC

    返回 (ic_series, n_names_series)。底层是 ml_stats.daily_cs_ic_wide。
    """
    ic, n = daily_cs_ic_wide(x.to_numpy(dtype=float), y.to_numpy(dtype=float),
                              _mask_array(mask, x), min_names)
    return pd.Series(ic, index=x.index), pd.Series(n, index=x.index)


def pooled_ic(x: pd.DataFrame, y: pd.DataFrame, mask: pd.DataFrame | None) -> float:
    m = _mask_array(mask, x)
    return _pooled_ic(x.to_numpy(dtype=float), y.to_numpy(dtype=float), m)


def ts_ic(x: pd.DataFrame, y: pd.DataFrame, mask: pd.DataFrame | None,
          min_obs: int = 60) -> tuple[float, float]:
    """逐标的时间序列 IC 的均值/标准差，用来和横截面 IC 对照"""
    m = _mask_array(mask, x)
    return _ts_ic(x.to_numpy(dtype=float), y.to_numpy(dtype=float), m, min_obs)


def topk_stats(x: pd.DataFrame, fwd: pd.DataFrame, mask: pd.DataFrame | None,
               dates: pd.Index, sign: int, k: int, min_names: int,
               nw_lag: int) -> dict:
    """top-k 超额与 top-bottom spread

    score = sign * feature。sign 由 DEV IC 决定，TEST 沿用，避免用 TEST 定向。
    底层是 ml_stats.topk_excess。
    """
    m = _mask_array(mask, x)
    rows = x.index.get_indexer(dates)
    rows = rows[rows >= 0]
    return topk_excess(x.to_numpy(dtype=float), fwd.to_numpy(dtype=float), m,
                       sign, k, min_names, nw_lag, dates=rows)


def period_stats(x: pd.DataFrame, fwd: pd.DataFrame, mask: pd.DataFrame | None,
                 dates: pd.Index, min_names: int, nw_lag: int,
                 do_ts: bool) -> dict:
    xp = x.loc[dates]
    yp = fwd.loc[dates]
    mp = mask.loc[dates] if mask is not None else None

    ic, _ = daily_cs_ic(xp, yp, mp, min_names)
    ic_valid = ic.dropna()
    out = {
        "days": int(ic_valid.shape[0]),
        "coverage": float(ic_valid.shape[0] / max(1, len(dates))),
        "ic_mean": float(ic_valid.mean()) if len(ic_valid) else float("nan"),
        "ic_std": float(ic_valid.std(ddof=1)) if len(ic_valid) > 1 else float("nan"),
        "hit_rate": float((ic_valid > 0).mean()) if len(ic_valid) else float("nan"),
        "pooled_ic": pooled_ic(xp, yp, mp),
        "_ic": ic_valid,
        # 完整逐日序列（含无效日的 NaN）：fold 按日历 bar 切折时要用它，
        # 不能用 dropna 后的 _ic，否则 gap 会变成"有效观测数"口径。
        "_ic_full": ic,
    }
    t, p = newey_west_t(ic_valid, nw_lag)
    out["nw_t"], out["p_value"] = t, p
    out["icir"] = (out["ic_mean"] / out["ic_std"]
                   if np.isfinite(out["ic_std"]) and out["ic_std"] > 1e-12 else float("nan"))
    if do_ts:
        ts_mean, ts_std = ts_ic(xp, yp, mp)
        out["ts_ic_mean"], out["ts_ic_std"] = ts_mean, ts_std
    return out


def fold_stats(ic: pd.Series, n_folds: int, nw_lag: int,
               gap: int = 0, horizon: int = 0) -> dict:
    """DEV 内连续折的 IC 与同号折数。底层是 ml_stats.fold_stats。

    ic 必须是**带日期索引的完整逐日序列**（含无效日的 NaN）：gap 按日历
    bar 计，若先 dropna 再切，gap 会变成"有效观测数"口径，与 bar 口径不符。
    """
    return _fold_stats(ic, n_folds, nw_lag, gap=gap, horizon=horizon)


# ============================================================
# 滞后敏感性（诊断，不参与选择）
# ============================================================

LAG_VERDICT_TEXT = {
    "persistent": "高度持续",
    "gradual": "缓慢衰减",
    "decaying": "衰减较快",
    "weak": "信号过弱",
    "insufficient": "样本不足",
}

# |IC(0)| 低于该值时，保留率的分母太小，比值完全由噪声决定，不给持续性结论
LAG_MIN_IC = 0.01


def shift_rows(a: np.ndarray, k: int) -> np.ndarray:
    """沿时间轴平移 k 行：shift(k)[t] = a[t-k]，空出的行填 NaN。

    与 pandas.DataFrame.shift 的约定一致：k>0 取更旧的值（滞后），
    k<0 取更未来的值。比 np.roll 安全——不会把首尾数据绕回另一端。
    """
    a = np.asarray(a, dtype=float)
    if k == 0:
        return a.copy()
    out = np.full_like(a, np.nan)
    if k > 0:
        if k < a.shape[0]:
            out[k:] = a[:-k]
    else:
        if -k < a.shape[0]:
            out[:k] = a[-k:]
    return out


def lag_sensitivity(x: pd.DataFrame, fwd: pd.DataFrame,
                    mask: pd.DataFrame | None, dates: pd.Index,
                    shifts: list[int], min_names: int,
                    nw_lag: int) -> dict:
    """特征平移 k 日后的横截面 IC，衡量信号随数据变旧衰减多快。

    约定 shift(k)[t] = x[t-k]：
      k > 0 —— 用更旧的数据（滞后），衡量"信号能保鲜多久"；
      k < 0 —— 用更未来的数据，h>1 时与标签窗口重叠，只作参考。

    **这不是前视偏差检验。** 若特征本身混入了未来数据，平移后 IC 依然会
    异常，本表无法定性；决定性检验是"把输入截断到 T、重算特征、比较 X_T"。

    判定只看滞后一侧：|IC(+1)| / |IC(0)|。
      >= 0.9 → persistent（慢变状态量，隔日更新几乎无损）
      >= 0.5 → gradual
      <  0.5 → decaying
    |IC(0)| < LAG_MIN_IC 时比值由噪声主导，判为 weak，不给持续性结论。
    """
    xp = x.loc[dates]
    yp = fwd.loc[dates]
    mp = mask.loc[dates] if mask is not None else None
    arr = xp.to_numpy(dtype=float)
    yarr = yp.to_numpy(dtype=float)
    marr = _mask_array(mp, xp)

    ic_by_shift, t_by_shift = {}, {}
    for k in shifts:
        ic, _ = daily_cs_ic_wide(shift_rows(arr, k), yarr, marr, min_names)
        s = pd.Series(ic, index=xp.index).dropna()
        ic_by_shift[str(k)] = float(s.mean()) if len(s) else float("nan")
        t, _ = newey_west_t(s, nw_lag)
        t_by_shift[str(k)] = t

    base = ic_by_shift.get("0", float("nan"))
    ac1 = autocorr_1(arr)

    retention = float("nan")
    if np.isfinite(base) and abs(base) > 1e-12 and "1" in ic_by_shift:
        ic1 = ic_by_shift["1"]
        if np.isfinite(ic1):
            retention = abs(ic1) / abs(base)

    if not np.isfinite(base) or abs(base) < 1e-12:
        verdict = "insufficient"
    elif abs(base) < LAG_MIN_IC:
        verdict = "weak"
    elif not np.isfinite(retention):
        verdict = "insufficient"
    elif retention >= 0.9:
        verdict = "persistent"
    elif retention >= 0.5:
        verdict = "gradual"
    else:
        verdict = "decaying"

    return {"shifts": [int(k) for k in shifts],
            "ic": ic_by_shift, "t": t_by_shift,
            "autocorr1": ac1, "lag_retention": retention,
            "verdict": verdict}


def incremental_stats(target: pd.DataFrame, target_name: str,
                      features: dict[str, pd.DataFrame],
                      fwd: pd.DataFrame, mask: pd.DataFrame | None,
                      dates: pd.Index, min_names: int, nw_lag: int) -> dict:
    """候选特征对现有特征逐日横截面回归后的残差 IC

    回答"去掉其他特征能解释的部分，它还剩多少信息"。
    为避免高维回归吃样本，要求当日 target + 所有 regressor 都非缺失。
    """
    others = [f for f in features if f != target_name]
    if not others:
        return {"incr_ic": float("nan"), "incr_t": float("nan"),
                "incr_ratio": float("nan"), "incr_days": 0, "n_regressors": 0}

    X = np.stack([features[f].loc[dates].to_numpy() for f in others], axis=-1)
    y = target.loc[dates].to_numpy()
    fv = fwd.loc[dates].to_numpy()
    valid = ~np.isnan(y) & ~np.isnan(fv)
    if mask is not None:
        valid &= mask.loc[dates].to_numpy()
    for j in range(X.shape[2]):
        valid &= ~np.isnan(X[:, :, j])

    need = max(min_names, X.shape[2] + 2)
    ics = {}
    for i, d in enumerate(dates):
        m = valid[i]
        if int(m.sum()) < need:
            continue
        Xi, yi, fi = X[i][m], y[i][m], fv[i][m]
        sd = Xi.std(axis=0)
        keep = sd > 1e-12
        if not keep.any():
            continue
        Xi = (Xi[:, keep] - Xi[:, keep].mean(axis=0)) / sd[keep]
        A = np.column_stack([np.ones(len(Xi)), Xi])
        coef, *_ = np.linalg.lstsq(A, yi, rcond=None)
        resid = yi - A @ coef
        if np.std(resid) < 1e-12:
            continue
        v = spearman_ic(resid, fi)
        if pd.notna(v):
            ics[d] = float(v)

    if len(ics) < 10:
        return {"incr_ic": float("nan"), "incr_t": float("nan"),
                "incr_ratio": float("nan"), "incr_days": len(ics),
                "n_regressors": len(others)}
    s = pd.Series(ics).sort_index()
    t, _ = newey_west_t(s, nw_lag)
    return {"incr_ic": float(s.mean()), "incr_t": t,
            "incr_ratio": float("nan"), "incr_days": len(s),
            "n_regressors": len(others)}


# ============================================================
# 主流程
# ============================================================

def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="统一特征筛选（Step 1）")
    ap.add_argument("--feature-cache", required=True,
                    help="特征缓存宽表 CSV（C++ /v0/ml action=collect 的产出）")
    ap.add_argument("--out-dir", default=str(DEFAULT_OUT),
                    help="输出目录；默认 <data>/reports/feature_screen/")
    ap.add_argument("--ohlcv-cache", default="",
                    help="可选: OHLCV 宽表/长表 CSV 或每标的 CSV 目录")
    ap.add_argument("--entry", choices=["close", "next_open"], default="close",
                    help="forward return 入场价: close 或次日开盘 next_open")
    ap.add_argument("--horizons", default="1,5,10,20",
                    help="IC 衰减用的 horizon 列表，逗号分隔")
    ap.add_argument("--primary-horizon", type=int, default=5,
                    help="选择与 top-k 使用的主 horizon")
    ap.add_argument("--dev-end", default="2025-10-28",
                    help="DEV 最后一天（含）; 找不到则退化为前 80%%")
    ap.add_argument("--test-start", default="2025-10-29",
                    help="锁定 TEST 第一天（含）; 找不到则退化为后 20%%")
    ap.add_argument("--n-folds", type=int, default=5, help="DEV 内连续折数")
    ap.add_argument("--fold-gap", type=int, default=0,
                    help="相邻折之间跳过的 bar 数；生效值 = max(fold_gap, primary_horizon)，"
                         "用于保证相邻折的标签窗口不重叠")
    ap.add_argument("--min-names", type=int, default=15,
                    help="单日横截面最少有效标的数")
    ap.add_argument("--nw-lag", type=int, default=0,
                    help="Newey-West lag; 0=auto max(10, 2*horizon)")
    ap.add_argument("--topk", default="3,5,10", help="top-k 列表")
    ap.add_argument("--gate-topk", type=int, default=5,
                    help="选择规则使用的 top-k（默认 5，比 top3 稳）")
    ap.add_argument("--min-ic", type=float, default=0.02)
    ap.add_argument("--min-t", type=float, default=2.5)
    ap.add_argument("--min-sign-folds", type=int, default=4)
    ap.add_argument("--fdr-q", type=float, default=0.10)
    ap.add_argument("--min-coverage", type=float, default=0.50)
    ap.add_argument("--winsorize", type=float, default=0.01,
                    help="按日横截面缩尾分位; 0=关闭")
    ap.add_argument("--vol-scale", action="store_true",
                    help="forward return 除以 trailing vol*sqrt(h)")
    ap.add_argument("--limit-eps", type=float, default=0.002,
                    help="涨跌停判定容差（默认 0.2%%）")
    ap.add_argument("--min-turnover", type=float, default=0.0,
                    help="最低换手率; 0=不过滤。单位与 OHLCV 的 turnover 列一致")
    ap.add_argument("--trial-count", type=int, default=0,
                    help="历史累计尝试过的特征数; 0=用本次候选数")
    ap.add_argument("--features", default="",
                    help="只筛这些特征（逗号分隔）; 空=全部")
    ap.add_argument("--incremental-base", default="",
                    help="增量IC的回归基准特征（逗号分隔）；空=用本次全部特征。"
                         "候选特征很多时应指定现有特征集，避免 p>n")
    ap.add_argument("--max-plot-features", type=int, default=30,
                    help="图中最多展示多少个特征（表格仍含全部）")
    ap.add_argument("--no-test", action="store_true", help="不计算 TEST 指标")
    ap.add_argument("--no-incremental", action="store_true", help="跳过增量 IC")
    ap.add_argument("--lag-shifts", default="-2,-1,0,1,2",
                    help="滞后敏感性诊断的平移步数，逗号分隔；"
                         "k>0 用更旧的数据，k<0 用更未来的数据")
    ap.add_argument("--no-lag-sensitivity", action="store_true",
                    help="跳过滞后敏感性诊断")
    ap.add_argument("--no-plot", action="store_true",
                    help="不画图（服务端 REST 调用用；前端拿 JSON 自绘）")
    ap.add_argument("--emit-events", action="store_true",
                    help="向 stdout 输出 step/progress/result JSON 行，供 C++ 侧解析")
    ap.add_argument("--tag", default="", help="输出文件名后缀")
    return ap.parse_args()


def split_periods(index: pd.DatetimeIndex, dev_end: str, test_start: str,
                  purge: int) -> tuple[pd.Index, pd.Index, pd.Timestamp]:
    dev_all = index[index <= pd.Timestamp(dev_end)]
    test_all = index[index >= pd.Timestamp(test_start)]

    if len(dev_all) == 0 or len(test_all) == 0 or dev_all[-1] >= test_all[0]:
        n_dev = int(len(index) * 0.8)
        dev_all = index[:n_dev]
        test_all = index[n_dev:]
        boundary = index[n_dev - 1]
        log(f"  警告: 指定日期切分不适用，退化为 80/20，DEV 截止 {boundary.date()}")
    else:
        boundary = dev_all[-1]

    dev_eval = dev_all[:-purge] if purge > 0 else dev_all
    return dev_eval, test_all, boundary


def main() -> None:
    args = parse_args()

    def step(name: str, status: str, **kw) -> None:
        if args.emit_events:
            emit({"type": "step", "step": name, "status": status, **kw})

    t0 = datetime.now()
    cache = Path(args.feature_cache)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    suffix = f"_{args.tag}" if args.tag else ""

    step("validate", "start")
    if not cache.exists():
        raise SystemExit(f"特征缓存不存在: {cache}")
    step("validate", "done")

    step("load_cache", "start")
    log("加载特征缓存…")
    df, symbols, feature_data, close = load_feature_cache(cache)
    all_feature_data = dict(feature_data)

    if args.features:
        wanted = [f.strip() for f in args.features.split(",") if f.strip()]
        missing = [f for f in wanted if f not in feature_data]
        if missing:
            raise SystemExit(f"缓存里没有这些特征: {missing}")
        feature_data = {f: feature_data[f] for f in wanted}

    base_names = [f.strip() for f in args.incremental_base.split(",") if f.strip()]
    if base_names:
        missing = [f for f in base_names if f not in all_feature_data]
        if missing:
            raise SystemExit(f"增量基准里没有这些特征: {missing}")
        incr_data = {f: all_feature_data[f] for f in base_names}
        log(f"  增量 IC 基准: {len(incr_data)} 个现有特征")
    else:
        incr_data = all_feature_data

    step("load_cache", "done",
         features=len(feature_data), symbols=len(symbols), rows=len(df))

    ohlcv = None
    if args.ohlcv_cache:
        log("加载 OHLCV…")
        ohlcv = load_ohlcv(Path(args.ohlcv_cache), close.index)
        if ohlcv:
            # 只保留缓存里存在的标的
            for f in list(ohlcv):
                ohlcv[f] = ohlcv[f].reindex(columns=symbols)
            missing = [s for s in symbols if s not in ohlcv.get("close", pd.DataFrame()).columns]
            if missing:
                log(f"  警告: {len(missing)} 个标的没有 OHLCV，将被跳过: {missing[:5]}…")
    elif args.entry == "next_open":
        raise SystemExit("--entry next_open 需要 --ohlcv-cache")

    # ---- 时间切分 ----
    horizons = sorted({int(x) for x in args.horizons.split(",") if x.strip()})
    if args.primary_horizon not in horizons:
        horizons = sorted(set(horizons + [args.primary_horizon]))
    purge = max(horizons)
    dev_dates, test_dates, boundary = split_periods(
        close.index, args.dev_end, args.test_start, purge)
    log(f"  DEV: {dev_dates.min().date()} ~ {dev_dates.max().date()} "
        f"({len(dev_dates)} 天, purge {purge} 天)")
    log(f"  TEST: {test_dates.min().date()} ~ {test_dates.max().date()} "
        f"({len(test_dates)} 天)")

    # ---- 折间间隔（gap）----
    # 生效值 = max(fold_gap, primary_horizon)：标签是 primary_horizon 根 bar
    # 的前瞻收益，相邻折至少要隔这么多 bar，两折的标签窗口才不重叠。
    eff_gap, gap_raised = effective_gap(args.fold_gap, args.primary_horizon)
    need_days = args.n_folds * MIN_FOLD_OBS + max(0, args.n_folds - 1) * eff_gap
    if len(dev_dates) < need_days:
        raise SystemExit(
            f"DEV 区间不足以支撑 {args.n_folds} 折 + 折间间隔 {eff_gap} bar："
            f"需要至少 {need_days} 根 bar，当前只有 {len(dev_dates)} 根。"
            f"请减少 --n-folds、减小 --fold-gap，或拉长 DEV 区间。")
    if gap_raised:
        log(f"  折间间隔：设定 {args.fold_gap} → 生效 {eff_gap} bar"
            f"（受 primary_horizon={args.primary_horizon} 约束，不可小于它）")
    else:
        log(f"  折间间隔：{eff_gap} bar")

    # ---- 目标与掩码 ----
    step("compute_target", "start")
    targets = {}
    for h in horizons:
        fwd = compute_forward(close, h, args.entry, ohlcv)
        fwd = winsorize_cs(fwd, args.winsorize)
        if args.vol_scale:
            fwd = vol_scale_forward(fwd, close, h)
        mask = build_trade_mask(close, h, args.entry, ohlcv,
                                args.limit_eps, args.min_turnover)
        targets[h] = {"fwd": fwd, "mask": mask}
        log(f"  h={h}: 有效 forward 观测 {int(fwd.notna().sum().sum())}")

    step("compute_target", "done",
         dev_days=len(dev_dates), test_days=len(test_dates))

    # ---- 逐特征评估 ----
    step("evaluate", "start", total=len(feature_data), primary_horizon=args.primary_horizon)
    results: dict[str, dict] = {}
    for fi, feat in enumerate(feature_data, 1):
        x = feature_data[feat]
        target = targets[args.primary_horizon]
        nw_lag = args.nw_lag or max(10, 2 * args.primary_horizon)

        # DEV: 先算 IC，再定向算 top-k
        dev = period_stats(x, target["fwd"], target["mask"], dev_dates,
                           args.min_names, nw_lag, do_ts=True)
        sign = 1 if (not np.isfinite(dev["ic_mean"]) or dev["ic_mean"] >= 0) else -1
        dev["topk"] = {k: topk_stats(x, target["fwd"], target["mask"], dev_dates,
                                     sign, k, args.min_names, nw_lag)
                       for k in [int(v) for v in args.topk.split(",")]}
        dev["folds"] = fold_stats(dev["_ic_full"], args.n_folds, nw_lag,
                                  gap=args.fold_gap, horizon=args.primary_horizon)
        dev["yearly"] = {int(y): float(v) for y, v in
                         dev["_ic"].groupby(dev["_ic"].index.year).mean().items()}

        # 增量 IC（DEV）
        if args.no_incremental:
            incr = {"incr_ic": float("nan"), "incr_t": float("nan"),
                    "incr_ratio": float("nan"), "incr_days": 0, "n_regressors": 0}
        else:
            incr = incremental_stats(x, feat, incr_data, target["fwd"],
                                     target["mask"], dev_dates,
                                     args.min_names, nw_lag)
            if np.isfinite(dev["ic_mean"]) and abs(dev["ic_mean"]) > 1e-12 \
                    and np.isfinite(incr["incr_ic"]):
                incr["incr_ratio"] = incr["incr_ic"] / dev["ic_mean"]

        # IC 衰减
        decay = {}
        for h in horizons:
            st = period_stats(x, targets[h]["fwd"], targets[h]["mask"], dev_dates,
                              args.min_names, args.nw_lag or max(10, 2 * h),
                              do_ts=False)
            decay[h] = {"ic": st["ic_mean"], "t": st["nw_t"],
                        "days": st["days"], "coverage": st["coverage"]}

        # 滞后敏感性（诊断，不参与选择）
        if args.no_lag_sensitivity:
            lag = {}
        else:
            lag = lag_sensitivity(x, target["fwd"], target["mask"], dev_dates,
                                   [int(v) for v in args.lag_shifts.split(",")
                                    if v.strip()],
                                   args.min_names, nw_lag)

        # TEST：只验证，不参与 sign/选择
        test = None
        if not args.no_test:
            test = period_stats(x, target["fwd"], target["mask"], test_dates,
                                args.min_names, nw_lag, do_ts=False)
            test["topk"] = {k: topk_stats(x, target["fwd"], target["mask"],
                                          test_dates, sign, k, args.min_names, nw_lag)
                            for k in [int(v) for v in args.topk.split(",")]}

        results[feat] = {"dev": dev, "test": test, "incremental": incr,
                         "decay": decay, "lag": lag, "sign": sign,
                         "coverage": dev["coverage"]}

        log(f"  [{fi}/{len(feature_data)}] {feat:20s} "
            f"IC={dev['ic_mean']:+.4f} t={dev['nw_t']:+.2f} "
            f"fold={dev['folds']['sign_consistency']}/{args.n_folds} "
            f"incr={incr['incr_ic']:+.4f}")
        if args.emit_events:
            emit({"type": "progress", "current": fi,
                  "total": len(feature_data), "feature": feat,
                  "ic": dev["ic_mean"], "nw_t": dev["nw_t"]})

    step("evaluate", "done")

    # ---- 多重检验与选择 ----
    step("multiple_testing", "start")
    feats = list(results)
    pvals = [results[f]["dev"]["p_value"] for f in feats]
    m_tests = args.trial_count if args.trial_count > 0 else len(feats)
    qvals = bh_qvalues(pvals, m_tests=m_tests)
    t_crit = bonferroni_t_crit(m_tests)

    for f, q in zip(feats, qvals):
        r = results[f]
        dev = r["dev"]
        gate_k = args.gate_topk
        topk = dev["topk"].get(gate_k, next(iter(dev["topk"].values())))
        reasons = []
        if not np.isfinite(dev["ic_mean"]) or abs(dev["ic_mean"]) < args.min_ic:
            reasons.append(f"|IC|<{args.min_ic:g}")
        if not np.isfinite(dev["nw_t"]) or abs(dev["nw_t"]) < args.min_t:
            reasons.append(f"|t|<{args.min_t:g}")
        if dev["folds"]["sign_consistency"] < args.min_sign_folds:
            reasons.append(f"同号折<{args.min_sign_folds}/{args.n_folds}")
        if np.isfinite(topk["excess"]) and topk["excess"] <= 0:
            reasons.append(f"top{gate_k}超额<=0")
        if not np.isfinite(q) or q > args.fdr_q:
            reasons.append(f"q>{args.fdr_q:g}")
        if dev["coverage"] < args.min_coverage:
            reasons.append(f"覆盖<{args.min_coverage:.0%}")
        selected = not reasons

        # 原始规则（不含 FDR）通过的标记为 B，便于区分"有点信号但不够多重检验"
        raw_reasons = [x for x in reasons if not x.startswith("q>")]
        tier = "A" if selected else ("B" if not raw_reasons else "C")
        r["q_value"] = float(q) if np.isfinite(q) else float("nan")
        r["selection"] = {"selected": selected, "tier": tier,
                          "reasons": reasons, "gate_k": gate_k,
                          "gate_topk": topk}

    n_a = sum(1 for f in feats if results[f]["selection"]["tier"] == "A")
    n_b = sum(1 for f in feats if results[f]["selection"]["tier"] == "B")
    step("multiple_testing", "done",
         m_tests=m_tests, bonferroni_t=t_crit, n_tier_a=n_a, n_tier_b=n_b)

    # ---- 报告 ----
    step("report", "start")
    log("生成报告…")
    files = write_outputs(args, results, feats, horizons, dev_dates, test_dates,
                          boundary, m_tests, t_crit, ohlcv is not None,
                          out_dir, suffix, t0)
    step("report", "done")

    log(f"完成: {len(feats)} 个特征，A 档 {n_a}，B 档 {n_b}，"
        f"Bonferroni t 门槛 {t_crit:.2f}（M={m_tests}）")
    log(f"输出目录: {out_dir}")

    if args.emit_events:
        emit({"type": "result",
              "out_dir": str(out_dir),
              "files": files,
              "summary": {
                  "n_features": len(feats), "n_tier_a": n_a, "n_tier_b": n_b,
                  "m_tests": m_tests, "bonferroni_t": t_crit,
                  "dev_days": len(dev_dates), "test_days": len(test_dates),
                  "primary_horizon": args.primary_horizon,
                  "lag_sensitivity": not args.no_lag_sensitivity,
                  "plots": _HAS_MPL and not args.no_plot,
              },
              "meta": {
                  "feature_cache": str(args.feature_cache),
                  "entry": args.entry,
                  "dev_end": str(dev_dates.max().date()),
                  "test_start": str(test_dates.min().date()),
                  "horizons": horizons,
                  "nw_lag": args.nw_lag or max(10, 2 * args.primary_horizon),
                  "n_folds": args.n_folds,
                  "fold_gap": int(args.fold_gap),
                  "effective_gap": int(eff_gap),
                  "rules": {"min_ic": args.min_ic, "min_t": args.min_t,
                            "min_sign_folds": args.min_sign_folds,
                            "fdr_q": args.fdr_q,
                            "min_coverage": args.min_coverage},
              }})


# ============================================================
# 输出
# ============================================================

def _f(x, nd: int = 4, signed: bool = False) -> str:
    if x is None:
        return "—"
    try:
        v = float(x)
    except (TypeError, ValueError):
        return str(x)
    if not np.isfinite(v):
        return "—"
    return f"{v:+.{nd}f}" if signed else f"{v:.{nd}f}"


def _pct(x, nd: int = 0) -> str:
    if x is None or not np.isfinite(float(x)):
        return "—"
    return f"{float(x) * 100:.{nd}f}%"


def write_outputs(args, results, feats, horizons, dev_dates, test_dates,
                  boundary, m_tests, t_crit, has_ohlcv, out_dir, suffix,
                  t0):
    primary = args.primary_horizon

    # ---- CSV ----
    rows = []
    for f in feats:
        r = results[f]
        d, inc = r["dev"], r["incremental"]
        row = {
            "feature": f, "tier": r["selection"]["tier"],
            "selected": r["selection"]["selected"],
            "direction": "positive" if r["sign"] > 0 else "negative",
            "dev_ic": d["ic_mean"], "dev_nw_t": d["nw_t"], "dev_p": d["p_value"],
            "dev_icir": d["icir"], "dev_hit": d["hit_rate"],
            "dev_coverage": d["coverage"], "dev_days": d["days"],
            "dev_pooled_ic": d["pooled_ic"],
            "dev_ts_ic": d.get("ts_ic_mean", float("nan")),
            "dev_fold_sign": d["folds"]["sign_consistency"],
            "dev_fold_std": d["folds"]["fold_ic_std"],
            "incr_ic": inc["incr_ic"], "incr_t": inc["incr_t"],
            "incr_ratio": inc["incr_ratio"], "incr_days": inc["incr_days"],
            "q_value": r["q_value"],
            "reasons": ";".join(r["selection"]["reasons"]),
        }
        for k, v in d["topk"].items():
            row[f"top{k}_excess"] = v["excess"]
            row[f"top{k}_t"] = v["excess_t"]
            row[f"top{k}_ls"] = v["ls"]
        for h in horizons:
            row[f"ic_h{h}"] = r["decay"][h]["ic"]
            row[f"t_h{h}"] = r["decay"][h]["t"]
        lag = r.get("lag") or {}
        if lag:
            row["lag_autocorr1"] = lag["autocorr1"]
            row["lag_ic_m1"] = lag["ic"].get("-1", float("nan"))
            row["lag_ic_p1"] = lag["ic"].get("1", float("nan"))
            row["lag_retention"] = lag["lag_retention"]
            row["lag_verdict"] = lag["verdict"]
        if r["test"] is not None:
            t = r["test"]
            row.update({
                "test_ic": t["ic_mean"], "test_nw_t": t["nw_t"],
                "test_icir": t["icir"], "test_hit": t["hit_rate"],
                "test_pooled_ic": t["pooled_ic"],
            })
            for k, v in t["topk"].items():
                row[f"test_top{k}_excess"] = v["excess"]
        rows.append(row)
    csv_path = out_dir / f"feature_screen{suffix}.csv"
    pd.DataFrame(rows).to_csv(csv_path, index=False)

    # ---- JSON ----
    def clean(obj):
        if isinstance(obj, dict):
            return {k: clean(v) for k, v in obj.items()
                    if not (isinstance(k, str) and k.startswith("_"))}
        if isinstance(obj, (list, tuple)):
            return [clean(v) for v in obj]
        if isinstance(obj, (np.floating, np.integer)):
            return obj.item()
        if isinstance(obj, float) and not np.isfinite(obj):
            return None
        return obj

    payload = {
        "meta": {
            "generated_at": t0.strftime("%Y-%m-%d %H:%M:%S"),
            "feature_cache": str(args.feature_cache),
            "ohlcv_cache": args.ohlcv_cache or None,
            "entry": args.entry,
            "primary_horizon": primary,
            "horizons": horizons,
            "winsorize": args.winsorize,
            "vol_scale": bool(args.vol_scale),
            "incremental_base": args.incremental_base or None,
            "dev_end": str(dev_dates.max().date()),
            "dev_split_end": str(boundary.date()),
            "dev_days": len(dev_dates),
            "test_start": str(test_dates.min().date()),
            "test_days": len(test_dates),
            "purge": max(horizons),
            "n_folds": args.n_folds,
            "fold_gap": int(args.fold_gap),
            "effective_gap": int(effective_gap(args.fold_gap, primary)[0]),
            "min_names": args.min_names,
            "nw_lag": args.nw_lag or max(10, 2 * primary),
            "gate_topk": args.gate_topk,
            "lag_sensitivity": not bool(args.no_lag_sensitivity),
            "lag_shifts": [int(v) for v in args.lag_shifts.split(",") if v.strip()],
            "rules": {
                "min_ic": args.min_ic, "min_t": args.min_t,
                "min_sign_folds": args.min_sign_folds,
                "fdr_q": args.fdr_q, "min_coverage": args.min_coverage,
                "require_topk_positive": True,
            },
            "m_tests": m_tests,
            "bonferroni_t": t_crit,
            "has_ohlcv": has_ohlcv,
        },
        "features": clean(results),
    }
    json_path = out_dir / f"feature_screen{suffix}.json"
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2),
                         encoding="utf-8")

    # ---- 图 ----
    # 服务端 REST 调用一律 --no-plot：由前端拿 JSON 自绘，部署环境也不需要
    # matplotlib。手动运行时才会生成 PNG。
    do_plot = _HAS_MPL and not args.no_plot
    if do_plot:
        plot_ic(results, feats, primary, out_dir, suffix, args.max_plot_features)
        if not args.no_test:
            plot_stability(results, feats, out_dir, suffix)
        plot_yearly(results, feats, out_dir, suffix, args.max_plot_features)
        plot_decay(results, feats, horizons, out_dir, suffix)
        if not args.no_lag_sensitivity:
            plot_lag_sensitivity(results, feats, out_dir, suffix,
                                 args.max_plot_features)
    else:
        reason = "--no-plot" if args.no_plot else "缺少 matplotlib"
        log(f"  跳过绘图（{reason}）；JSON 已包含全部绘图所需数据")

    # ---- Markdown ----
    md = build_markdown(args, results, feats, horizons, dev_dates, test_dates,
                        boundary, m_tests, t_crit, has_ohlcv, suffix, t0,
                        has_plots=do_plot)
    md_path = out_dir / f"feature_screen{suffix}.md"
    md_path.write_text("\n".join(md), encoding="utf-8")

    return {"json": str(json_path), "csv": str(csv_path), "md": str(md_path)}


def plot_ic(results, feats, primary, out_dir, suffix, max_features=30):
    ranked = sorted(feats, key=lambda f: -abs(results[f]["dev"]["nw_t"])
                    if np.isfinite(results[f]["dev"]["nw_t"]) else 0)
    ranked = ranked[:max_features]
    order = sorted(ranked, key=lambda f: results[f]["dev"]["ic_mean"])
    dev = np.array([results[f]["dev"]["ic_mean"] for f in order])
    test = np.array([results[f]["test"]["ic_mean"] if results[f]["test"] else np.nan
                     for f in order])
    se = np.array([abs(results[f]["dev"]["ic_mean"] / results[f]["dev"]["nw_t"])
                   if np.isfinite(results[f]["dev"]["nw_t"]) and results[f]["dev"]["nw_t"] != 0
                   else 0.0 for f in order])
    se = np.nan_to_num(se, nan=0.0, posinf=0.0, neginf=0.0)
    y = np.arange(len(order))

    fig, ax = plt.subplots(figsize=(9, max(4, 0.42 * len(order) + 1.5)))
    ax.barh(y, dev, xerr=1.96 * se, color="#4472c4", alpha=0.85,
            error_kw={"ecolor": "#555", "capsize": 2, "lw": 0.8},
            label=f"DEV (h={primary})")
    if np.isfinite(test).any():
        ax.scatter(test, y, color="#c00000", zorder=3, s=28, label="TEST (验证)")
        for yi, tv in zip(y, test):
            if np.isfinite(tv):
                ax.plot([0, tv], [yi, yi], color="#c00000", alpha=0.25, lw=1)
    ax.axvline(0, color="#333", lw=0.8)
    ax.set_yticks(y)
    ax.set_yticklabels(order, fontsize=9)
    ax.set_xlabel("日频横截面 Spearman IC")
    ax.set_title(f"特征 IC（DEV 误差棒=±1.96×NW SE；红点=TEST，不参与选择）")
    ax.legend(loc="lower right", fontsize=8)
    ax.grid(axis="x", alpha=0.25)
    fig.tight_layout()
    fig.savefig(out_dir / f"feature_screen_ic{suffix}.png", dpi=150)
    plt.close(fig)


def plot_stability(results, feats, out_dir, suffix):
    fig, ax = plt.subplots(figsize=(6.5, 6))
    dev = np.array([results[f]["dev"]["ic_mean"] for f in feats])
    test = np.array([results[f]["test"]["ic_mean"] if results[f]["test"] else np.nan
                     for f in feats])
    tdev = np.array([abs(results[f]["dev"]["nw_t"]) if np.isfinite(results[f]["dev"]["nw_t"])
                     else 0 for f in feats])
    m = np.isfinite(dev) & np.isfinite(test)
    ax.scatter(dev[m], test[m], s=28, alpha=0.8, color="#4472c4")
    ax.axhline(0, color="#333", lw=0.8)
    ax.axvline(0, color="#333", lw=0.8)
    lim = np.nanmax(np.abs(np.r_[dev[m], test[m]])) * 1.15 if m.any() else 0.1
    ax.plot([-lim, lim], [-lim, lim], "--", color="#999", lw=1, label="y=x")
    top = np.argsort(-tdev)[:10]
    for i in top:
        if m[i]:
            ax.annotate(feats[i], (dev[i], test[i]), fontsize=7,
                        xytext=(3, 3), textcoords="offset points")
    ax.set_xlim(-lim, lim)
    ax.set_ylim(-lim, lim)
    ax.set_xlabel("DEV 横截面 IC")
    ax.set_ylabel("TEST 横截面 IC")
    ax.set_title("样本内 vs 锁定 TEST（只做验证，不参与选择）")
    ax.grid(alpha=0.2)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out_dir / f"feature_screen_stability{suffix}.png", dpi=150)
    plt.close(fig)


def plot_yearly(results, feats, out_dir, suffix, max_features=30):
    years = sorted({y for f in feats for y in results[f]["dev"]["yearly"]})
    if not years:
        return
    order = sorted(feats, key=lambda f: -abs(results[f]["dev"]["ic_mean"])
                   if np.isfinite(results[f]["dev"]["ic_mean"]) else 0)[:max_features]
    mat = np.array([[results[f]["dev"]["yearly"].get(y, np.nan) for y in years]
                    for f in order])
    fig, ax = plt.subplots(figsize=(max(6, 1.2 * len(years) + 2),
                                    max(4, 0.38 * len(order) + 1.5)))
    vmax = np.nanmax(np.abs(mat)) if np.isfinite(mat).any() else 0.1
    im = ax.imshow(mat, aspect="auto", cmap="RdBu_r", vmin=-vmax, vmax=vmax)
    ax.set_xticks(range(len(years)))
    ax.set_xticklabels([str(y) for y in years])
    ax.set_yticks(range(len(order)))
    ax.set_yticklabels(order, fontsize=9)
    for i in range(mat.shape[0]):
        for j in range(mat.shape[1]):
            if np.isfinite(mat[i, j]):
                ax.text(j, i, f"{mat[i, j]:+.2f}", ha="center", va="center",
                        fontsize=7, color="white" if abs(mat[i, j]) > vmax * 0.6 else "#222")
    ax.set_title("DEV 年度横截面 IC（看符号是否稳定）")
    fig.colorbar(im, ax=ax, shrink=0.8, label="IC")
    fig.tight_layout()
    fig.savefig(out_dir / f"feature_screen_year{suffix}.png", dpi=150)
    plt.close(fig)


def plot_decay(results, feats, horizons, out_dir, suffix):
    order = sorted(feats, key=lambda f: -abs(results[f]["dev"]["nw_t"])
                   if np.isfinite(results[f]["dev"]["nw_t"]) else 0)[:8]
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for f in order:
        ys = [results[f]["decay"][h]["ic"] for h in horizons]
        ax.plot(horizons, ys, marker="o", lw=1.4, label=f)
    ax.axhline(0, color="#333", lw=0.8)
    ax.set_xlabel("horizon (交易日)")
    ax.set_ylabel("DEV 横截面 IC")
    ax.set_title("IC 衰减：按 |t| 最大的 8 个特征")
    ax.grid(alpha=0.25)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(out_dir / f"feature_screen_decay{suffix}.png", dpi=150)
    plt.close(fig)


def plot_lag_sensitivity(results, feats, out_dir, suffix, max_features=8):
    """归一化 IC（相对 shift=0）随 shift 的曲线。

    shift=0 处恒为 1；曲线越平说明信号越持久。k<0 一侧只作参考——
    horizon>1 时它与标签窗口重叠，不能当作前视偏差的证据。

    只画 |IC(0)| >= LAG_MIN_IC 的特征：分母接近 0 时归一化曲线会被放大到
    几十倍，把其余曲线压平（weak 判定同理）。
    """
    def _lab(f):
        lag = results[f].get("lag") or {}
        base = lag.get("ic", {}).get("0", float("nan"))
        return lag, base

    ranked = [f for f in feats
              if (results[f].get("lag") or {}).get("ic")
              and np.isfinite(_lab(f)[1])
              and abs(_lab(f)[1]) >= LAG_MIN_IC]
    ranked.sort(key=lambda f: -abs(results[f]["dev"]["nw_t"])
                if np.isfinite(results[f]["dev"]["nw_t"]) else 0)
    ranked = ranked[:max_features]
    if not ranked:
        return

    shifts = results[ranked[0]]["lag"]["shifts"]
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    plotted = []
    for f in ranked:
        lag, base = _lab(f)
        ys = [lag["ic"].get(str(k), float("nan")) / base for k in shifts]
        plotted.extend(y for y in ys if np.isfinite(y))
        ax.plot(shifts, ys, marker="o", lw=1.4,
                label=f"{f} (ac1={lag['autocorr1']:.2f})")
    if not plotted:
        plt.close(fig)
        return

    ax.axhline(0, color="#333", lw=0.8)
    ax.axhline(1, color="#999", ls="--", lw=1)
    ax.axvline(0, color="#999", ls="--", lw=1)
    lo = max(-2.0, float(np.min(plotted)) - 0.15)
    hi = min(3.0, float(np.max(plotted)) + 0.15)
    ax.set_ylim(lo, max(hi, lo + 0.5))
    ax.set_xlabel("特征平移步数（>0 用更旧的数据）")
    ax.set_ylabel("IC / IC(shift=0)")
    ax.set_title("滞后敏感性（归一化 IC；虚线为 shift=0 基准）")
    ax.grid(alpha=0.25)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(out_dir / f"feature_screen_lag_sensitivity{suffix}.png", dpi=150)
    plt.close(fig)


def build_markdown(args, results, feats, horizons, dev_dates, test_dates,
                   boundary, m_tests, t_crit, has_ohlcv, suffix, t0,
                   has_plots: bool = True) -> list[str]:
    primary = args.primary_horizon
    eff_gap = effective_gap(args.fold_gap, primary)[0]
    L: list[str] = []
    L.append("# 特征筛选报告（Step 1）\n")
    L.append(f"生成时间：{t0:%Y-%m-%d %H:%M:%S}\n")

    L.append("## 口径\n")
    L.append(f"- 特征缓存：`{args.feature_cache}`")
    L.append(f"- DEV：评估 {dev_dates.min().date()} ~ {dev_dates.max().date()}"
             f"（{len(dev_dates)} 天）；原始切分截止 {boundary.date()}，"
             f"已 purge 最后 {max(horizons)} 根 bar")
    L.append(f"- 锁定 TEST：{test_dates.min().date()} ~ {test_dates.max().date()}"
             f"（{len(test_dates)} 天，仅验证，不参与选择）")
    L.append(f"- 主 horizon：{primary} 日；IC 衰减：{horizons}")
    if args.incremental_base:
        L.append(f"- 增量 IC 基准：`{args.incremental_base}`")
    L.append(f"- 入场：`{args.entry}`；缩尾：{args.winsorize:g}；"
             f"波动率标准化：{'是' if args.vol_scale else '否'}")
    L.append(f"- 单日最少标的：{args.min_names}；NW lag："
             f"{args.nw_lag or max(10, 2 * primary)}；折数：{args.n_folds}"
             f"；折间间隔：{eff_gap} bar（设定 {args.fold_gap}，"
             f"受主 horizon {primary} 约束）")
    if has_ohlcv:
        extras = ["停牌", "涨跌停"]
        if args.min_turnover > 0:
            extras.append(f"换手率≥{args.min_turnover:g}")
        L.append("- 可交易性：已启用（" + "、".join(extras) + "）")
    else:
        L.append("- **可交易性：未启用**（没有 OHLCV，close-to-close，"
                 "未剔除涨跌停/停牌/低流动性，未计成本）")
    L.append("")

    L.append("## 预注册选择规则（只看 DEV）\n")
    L.append(f"1. |DEV 横截面 IC| ≥ **{args.min_ic:g}**")
    L.append(f"2. |Newey-West t| ≥ **{args.min_t:g}**")
    L.append(f"3. DEV 内 **{args.min_sign_folds}/{args.n_folds}** 折同号")
    L.append(f"4. top{args.gate_topk} 超额（top{args.gate_topk} − 全池均值）> 0")
    L.append(f"5. BH FDR q ≤ **{args.fdr_q:g}**")
    L.append(f"6. 覆盖 ≥ **{args.min_coverage:.0%}**")
    L.append("")
    L.append(f"多重检验：M={m_tests}，Bonferroni t 门槛 ≈ **{t_crit:.2f}**。"
             "A=全部通过；B=原始规则通过但 FDR 未过；C=拒绝。"
             "若特征来自历史搜索，请用 `--trial-count` 传入真实尝试数，"
             "否则 q 值偏乐观。\n")

    # 总表
    L.append("## 总表\n")
    L.append("> **DEV 列用于选择；TEST 列只做最终验证。** "
             "`方向` 由 DEV IC 符号决定，反向特征在 top-k 里会先取负号。\n")
    head = ["特征", "方向", "DEV IC", "NW t", "ICIR", "胜率", "覆盖",
            "折同号", "增量IC", "增量t", "top3超额", "top5超额",
            "q值", "结论", "TEST IC", "TEST t"]
    L.append("| " + " | ".join(head) + " |")
    L.append("|" + "---|" * len(head))
    order = sorted(feats, key=lambda f: (
        0 if results[f]["selection"]["tier"] == "A" else
        1 if results[f]["selection"]["tier"] == "B" else 2,
        -(abs(results[f]["dev"]["nw_t"]) if np.isfinite(results[f]["dev"]["nw_t"]) else 0)))
    for f in order:
        r = results[f]
        d, inc, sel = r["dev"], r["incremental"], r["selection"]
        t = r["test"]
        verdict = {"A": "**A 通过**", "B": "B 仅原始规则",
                   "C": "C " + ("、".join(sel["reasons"]) or "拒绝")}[sel["tier"]]
        tk3 = d["topk"].get(3, {}).get("excess", float("nan"))
        tk5 = d["topk"].get(5, {}).get("excess", float("nan"))
        L.append("| " + " | ".join([
            f, "正向" if r["sign"] > 0 else "反向",
            _f(d["ic_mean"], signed=True), _f(d["nw_t"], 2, True),
            _f(d["icir"], 3, True), _pct(d["hit_rate"]), _pct(d["coverage"]),
            f"{d['folds']['sign_consistency']}/{args.n_folds}",
            _f(inc["incr_ic"], signed=True), _f(inc["incr_t"], 2, True),
            _pct(tk3, 2), _pct(tk5, 2),
            _f(r["q_value"], 3), verdict,
            _f(t["ic_mean"], signed=True) if t else "—",
            _f(t["nw_t"], 2, True) if t else "—",
        ]) + " |")
    L.append("")

    # top-k 明细
    L.append("## top-k 超额（DEV）\n")
    L.append("> 超额 = 当日 top-k 均值 − 当日全池均值；方向由 DEV IC 符号决定。"
             "top-k 是尾部选择，和线性 IC 不总一致。\n")
    tk_cols = []
    for k in [int(v) for v in args.topk.split(",")]:
        tk_cols += [f"top{k} 超额", f"top{k} t"]
    L.append("| 特征 | " + " | ".join(tk_cols) + " |")
    L.append("|" + "---|" * (len(tk_cols) + 1))
    for f in order:
        vals = []
        for k in [int(v) for v in args.topk.split(",")]:
            v = results[f]["dev"]["topk"].get(k, {})
            vals += [_pct(v.get("excess", float("nan")), 2),
                     _f(v.get("excess_t", float("nan")), 2, True)]
        L.append(f"| {f} | " + " | ".join(vals) + " |")
    L.append("")

    # IC 衰减
    L.append("## IC 衰减（DEV）\n")
    cols = []
    for h in horizons:
        cols += [f"IC@{h}", f"t@{h}"]
    L.append("| 特征 | " + " | ".join(cols) + " |")
    L.append("|" + "---|" * (len(cols) + 1))
    for f in order:
        r = results[f]
        vals = []
        for h in horizons:
            vals += [_f(r["decay"][h]["ic"], signed=True),
                     _f(r["decay"][h]["t"], 2, True)]
        L.append(f"| {f} | " + " | ".join(vals) + " |")
    L.append("")

    # 滞后敏感性
    if not args.no_lag_sensitivity:
        shifts = [int(v) for v in args.lag_shifts.split(",") if v.strip()]
        L.append("## 滞后敏感性（DEV，诊断项，不参与选择）\n")
        L.append("> `shift(k)[t] = x[t-k]`：k>0 用更旧的数据，k<0 用更未来的数据。"
                 "主看 k>0 一侧，它衡量信号随数据变旧衰减多快；"
                 "k<0 一侧在 horizon>1 时与标签窗口重叠，只作参考。")
        L.append("> **本表不检测前视偏差**：特征若混入未来数据，各 shift 都会异常。"
                 "决定性检验是「截断到 T 重算特征、比较 X_T」。\n")
        head = (["特征", "自相关1"]
                + [("IC@0" if k == 0 else f"IC@{k:+d}") for k in shifts]
                + ["滞后保留", "判定"])
        L.append("| " + " | ".join(head) + " |")
        L.append("|" + "---|" * len(head))
        for f in order:
            lag = results[f].get("lag") or {}
            if not lag:
                continue
            vals = [f, _f(lag["autocorr1"], 3)]
            vals += [_f(lag["ic"].get(str(k), float("nan")), signed=True)
                     for k in shifts]
            vals += [_pct(lag["lag_retention"]),
                     LAG_VERDICT_TEXT.get(lag["verdict"], lag["verdict"])]
            L.append("| " + " | ".join(vals) + " |")
        L.append("")
        L.append("> 判定只看滞后一侧：`|IC(+1)| / |IC(0)|` ≥90% 为高度持续、"
                 "≥50% 为缓慢衰减、其余为衰减较快。"
                 "高自相关的慢变特征不衰减是正常的。"
                 f"`|IC(0)| < {LAG_MIN_IC:g}` 时比值由噪声主导，判为信号过弱。\n")

    # 逐折
    L.append("## 逐折稳定性（DEV）\n")
    L.append(f"> DEV 内切 {args.n_folds} 个连续折，折间跳过 **{eff_gap} bar**"
             f"（设定 {args.fold_gap}，受标签周期 {args.primary_horizon} 约束）。"
             "跳过的 bar 不参与任何折，保证相邻折的标签窗口不重叠。\n")
    L.append("| 特征 | " + " | ".join(f"折{i+1}" for i in range(args.n_folds))
             + " | 同号 | 折间std |")
    L.append("|" + "---|" * (args.n_folds + 3))
    for f in order:
        fo = results[f]["dev"]["folds"]
        vals = [_f(v, signed=True) for v in fo["fold_ic"]]
        vals += [""] * (args.n_folds - len(vals))
        L.append(f"| {f} | " + " | ".join(vals)
                 + f" | {fo['sign_consistency']}/{args.n_folds}"
                 + f" | {_f(fo['fold_ic_std'])} |")
    L.append("")

    # 增量
    if not args.no_incremental:
        L.append("## 增量 IC（对现有特征做逐日横截面回归后的残差）\n")
        L.append("> 残差 IC 接近 0 = 它只是现有特征的重复；"
                 "`保留比例 = 残差IC / 原始IC`。\n")
        L.append("| 特征 | 原始IC | 残差IC | 残差t | 保留比例 | 有效天数 |")
        L.append("|---|---:|---:|---:|---:|---:|")
        for f in sorted(order, key=lambda x: -(abs(results[x]["incremental"]["incr_t"])
                                               if np.isfinite(results[x]["incremental"]["incr_t"])
                                               else 0)):
            inc = results[f]["incremental"]
            L.append(f"| {f} | {_f(results[f]['dev']['ic_mean'], signed=True)} | "
                     f"{_f(inc['incr_ic'], signed=True)} | "
                     f"{_f(inc['incr_t'], 2, True)} | "
                     f"{_pct(inc['incr_ratio']) if np.isfinite(inc['incr_ratio']) else '—'} | "
                     f"{inc['incr_days']} |")
        L.append("")

    # TEST 验证
    if not args.no_test:
        L.append("## 锁定 TEST 验证（只看不改）\n")
        L.append("> TEST 已在历史报告中多次使用，不再是全新 holdout；"
                 "这里的数字只用于判断 DEV 结论是否延续，不参与任何选择。\n")
        L.append("| 特征 | TEST IC | TEST t | ICIR | 胜率 | pooled IC | top3超额 | top5超额 |")
        L.append("|---|---:|---:|---:|---:|---:|---:|---:|")
        for f in order:
            t = results[f]["test"]
            if t is None:
                continue
            L.append(f"| {f} | {_f(t['ic_mean'], signed=True)} | "
                     f"{_f(t['nw_t'], 2, True)} | {_f(t['icir'], 3, True)} | "
                     f"{_pct(t['hit_rate'])} | {_f(t['pooled_ic'], signed=True)} | "
                     f"{_pct(t['topk'].get(3, {}).get('excess', float('nan')), 2)} | "
                     f"{_pct(t['topk'].get(5, {}).get('excess', float('nan')), 2)} |")
        L.append("")

    # 结论
    n_a = [f for f in feats if results[f]["selection"]["tier"] == "A"]
    n_b = [f for f in feats if results[f]["selection"]["tier"] == "B"]
    L.append("## 结论\n")
    L.append(f"- 通过全部预注册规则（A 档）："
             + ("、".join(f"`{x}`" for x in n_a) if n_a else "**无**"))
    L.append(f"- 原始规则通过但 FDR 未过（B 档）："
             + ("、".join(f"`{x}`" for x in n_b) if n_b else "无"))
    L.append(f"- 合计 {len(feats)} 个候选；"
             f"Bonferroni t 门槛 {t_crit:.2f}（M={m_tests}）。")
    if not n_a:
        L.append("- **当前没有任何特征能同时通过 IC、t、折稳定性、top-k 和 FDR。** "
                 "这不是脚本的问题，而是 33 只标的 + 5 日重叠标签下统计功效不足的直接结果。")
    L.append("")

    L.append("## 限制与下一步\n")
    L.append("- 没有 OHLCV 时是 close-to-close、无涨跌停/停牌过滤、无交易成本；"
             "top-k 超额不是可实现的 PnL。")
    L.append("- ST 和上市不足 N 日未过滤（缓存没有 ST 标记/上市日期）；"
             "有 OHLCV 时也只过滤停牌、涨跌停和换手率。")
    L.append("- 33 只标的、5 日重叠标签：NW t 只是近似，"
             "真实有效独立观测约 `天数/5`。")
    L.append("- pooled IC 混入时序/市场择时；选股请看横截面 IC。")
    L.append("- 下一步：补 OHLCV 启用 `--entry next_open`；扩 universe；"
             "把新候选特征加入同一脚本，并用 `--trial-count` 计入历史搜索。")
    L.append("")

    if has_plots:
        L.append("## 图\n")
        L.append(f"![ic](feature_screen_ic{suffix}.png)\n")
        if not args.no_test:
            L.append(f"![stability](feature_screen_stability{suffix}.png)\n")
        L.append(f"![year](feature_screen_year{suffix}.png)\n")
        L.append(f"![decay](feature_screen_decay{suffix}.png)\n")
        if not args.no_lag_sensitivity:
            L.append(f"![lag](feature_screen_lag_sensitivity{suffix}.png)\n")
    else:
        L.append("## 图\n")
        L.append("本次未生成图（`--no-plot` 或缺少 matplotlib）。"
                 "绘图所需数据都在 JSON 里：`dev.ic_mean` / `test.ic_mean`、"
                 "`dev.yearly`、`decay[h]`、`lag`。\n")
    return L


if __name__ == "__main__":
    main()
