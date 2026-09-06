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
// CubicSpline — 自然三次样条（double x 坐标）
// ═══════════════════════════════════════════════════════════════════

void CubicSpline::fit(const Vector<double>& xs, const Vector<double>& ys) {
    size_t n = xs.size();
    if (n < 2) return;
    if (n != ys.size()) return;

    _x = xs;
    int nm1 = static_cast<int>(n) - 1;

    // 步长
    Vector<double> h(nm1);
    for (int i = 0; i < nm1; ++i)
        h[i] = xs[i + 1] - xs[i];

    // 求解三对角方程组 → 二阶导数 M
    Vector<double> alpha(nm1);
    for (int i = 1; i < nm1; ++i) {
        alpha[i] = (3.0 / h[i]) * (ys[i + 1] - ys[i])
                 - (3.0 / h[i - 1]) * (ys[i] - ys[i - 1]);
    }

    Vector<double> l(n), mu(n), z(n), M(n);
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
    if (_a.empty()) return 0.0;
    int seg = findSegment(x);
    double dx = x - _x[seg];
    return _a[seg] + _b[seg] * dx + _c[seg] * dx * dx + _d[seg] * dx * dx * dx;
}

Vector<double> CubicSpline::eval(const Vector<double>& xs) const {
    Vector<double> out(xs.size());
    for (size_t i = 0; i < xs.size(); ++i)
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
    _splines_by_expiry.clear();
    _expiry_list.clear();

    // 按 expiry_days 分组
    std::map<int, Vector<std::pair<double, double>>> grouped;  // expiry → [(strike, iv)]
    for (auto& p : points) {
        grouped[p.expiry_days].emplace_back(p.strike, p.iv);
    }

    for (auto& [expiry, pairs] : grouped) {
        // 按 strike 排序
        std::sort(pairs.begin(), pairs.end());

        // 去重（同一 strike 取均值）
        Vector<double> strikes, ivs;
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (!strikes.empty() && std::abs(pairs[i].first - strikes.back()) < 1e-6) {
                ivs.back() = (ivs.back() + pairs[i].second) / 2.0;
            } else {
                strikes.push_back(pairs[i].first);
                ivs.push_back(pairs[i].second);
            }
        }

        if (strikes.size() >= 2) {
            CubicSpline spline;
            spline.fit(strikes, ivs);
            _splines_by_expiry[expiry] = std::move(spline);
            _expiry_list.push_back(expiry);
        } else if (strikes.size() == 1) {
            // 单点: 构建常数样条（退化情况）
            // 用两个相同 y 值的点构造
            Vector<double> xs = {strikes[0] - 1.0, strikes[0] + 1.0};
            Vector<double> ys = {ivs[0], ivs[0]};
            CubicSpline spline;
            spline.fit(xs, ys);
            _splines_by_expiry[expiry] = std::move(spline);
            _expiry_list.push_back(expiry);
        }
    }
}

double IVSurface::interpolate(double strike, int expiry_days) const {
    if (_expiry_list.empty()) return 0.0;

    // 沿 expiry 维度找相邻两个
    auto it = std::lower_bound(_expiry_list.begin(), _expiry_list.end(), expiry_days);

    if (it == _expiry_list.end()) {
        // 超出最大 expiry: 用最近 expiry 的样条
        int nearest = _expiry_list.back();
        return _splines_by_expiry.at(nearest).eval(strike);
    }
    if (it == _expiry_list.begin()) {
        // 小于最小 expiry: 用最近 expiry 的样条
        int nearest = _expiry_list.front();
        return _splines_by_expiry.at(nearest).eval(strike);
    }

    int exp_hi = *it;
    int exp_lo = *(it - 1);

    double iv_lo = _splines_by_expiry.at(exp_lo).eval(strike);
    double iv_hi = _splines_by_expiry.at(exp_hi).eval(strike);

    // 线性插值
    double t = static_cast<double>(expiry_days - exp_lo) / (exp_hi - exp_lo);
    return iv_lo + t * (iv_hi - iv_lo);
}

Vector<Vector<double>> IVSurface::generateSurface(
    const Vector<double>& strikes,
    const Vector<int>& expiry_days_list) const {

    Vector<Vector<double>> surface(expiry_days_list.size());
    for (size_t i = 0; i < expiry_days_list.size(); ++i) {
        surface[i].resize(strikes.size());
        for (size_t j = 0; j < strikes.size(); ++j) {
            surface[i][j] = interpolate(strikes[j], expiry_days_list[i]);
        }
    }
    return surface;
}
