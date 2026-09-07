#include "Derivative/IVSurface.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════════════
//  computeIVFromPrice — Newton-Raphson 反算隐含波动率
// ═══════════════════════════════════════════════════════════════════

static double bsNormCDF(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

static double bsPrice(double S, double K, double T, double r, double sigma, bool is_call) {
    if (T <= 0 || sigma <= 0 || S <= 0) return 0.0;
    double sqrtT = std::sqrt(T);
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;
    if (is_call)
        return S * bsNormCDF(d1) - K * std::exp(-r * T) * bsNormCDF(d2);
    else
        return K * std::exp(-r * T) * bsNormCDF(-d2) - S * bsNormCDF(-d1);
}

static double bsVega(double S, double K, double T, double r, double sigma) {
    if (T <= 0 || sigma <= 0 || S <= 0) return 0.0;
    double sqrtT = std::sqrt(T);
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double nd1 = std::exp(-0.5 * d1 * d1) / std::sqrt(2.0 * std::numbers::pi);
    return S * nd1 * sqrtT;
}

double computeIVFromPrice(double price, double S, double K, double T, double r, bool is_call) {
    if (price <= 0 || S <= 0 || K <= 0 || T <= 0) return 0.0;

    double intrinsic = is_call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
    if (price < intrinsic - 1e-8) return 0.0;

    double sigma = 0.3;
    for (int i = 0; i < 100; ++i) {
        double theo = bsPrice(S, K, T, r, sigma, is_call);
        double vega = bsVega(S, K, T, r, sigma);
        if (vega < 1e-12) break;
        double diff = theo - price;
        if (std::abs(diff) < 1e-8) return sigma;
        sigma -= diff / vega;
        if (sigma <= 0) sigma = 0.01;
        if (sigma > 10.0) sigma = 10.0;
    }
    return sigma;
}

// ═══════════════════════════════════════════════════════════════════
// CubicSpline — 自然三次样条（double x 坐标，Eigen 内部计算）
// ═══════════════════════════════════════════════════════════════════

void CubicSpline::fit(const Eigen::VectorXd& xs, const Eigen::VectorXd& ys) {
    Eigen::Index n = xs.size();
    if (n < 2) return;
    if (n != ys.size()) return;

    _x = xs;
    int nm1 = static_cast<int>(n) - 1;

    // 步长
    Eigen::VectorXd h(nm1);
    for (int i = 0; i < nm1; ++i)
        h[i] = xs[i + 1] - xs[i];

    // 求解三对角方程组 → 二阶导数 M
    Eigen::VectorXd alpha(nm1);
    for (int i = 1; i < nm1; ++i) {
        alpha[i] = (3.0 / h[i]) * (ys[i + 1] - ys[i])
                 - (3.0 / h[i - 1]) * (ys[i] - ys[i - 1]);
    }

    Eigen::VectorXd l(n), mu(n), z(n), M(n);
    l[0] = 1.0; mu[0] = 0.0; z[0] = 0.0;

    for (int i = 1; i < nm1; ++i) {
        l[i] = 2.0 * (xs[i + 1] - xs[i - 1]) - h[i - 1] * mu[i - 1];
        if (std::abs(l[i]) < 1e-15) l[i] = 1e-15;
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }

    l[nm1] = 1.0; z[nm1] = 0.0; M[nm1] = 0.0;  // 自然边界
    for (int j = nm1 - 1; j >= 0; --j)
        M[j] = z[j] - mu[j] * M[j + 1];

    // 转换为 a + b*(x-xi) + c*(x-xi)^2 + d*(x-xi)^3 系数
    _a.resize(nm1);
    _b.resize(nm1);
    _c.resize(nm1);
    _d.resize(nm1);

    for (int i = 0; i < nm1; ++i) {
        _a[i] = ys[i];
        _c[i] = M[i] / 2.0;
        _d[i] = (M[i + 1] - M[i]) / (6.0 * h[i]);
        _b[i] = (ys[i + 1] - ys[i]) / h[i] - h[i] * (2.0 * M[i] + M[i + 1]) / 6.0;
    }
}

double CubicSpline::eval(double x) const {
    if (_a.size() == 0) return 0.0;
    int seg = findSegment(x);
    double dx = x - _x[seg];
    return _a[seg] + _b[seg] * dx + _c[seg] * dx * dx + _d[seg] * dx * dx * dx;
}

Eigen::VectorXd CubicSpline::eval(const Eigen::VectorXd& xs) const {
    Eigen::VectorXd out(xs.size());
    for (Eigen::Index i = 0; i < xs.size(); ++i)
        out[i] = eval(xs[i]);
    return out;
}

int CubicSpline::findSegment(double x) const {
    int n = static_cast<int>(_x.size()) - 1;
    if (x <= _x[0]) return 0;
    if (x >= _x[n]) return n - 1;
    // 二分查找
    int lo = 0, hi = n;
    while (lo < hi - 1) {
        int mid = (lo + hi) / 2;
        if (_x[mid] <= x) lo = mid;
        else hi = mid;
    }
    return lo;
}

// ═══════════════════════════════════════════════════════════════════
// IVSurface
// ═══════════════════════════════════════════════════════════════════

void IVSurface::build(const Vector<IVPoint>& points) {
    _points = points;
    _splines.clear();
    _splines_merged.clear();
    _expiry_list.clear();

    // 按 (expiry_days, opt_type) 分组
    std::map<std::pair<int, OptionType>, Vector<std::pair<double, double>>> grouped;
    // 同时构建合并样条(向后兼容)
    std::map<int, Vector<std::pair<double, double>>> grouped_merged;

    for (auto& p : points) {
        grouped[{p.expiry_days, p.opt_type}].emplace_back(p.strike, p.iv);
        grouped_merged[p.expiry_days].emplace_back(p.strike, p.iv);
    }

    // 构建分离样条 (call/put 分开)
    for (auto& [key, pairs] : grouped) {
        // 按 strike 排序
        std::sort(pairs.begin(), pairs.end());

        // 去重（同一 strike 同一 opt_type 取均值）
        Vector<double> strikes_v, ivs_v;
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (!strikes_v.empty() && std::abs(pairs[i].first - strikes_v.back()) < 1e-6) {
                ivs_v.back() = (ivs_v.back() + pairs[i].second) / 2.0;
            } else {
                strikes_v.push_back(pairs[i].first);
                ivs_v.push_back(pairs[i].second);
            }
        }

        // 转 Eigen 用于样条拟合
        auto toEigen = [](const Vector<double>& v) {
            Eigen::VectorXd e(v.size());
            for (size_t i = 0; i < v.size(); ++i) e[i] = v[i];
            return e;
        };

        if (strikes_v.size() >= 2) {
            CubicSpline spline;
            spline.fit(toEigen(strikes_v), toEigen(ivs_v));
            _splines[key] = std::move(spline);
            if (std::find(_expiry_list.begin(), _expiry_list.end(), key.first) == _expiry_list.end()) {
                _expiry_list.push_back(key.first);
            }
        } else if (strikes_v.size() == 1) {
            Eigen::VectorXd xs(2), ys(2);
            xs << strikes_v[0] - 1.0, strikes_v[0] + 1.0;
            ys << ivs_v[0], ivs_v[0];
            CubicSpline spline;
            spline.fit(xs, ys);
            _splines[key] = std::move(spline);
            if (std::find(_expiry_list.begin(), _expiry_list.end(), key.first) == _expiry_list.end()) {
                _expiry_list.push_back(key.first);
            }
        }
    }

    // 构建合并样条 (向后兼容)
    for (auto& [expiry, pairs] : grouped_merged) {
        std::sort(pairs.begin(), pairs.end());
        Vector<double> strikes_v, ivs_v;
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (!strikes_v.empty() && std::abs(pairs[i].first - strikes_v.back()) < 1e-6) {
                ivs_v.back() = (ivs_v.back() + pairs[i].second) / 2.0;
            } else {
                strikes_v.push_back(pairs[i].first);
                ivs_v.push_back(pairs[i].second);
            }
        }

        auto toEigen = [](const Vector<double>& v) {
            Eigen::VectorXd e(v.size());
            for (size_t i = 0; i < v.size(); ++i) e[i] = v[i];
            return e;
        };

        if (strikes_v.size() >= 2) {
            CubicSpline spline;
            spline.fit(toEigen(strikes_v), toEigen(ivs_v));
            _splines_merged[expiry] = std::move(spline);
        } else if (strikes_v.size() == 1) {
            Eigen::VectorXd xs(2), ys(2);
            xs << strikes_v[0] - 1.0, strikes_v[0] + 1.0;
            ys << ivs_v[0], ivs_v[0];
            CubicSpline spline;
            spline.fit(xs, ys);
            _splines_merged[expiry] = std::move(spline);
        }
    }

    std::sort(_expiry_list.begin(), _expiry_list.end());
}

