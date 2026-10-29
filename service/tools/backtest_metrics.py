#!/usr/bin/env python3
"""回测级指标的唯一实现：与 C++ ``ComputeBacktestMetrics`` 逐行对齐。

为什么单独一个模块
------------------
C++ 侧 ``AgentSubSystem::ComputeBacktestMetrics`` 用一套口径算指标，
``service/lab/walk_forward_cta_v16.py`` 里又用 Python 重写了一份同样口径的
实现（用来在客户端按日期窗口切分后重算指标）。但 ``service/lab/`` 是
gitignored 的实验目录，生产脚本引不到，于是"同一套指标的第二份实现"要么
继续躺在实验目录里漂移，要么被再抄第三份。

本模块把这套口径收到 ``service/tools/``（进版本库），供生产脚本使用：

  - ``xgboost_optimize.py`` 需要按 DEV/HOLDOUT 窗口重算指标，必须与服务端
    summary 同口径，否则"选择用的指标"和"报告用的指标"不是同一个东西；
  - ``service/lab/`` 里的同名实现改为引用本模块，消掉重复。

口径（与 C++ 一一对应）
-----------------------
    daily_returns = simple_daily_return(portfolio_values)   # 长度 = len(values) - 1
    total_return  = (V[-1] - V[0]) / V[0]                   # V 为权益曲线
    annual_return = (1 + total_return) ** (YEAR_DAY / n) - 1
    annual_vol    = std(daily_returns, ddof=1) * sqrt(YEAR_DAY)
    sharp         = annual_return / annual_vol              ← 不是 mean/std*sqrt(n)
    max_dd        = max((running_max - V) / running_max)     # running_max 初值 0
    calmar_ratio  = annual_return / max_dd
    win_rate      = 收益 > 0 的天数占比

注意 ``win_rate`` 在 C++ 里吃的是 ``daily_returns``，而 ``max_drawdown_ratio``
吃的是 ``portfolio_values``（权益曲线）——两者入参不同，本模块保持一致。

日期窗口
--------
回测响应里的 ``daily_dates`` 是 epoch 秒（本地时区零点），``daily_returns``
与它平行对齐：``daily_returns[i]`` 是第 i 根 bar 到第 i+1 根的收益，日期取
第 i 根 bar 的日期。特征缓存 CSV 的 ``date`` 列是同一时区的 ``YYYY-MM-DD``
字符串，两边可以直接按日期字符串比较，不受盘中时间戳差异影响。

只依赖 numpy，便于单测和被不同入口（优化脚本、研究脚本）复用。
"""

from __future__ import annotations

import datetime as _dt
import math
from typing import Dict, List, Optional, Sequence

import numpy as np

__all__ = [
    "YEAR_DAY",
    "compute_metrics",
    "daily_dates_to_local_dates",
    "slice_window",
]

# 年化交易日数，与 C++ 的 YEAR_DAY 常量保持一致
YEAR_DAY = 252

_METRIC_KEYS = ("sharp", "total_return", "annual_return", "annual_volatility",
                "max_drawdown", "calmar_ratio", "win_rate", "n_days")


def compute_metrics(daily_returns) -> Dict[str, float]:
    """按 C++ 口径计算回测指标。

    daily_returns：日收益序列（不含初始的 1.0 权益点）。
    返回 dict，键见 ``_METRIC_KEYS``。

    权益曲线在这里现算：以 1.0 为起点累乘日收益。C++ 侧传的是真实的
    portfolio_values，两者比例一致，故 total_return / max_drawdown 相等。
    """
    r = np.asarray(daily_returns, dtype=float)
    n = int(r.size)
    if n == 0:
        return {k: 0.0 for k in _METRIC_KEYS}

    values = np.empty(n + 1, dtype=float)
    values[0] = 1.0
    np.cumprod(1.0 + r, out=values[1:])

    total_return = float((values[-1] - values[0]) / values[0])
    years = n / float(YEAR_DAY)
    if years > 0 and total_return > -1.0:
        annual_return = float((1.0 + total_return) ** (1.0 / years) - 1.0)
    else:
        annual_return = 0.0

    if n < 2:
        annual_vol = 0.0
    else:
        annual_vol = float(r.std(ddof=1)) * math.sqrt(YEAR_DAY)

    sharp = annual_return / annual_vol if annual_vol > 0 else 0.0

    running_max = np.maximum.accumulate(values)
    with np.errstate(divide="ignore", invalid="ignore"):
        dd = np.where(running_max > 0, (running_max - values) / running_max, 0.0)
    max_dd = float(dd.max())

    return {
        "sharp": float(sharp),
        "total_return": total_return,
        "annual_return": annual_return,
        "annual_volatility": float(annual_vol),
        "max_drawdown": max_dd,
        "calmar_ratio": annual_return / max_dd if max_dd > 0 else 0.0,
        "win_rate": float((r > 0).sum()) / n,
        "n_days": n,
    }


def daily_dates_to_local_dates(daily_dates: Sequence) -> List[str]:
    """把回测响应的 epoch 秒转成本地时区的 ``YYYY-MM-DD`` 字符串。

    回测与特征缓存跑在同一台机器、同一时区，所以本地时区换算即可与服务端
    写出的 CSV 日期列对齐。
    """
    out: List[str] = []
    for ts in daily_dates:
        out.append(_dt.datetime.fromtimestamp(int(ts)).strftime("%Y-%m-%d"))
    return out


def slice_window(daily_dates: Sequence, daily_returns: Sequence,
                 start_date: Optional[str], end_date: Optional[str]) -> np.ndarray:
    """按本地日期窗口切出日收益序列（闭区间）。

    start_date / end_date 为 ``YYYY-MM-DD``；None 表示该侧不设限。
    返回切出的收益数组；窗口内无样本时返回空数组。
    """
    dates = np.asarray(daily_returns, dtype=float)
    if dates.size == 0:
        return dates

    local = daily_dates_to_local_dates(daily_dates)
    if len(local) != dates.size:
        raise ValueError(
            f"daily_dates 与 daily_returns 长度不一致：{len(local)} vs {dates.size}")

    mask = np.ones(len(local), dtype=bool)
    if start_date:
        mask &= np.asarray([d >= start_date for d in local], dtype=bool)
    if end_date:
        mask &= np.asarray([d <= end_date for d in local], dtype=bool)
    return dates[mask]
