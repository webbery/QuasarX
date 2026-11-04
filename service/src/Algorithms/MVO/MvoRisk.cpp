#include "Algorithms/MVO/MvoRisk.h"
#include "Util/finance.h"

#include <algorithm>
#include <cmath>

namespace mvo {

namespace {

/// EWMA 衰减系数（λ = 1 − 0.5^(1/halflife)）
double ewmaLambda(double halflife) {
    return 1.0 - std::pow(0.5, 1.0 / std::max(halflife, 1.0));
}

}  // namespace

Eigen::MatrixXd shrinkConstantCorrelation(const Eigen::MatrixXd& cov, double delta) {
    if (delta <= 0.0 || cov.rows() == 0) {
        return cov;
    }

    const Eigen::Index n = cov.rows();
    const Eigen::VectorXd sd =
        (cov.diagonal().cwiseMax(0.0)).cwiseSqrt().cwiseMax(1e-9);

    // 平均成对相关 ρ̄（只对 i<j 求和；见头文件里关于 lab 2× 口径的说明）
    double sum_corr = 0.0;
    Eigen::Index cnt = 0;
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = i + 1; j < n; ++j) {
            const double denom = sd(i) * sd(j);
            if (denom > 0.0) {
                sum_corr += cov(i, j) / denom;
                ++cnt;
            }
        }
    }
    double rho = (cnt > 0) ? (sum_corr / static_cast<double>(cnt)) : 0.0;
    rho = std::clamp(rho, -0.95, 0.95);

    // 目标：Σ_target = diag(sd)·C̄·diag(sd)，C̄ = (1−ρ)I + ρ·11ᵀ
    Eigen::MatrixXd target = Eigen::MatrixXd::Zero(n, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = 0; j < n; ++j) {
            const double corr = (i == j) ? 1.0 : rho;
            target(i, j) = sd(i) * sd(j) * corr;
        }
    }

    return (1.0 - delta) * cov + delta * target;
}

Eigen::MatrixXd nearestPsd(const Eigen::MatrixXd& cov, double floor) {
    if (cov.rows() == 0) {
        return cov;
    }

    Eigen::MatrixXd sym = 0.5 * (cov + cov.transpose());
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(sym);
    if (solver.info() != Eigen::Success) {
        return sym;
    }

    Eigen::VectorXd vals = solver.eigenvalues().cwiseMax(floor);
    Eigen::MatrixXd out = solver.eigenvectors() * vals.asDiagonal() *
                          solver.eigenvectors().transpose();
    return 0.5 * (out + out.transpose());
}

RiskModel::RiskModel(const RiskModelConfig& cfg)
    : _cfg(cfg) {
    reset();
}

void RiskModel::setConfig(const RiskModelConfig& cfg) {
    _cfg = cfg;
    reset();
}

void RiskModel::reset() {
    _mean.resize(0);
    _cov.resize(0, 0);
    _covAnnualized.resize(0, 0);
    _consumed = 0;
    _n = 0;
    _firstFrame = true;
}

Eigen::MatrixXd RiskModel::sanitize(const Eigen::MatrixXd& returns_history) {
    Eigen::MatrixXd x = returns_history;
    for (Eigen::Index t = 0; t < x.rows(); ++t) {
        for (Eigen::Index i = 0; i < x.cols(); ++i) {
            if (!std::isfinite(x(t, i))) {
                x(t, i) = 0.0;
            }
        }
    }
    return x;
}

Eigen::MatrixXd RiskModel::sampleCovariance(const Eigen::MatrixXd& x) {
    const Eigen::Index T = x.rows();
    const Eigen::Index n = x.cols();
    if (T < 2 || n <= 0) {
        return Eigen::MatrixXd::Zero(n, n);
    }
    const Eigen::RowVectorXd mean = x.colwise().mean();
    const Eigen::MatrixXd xc = x.rowwise() - mean;
    return (xc.transpose() * xc) / static_cast<double>(T - 1);
}

Eigen::MatrixXd RiskModel::update(const Eigen::MatrixXd& returns_history) {
    const Eigen::Index n = returns_history.cols();
    const Eigen::Index T = returns_history.rows();
    if (n <= 0 || T <= 0) {
        return _covAnnualized;
    }

    const double ann = static_cast<double>(_cfg._trading_days);

    // ── 非 EWMA 路径：每次从面板整体重算 ──
    if (_cfg._method == CovMethod::Sample || _cfg._method == CovMethod::LedoitWolf) {
        const Eigen::MatrixXd x = sanitize(returns_history);
        Eigen::MatrixXd cov;
        if (_cfg._method == CovMethod::Sample) {
            cov = sampleCovariance(x);
        } else {
            // finance::ledoitWolfShrinkage 约定：N 行 × T 列（行=标的）
            const finance::LedoitWolfResult lw =
                finance::ledoitWolfShrinkage(x.transpose());
            cov = (lw._covariance.rows() == n) ? lw._covariance
                                               : sampleCovariance(x);
        }
        cov = nearestPsd(cov, _cfg._psd_floor);
        _covAnnualized = cov * ann;

        _consumed = static_cast<int>(T);
        _n = static_cast<int>(n);
        return _covAnnualized;
    }

    // ── 默认路径：EWMA 递推（逐行推进）──
    if (_n != static_cast<int>(n) || static_cast<int>(T) < _consumed) {
        reset();
        _n = static_cast<int>(n);
    }

    if (_consumed == 0) {
        _mean = Eigen::VectorXd::Zero(n);
        _cov = Eigen::MatrixXd::Zero(n, n);
        _firstFrame = true;
    }

    const double lam = ewmaLambda(_cfg._cov_halflife);

    for (Eigen::Index t = _consumed; t < T; ++t) {
        // 缺失值按 0 处理（等价于"当日该资产无变动"）
        Eigen::VectorXd x = Eigen::VectorXd::Zero(n);
        for (Eigen::Index i = 0; i < n; ++i) {
            const double r = returns_history(t, i);
            x(i) = std::isfinite(r) ? r : 0.0;
        }

        const Eigen::VectorXd dev = x - _mean;
        const Eigen::MatrixXd outer = dev * dev.transpose();

        if (_firstFrame) {
            // 首帧：mean = x，cov = dev·devᵀ（此时 dev = x，因为 mean 初值为 0）
            _mean = x;
            _cov = outer;
            _firstFrame = false;
        } else {
            _mean = (1.0 - lam) * _mean + lam * x;
            _cov = (1.0 - lam) * _cov + lam * outer;
        }
    }
    _consumed = static_cast<int>(T);

    Eigen::MatrixXd shrunk = shrinkConstantCorrelation(_cov, _cfg._shrinkage);
    shrunk = nearestPsd(shrunk, _cfg._psd_floor);
    _covAnnualized = shrunk * ann;

    return _covAnnualized;
}

}  // namespace mvo