double IVSurface::interpolate(double strike, int expiry_days, OptionType opt_type) const {
    if (_expiry_list.empty()) return 0.0;

    // 选择样条集合
    const CubicSpline* spline_lo = nullptr;
    const CubicSpline* spline_hi = nullptr;
    int exp_lo = 0, exp_hi = 0;

    if (opt_type == OptionType::Unknown) {
        // 向后兼容: 使用合并样条
        auto it = std::lower_bound(_expiry_list.begin(), _expiry_list.end(), expiry_days);
        if (it == _expiry_list.end()) {
            int nearest = _expiry_list.back();
            auto sit = _splines_merged.find(nearest);
            return (sit != _splines_merged.end()) ? sit->second.eval(strike) : 0.0;
        }
        if (it == _expiry_list.begin()) {
            int nearest = _expiry_list.front();
            auto sit = _splines_merged.find(nearest);
            return (sit != _splines_merged.end()) ? sit->second.eval(strike) : 0.0;
        }
        exp_hi = *it;
        exp_lo = *(it - 1);
        auto sit_lo = _splines_merged.find(exp_lo);
        auto sit_hi = _splines_merged.find(exp_hi);
        if (sit_lo != _splines_merged.end()) spline_lo = &sit_lo->second;
        if (sit_hi != _splines_merged.end()) spline_hi = &sit_hi->second;
    } else {
        // 分离样条: 按 opt_type 查找
        auto it = std::lower_bound(_expiry_list.begin(), _expiry_list.end(), expiry_days);
        if (it == _expiry_list.end()) {
            int nearest = _expiry_list.back();
            auto sit = _splines.find({nearest, opt_type});
            return (sit != _splines.end()) ? sit->second.eval(strike) : 0.0;
        }
        if (it == _expiry_list.begin()) {
            int nearest = _expiry_list.front();
            auto sit = _splines.find({nearest, opt_type});
            return (sit != _splines.end()) ? sit->second.eval(strike) : 0.0;
        }
        exp_hi = *it;
        exp_lo = *(it - 1);
        auto sit_lo = _splines.find({exp_lo, opt_type});
        auto sit_hi = _splines.find({exp_hi, opt_type});
        if (sit_lo != _splines.end()) spline_lo = &sit_lo->second;
        if (sit_hi != _splines.end()) spline_hi = &sit_hi->second;
    }

    if (!spline_lo && !spline_hi) return 0.0;
    if (!spline_lo) return spline_hi->eval(strike);
    if (!spline_hi) return spline_lo->eval(strike);

    double iv_lo = spline_lo->eval(strike);
    double iv_hi = spline_hi->eval(strike);
    double t = static_cast<double>(expiry_days - exp_lo) / (exp_hi - exp_lo);
    return iv_lo + t * (iv_hi - iv_lo);
}

std::pair<Vector<Vector<double>>, Vector<Vector<double>>> IVSurface::generateSurface(
    const Vector<double>& strikes,
    const Vector<int>& expiry_days_list) const {

    Vector<Vector<double>> call_surface(expiry_days_list.size());
    Vector<Vector<double>> put_surface(expiry_days_list.size());

    // 检查是否有 call/put 分离数据
    bool has_call = false, has_put = false;
    for (auto& [key, _] : _splines) {
        if (key.second == OptionType::Call) has_call = true;
        if (key.second == OptionType::Put) has_put = true;
    }

    for (size_t i = 0; i < expiry_days_list.size(); ++i) {
        call_surface[i].resize(strikes.size());
        put_surface[i].resize(strikes.size());
        for (size_t j = 0; j < strikes.size(); ++j) {
            if (has_call) {
                call_surface[i][j] = interpolate(strikes[j], expiry_days_list[i], OptionType::Call);
            } else {
                call_surface[i][j] = interpolate(strikes[j], expiry_days_list[i], OptionType::Unknown);
            }
            if (has_put) {
                put_surface[i][j] = interpolate(strikes[j], expiry_days_list[i], OptionType::Put);
            }
        }
    }

    return {std::move(call_surface), std::move(put_surface)};
}
