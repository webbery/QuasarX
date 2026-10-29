#!/usr/bin/env python3
"""宽表特征缓存的载入与前瞻收益口径（唯一实现）。

背景
----
`feature_screen.py` 原先自己在文件里实现了一套"读宽表 CSV → 解析 symbol →
构造 close / feature 宽表 → 算前瞻收益 → 算可交易性掩码"的逻辑。
稳定性扫描（`stability_scan.py`）需要完全相同的输入口径 —— 同一份 CSV
必须解析出同样的 symbol / 特征 / 前瞻收益，否则门禁说 IC=0.03、稳定性报告
说 0.021，用户无法判断是策略变了还是口径变了。

因此把这一段从 `feature_screen.py` 原样搬到本模块，两边都从这里 import。
搬移只做位置调整，不改任何计算逻辑。

输入格式
--------
特征缓存（宽表 CSV，index 为日期，列名 `<symbol>.<feature>`）：
  - 至少包含 `<symbol>.close`，用于构造前瞻收益
  - 其余列按 symbol 前缀切分为特征宽表

OHLCV（可选），支持三种格式：
  A. 宽表 CSV：列名 `<symbol>.open/.high/.low/.close/.volume/.turnover`
  B. 长表 CSV：列 `date,symbol,open,high,low,close,volume,turnover`
  C. 目录：每个标的一个 CSV，文件名即代码（如 sh.600176.csv）
没有 OHLCV 时退回 close-to-close，并跳过涨跌停/停牌/流动性过滤。
"""

from __future__ import annotations

import math
import re
from pathlib import Path

import numpy as np
import pandas as pd

__all__ = [
    "OHLCV_FIELDS",
    "log",
    "normalize_symbol",
    "load_feature_cache",
    "load_ohlcv",
    "limit_pct",
    "compute_forward",
    "build_trade_mask",
    "winsorize_cs",
    "vol_scale_forward",
    "split_periods",
]

OHLCV_FIELDS = ("open", "high", "low", "close", "volume", "turnover")


def log(msg: str) -> None:
    """与 feature_screen.py 一致的输出约定：一行一条，flush。"""
    print(msg, flush=True)


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
    """返回 df, symbols, feature_data, close

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
# 时间切分
# ============================================================

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
