#include "Algorithms/MVO/MvoEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mvo {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// EWMA 衰减系数（λ = 1 − 0.5^(1/halflife)）
double ewmaLambda(double halflife) {
    return 1.0 - std::pow(0.5, 1.0 / std::max(halflife, 1.0));
}

}  // namespace

// ─────────────────────────────────────────────────────────────
// ScalarKalman
// ─────────────────────────────────────────────────────────────

void MeanEstimator::ScalarKalman::init(double prior_mean, double prior_var) {
    State x0;
    x0 << prior_mean;
    _kf.init(x0);

    Kalman::Covariance<State> P;
    P << prior_var;
    _kf.setCovariance(P);
}

void MeanEstimator::ScalarKalman::update(double z, double r, double q) {
    // 系统模型协方差 = 过程噪声 Q
    Kalman::Covariance<State> Q;
    Q << q;
    _sys.setCovariance(Q);
    _kf.predict(_sys);

    // 观测噪声 R 逐日可变 → 每次观测前重设
    if (std::isfinite(z) && std::isfinite(r) && r > 0.0) {
        Kalman::Covariance<Meas> R;
        R << r;
        _meas.setCovariance(R);

        Meas zz;
        zz << z;
        _kf.update(_meas, zz);
    }
}

// ─────────────────────────────────────────────────────────────
// MeanEstimator
// ─────────────────────────────────────────────────────────────

MeanEstimator::MeanEstimator(const MeanEstimatorConfig& cfg)
    : _cfg(cfg) {
    reset();
}

void MeanEstimator::setConfig(const MeanEstimatorConfig& cfg) {
    _cfg = cfg;
    reset();
}

void MeanEstimator::reset() {
    _n = 0;
    _consumed = 0;
    _filters.clear();
    _ewmaMean.clear();
    _factorEwmaMean = kNaN;
    _last = MeanEstimate();
    _lastPanel.resize(0, 0);

    _factor.init(_cfg._prior_mean, _cfg._prior_var);
}

double MeanEstimator::rollingBeta(int t, int i,
                                  const Eigen::MatrixXd& hist,
                                  const std::vector<double>& factor) const {
    const int lo = std::max(0, t - _cfg._factor_window + 1);
    double num = 0.0;
    double den = 0.0;
    int    cnt = 0;

    for (int s = lo; s <= t; ++s) {
        const double fs = factor[static_cast<size_t>(s)];
        const double ri = hist(s, i);
        if (!std::isfinite(fs) || !std::isfinite(ri)) continue;
        num += fs * ri;
        den += fs * fs;
        ++cnt;
    }

    // 样本不足或因子退化 → beta 保持 1.0（与 lab 的 np.ones 初值一致）
    if (cnt < _cfg._factor_min_obs || den <= 1e-16) {
        return 1.0;
    }

    const double beta = num / den;
    return (1.0 - _cfg._beta_shrink) * beta + _cfg._beta_shrink;
}

double MeanEstimator::rollingSampleMean(int t, int i,
                                        const Eigen::MatrixXd& hist) const {
    const int lo = std::max(0, t - _cfg._sample_window + 1);
    double sum = 0.0;
    int    cnt = 0;
    for (int s = lo; s <= t; ++s) {
        const double v = hist(s, i);
        if (!std::isfinite(v)) continue;
        sum += v;
        ++cnt;
    }
    return (cnt > 0) ? (sum / static_cast<double>(cnt)) : 0.0;
}

