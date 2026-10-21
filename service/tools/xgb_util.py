"""XGBoost 训练相关的公共工具

`xgboost_train.py` 与 `xgboost_optimize.py` 此前各自持有一份 compute_label 副本。
两份逻辑规则相同（threshold = vol_k × σ × √N，未来 N 日收益三分类），但写法和
边界处理略有差异，容易改一边漏一边。这里抽出唯一实现。

σ 口径
------
expanding（默认，无泄漏）
    逐行因果：第 t 行的阈值只用 [0, t] 的对数收益估计波动率，不含 t 之后的信息。
full（旧行为，有轻度泄漏）
    全序列估一个常数 σ，等于把未来区间的波动率刻度带进了训练标签的定义。
    仅保留用于对照，生产不要用。

注意：这是"标签定义"层面的泄漏修正。评估口径（不要在模型自己的训练期上评估、
超参搜索目标函数要用样本外指标）不在本模块范围内。
"""

import math

import numpy as np
import pandas as pd

LABEL_SIGMA_MODES = ("expanding", "full")
DEFAULT_SIGMA_MODE = "expanding"
LABEL_UP, LABEL_FLAT, LABEL_DOWN = 0, 1, 2
FALLBACK_THRESHOLD = 0.015
THRESHOLD_MIN, THRESHOLD_MAX = 0.005, 0.10
MIN_SIGMA_OBS = 20


def compute_label(s, period, label_type, vol_k, sigma_mode=DEFAULT_SIGMA_MODE):
    """计算标签：future_return = source[t+N]/source[t] - 1

    classification（自适应三分类）:
        threshold = vol_k × σ × √N
        future >  +threshold → 0 (UP)
        future <  -threshold → 2 (DOWN)
        其余                    → 1 (FLAT)
    regression: label = future_return

    sigma_mode:
        "expanding"（默认）—— 第 t 行只用 [0, t] 的对数收益估 σ，因果、无泄漏
        "full" —— 全序列常数 σ（旧行为）

    s 可以是 numpy 数组或 pd.Series；返回的 label / future 保留 s 的 index，便于按时间对齐。
    """
    s = pd.Series(s) if not isinstance(s, pd.Series) else s.astype(float)
    future = s.shift(-period) / s - 1.0
    if label_type != "classification":
        return future.copy(), future

    log_ret = np.log(s / s.shift(1)).replace([np.inf, -np.inf], np.nan)

    if sigma_mode == "full":
        lr = log_ret.dropna()
        sigma = float(lr.std()) if len(lr) >= MIN_SIGMA_OBS else 0.0
        if sigma <= 1e-12:
            threshold = FALLBACK_THRESHOLD
        else:
            threshold = float(np.clip(vol_k * sigma * math.sqrt(period),
                                      THRESHOLD_MIN, THRESHOLD_MAX))
    else:
        # 因果口径：第 t 行只用 [0, t] 的收益估 σ
        sigma = log_ret.expanding(min_periods=MIN_SIGMA_OBS).std()
        sigma = sigma.ffill().fillna(0.0)
        threshold = (vol_k * sigma * math.sqrt(period)).clip(THRESHOLD_MIN, THRESHOLD_MAX)
        threshold = threshold.where(sigma > 1e-12, FALLBACK_THRESHOLD)

    label = pd.Series(np.nan, index=s.index)
    label[future > threshold] = LABEL_UP
    label[future < -threshold] = LABEL_DOWN
    label[(future >= -threshold) & (future <= threshold)] = LABEL_FLAT
    return label, future


def add_label_args(parser):
    """给 argparse 挂上标签相关参数，两个训练脚本共用同一套默认值与口径"""
    parser.add_argument("--label-source", default="",
                        help="标签来源列名（vector 模式必填，matrix 模式自动从各标的 close 计算）")
    parser.add_argument("--label-period", type=int, default=5, help="未来收益周期 N")
    parser.add_argument("--label-type", choices=["classification", "regression"],
                        default="classification")
    parser.add_argument("--label-shape", choices=["vector", "matrix"], default="matrix",
                        help="vector=单列标签源；matrix=每个标的按自己的 close 各自算标签")
    parser.add_argument("--vol-k", type=float, default=0.5,
                        help="自适应阈值系数 vol_k (threshold = vol_k × σ × √N)")
    parser.add_argument("--label-sigma-mode", choices=list(LABEL_SIGMA_MODES),
                        default=DEFAULT_SIGMA_MODE,
                        help="标签阈值的波动率口径: expanding=逐行因果(默认, 无泄漏); "
                             "full=全序列常数(旧行为, 会把未来波动率带进标签定义)")
    return parser


def label_meta(args):
    """从 args 抽出标签配置，写进 meta.json 便于追溯"""
    return {
        "source": getattr(args, "label_source", ""),
        "period": getattr(args, "label_period", 5),
        "type": getattr(args, "label_type", "classification"),
        "shape": getattr(args, "label_shape", "matrix"),
        "vol_k": getattr(args, "vol_k", 0.5),
        "sigma_mode": getattr(args, "label_sigma_mode", DEFAULT_SIGMA_MODE),
    }