MeanEstimate MeanEstimator::update(const Eigen::MatrixXd& returns_history) {
    const int T = static_cast<int>(returns_history.rows());
    const int n = static_cast<int>(returns_history.cols());

    if (n <= 0 || T <= 0) {
        _last._note = "empty returns panel";
        return _last;
    }

    // 已消费部分的数值是否被改动（右对齐错位 / 历史修正）。
    // 不改的话滤波器会带着错位的历史继续递推 —— 错得很隐蔽。
    bool changed = false;
    if (n == _n && _consumed > 0 && _lastPanel.rows() >= _consumed &&
        T >= _consumed) {
        for (int t = 0; t < _consumed && !changed; ++t) {
            for (int i = 0; i < n; ++i) {
                const double a = returns_history(t, i);
                const double b = _lastPanel(t, i);
                if (a != b && !(std::isnan(a) && std::isnan(b))) {
                    changed = true;
                    break;
                }
            }
        }
    }

    // 历史被截断 / 资产数变化 / 内容被修正 → 从头重算
    if (n != _n || T < _consumed || changed) {
        reset();
    }

    if (n != _n) {
        _n = n;
        _filters.resize(static_cast<size_t>(n));
        _ewmaMean.assign(static_cast<size_t>(n), kNaN);
        for (int i = 0; i < n; ++i) {
            _filters[static_cast<size_t>(i)]._fast.init(_cfg._prior_mean, _cfg._prior_var);
        }
    }

    // 没有新样本 → 幂等返回上次的估计
    if (T == _consumed) {
        return _last;
    }

    MeanEstimate out;
    out._mu_total.assign(static_cast<size_t>(n), 0.0);
    out._mu_alpha.assign(static_cast<size_t>(n), 0.0);
    out._t_stat.assign(static_cast<size_t>(n), 0.0);
    out._betas.assign(static_cast<size_t>(n), 1.0);

    if (_cfg._method == MeanMethod::Sample) {
        // ── 对照组：滚动样本均值（不做因子剥离）──
        for (int i = 0; i < n; ++i) {
            out._mu_alpha[static_cast<size_t>(i)] =
                rollingSampleMean(T - 1, i, returns_history);
            out._t_stat[static_cast<size_t>(i)] = kNaN;
        }
        out._factor_mu = 0.0;
        _consumed = T;
        _lastPanel = returns_history;

        for (int i = 0; i < n; ++i) {
            out._mu_total[static_cast<size_t>(i)] =
                out._mu_alpha[static_cast<size_t>(i)] * _cfg._trading_days;
        }
        out._samples = _consumed;
        out._valid = true;
        out._note = "sample mean";
        _last = out;
        return _last;
    }

    // 市场代理 f_t = 当天全体标的的等权收益（逐行忽略缺失）
    std::vector<double> factor(static_cast<size_t>(T), kNaN);
    for (int t = 0; t < T; ++t) {
        double sum = 0.0;
        int    cnt = 0;
        for (int i = 0; i < n; ++i) {
            const double r = returns_history(t, i);
            if (!std::isfinite(r)) continue;
            sum += r;
            ++cnt;
        }
        if (cnt > 0) {
            factor[static_cast<size_t>(t)] = sum / static_cast<double>(cnt);
        }
    }

    const double lam = ewmaLambda(_cfg._obs_var_halflife);

    // ── 推进新增的时间点 ──
    for (int t = _consumed; t < T; ++t) {
        const double f = factor[static_cast<size_t>(t)];

        for (int i = 0; i < n; ++i) {
            // 滚动 beta → 残差收益 alpha = r − beta·f
            const double beta = rollingBeta(t, i, returns_history, factor);
            const double r = returns_history(t, i);
            const double alpha = (std::isfinite(r) && std::isfinite(f))
                                   ? r - beta * f
                                   : kNaN;

            // 观测噪声 R：残差收益相对 EWMA 一阶矩的增量平方偏差。
            // 注意口径：lab 的 pf/kalman.py ewma_variance() 返回的是**瞬时**
            // 增量平方偏差（不是 docstring 里写的 EWMA 方差），这里按代码对齐。
            const double prev = _ewmaMean[static_cast<size_t>(i)];
            double dev = 0.0;
            if (std::isfinite(alpha)) {
                const double ref = std::isfinite(prev) ? prev : alpha;
                dev = (alpha - ref) * (alpha - ref);
                double st = _ewmaMean[static_cast<size_t>(i)];
                if (!std::isfinite(st)) {
                    st = alpha;
                }
                _ewmaMean[static_cast<size_t>(i)] = (1.0 - lam) * st + lam * alpha;
            }
            const double obs_var =
                std::isfinite(alpha) ? std::max(dev, 1e-12) : kNaN;

            // 双精度卡尔曼：快慢两个滤波器 + 逆方差加权融合
            FilterPair& fp = _filters[static_cast<size_t>(i)];
            fp._fast.update(alpha, obs_var, _cfg._q_fast);

            double mu = 0.0;
            double pvar = 0.0;
            if (_cfg._double_accuracy) {
                fp._slow.update(alpha, obs_var, _cfg._q_slow);
                const double pf = std::max(fp._fast.var(), 1e-18);
                const double ps = std::max(fp._slow.var(), 1e-18);
                const double inv = 1.0 / pf + 1.0 / ps;
                const double w = (1.0 / pf) / inv;
                mu = w * fp._fast.mean() + (1.0 - w) * fp._slow.mean();
                pvar = 1.0 / inv;
            } else {
                mu = fp._fast.mean();
                pvar = fp._fast.var();
            }

            out._mu_alpha[static_cast<size_t>(i)] = mu;
            out._t_stat[static_cast<size_t>(i)] =
                mu / std::sqrt(std::max(pvar, 1e-18));
            out._betas[static_cast<size_t>(i)] = beta;
        }

        // 市场因子自身用**单个**滤波器（过程噪声取快滤波器的 10 倍），
        // 与 lab 的 pf/kalman.py 一致 —— 因子序列只有一个自由度，双精度
        // 融合在这里没有意义。
        double fvar = 0.0;
        {
            const double prev = _factorEwmaMean;
            if (std::isfinite(f)) {
                const double ref = std::isfinite(prev) ? prev : f;
                fvar = std::max((f - ref) * (f - ref), 1e-12);
                double st = _factorEwmaMean;
                if (!std::isfinite(st)) st = f;
                _factorEwmaMean = (1.0 - lam) * st + lam * f;
            } else {
                fvar = kNaN;
            }
        }
        _factor.update(f, fvar, _cfg._q_fast * 10.0);
        out._factor_mu = _factor.mean();
    }

    _consumed = T;
    _lastPanel = returns_history;

    // 总收益口径：alpha + beta·市场因子均值（MVO 里"持有现金"是备选方案，
    // 与现金比较要用总收益而不是纯 alpha）
    for (int i = 0; i < n; ++i) {
        const double alpha = out._mu_alpha[static_cast<size_t>(i)];
        const double beta = out._betas[static_cast<size_t>(i)];
        out._mu_total[static_cast<size_t>(i)] =
            (alpha + beta * out._factor_mu) * _cfg._trading_days;
    }

    out._samples = _consumed;
    out._valid = _consumed > 0;
    if (!out._valid) {
        out._note = "no samples consumed";
    }

    _last = out;
    return _last;
}

}  // namespace mvo
