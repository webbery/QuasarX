#include "Derivative/OptionPricer.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <numeric>

// ── 标准正态分布工具 ──
namespace {

inline double normPDF(double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
}

inline double normCDF(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

} // anonymous namespace

namespace OptionPricer {

// ── 虚实度判定 ──
String classifyMoneyness(double S, double K, double tolerance) {
    double moneyness = (S - K) / K;
    if (std::abs(moneyness) <= tolerance) return "ATM";
    return moneyness > 0 ? "ITM" : "OTM";
}

// ── Black-Scholes 解析解 ──
BSResult blackScholes(double S, double K, double T, double sigma,
                      double r, double q, bool is_call) {
    BSResult res;
    if (T <= 1e-10 || sigma <= 1e-10) {
        // 到期或零波动率: 纯内在价值
        double intrinsic = is_call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        res.price = intrinsic;
        res.intrinsic_value = intrinsic;
        res.time_value = 0;
        res.delta = is_call ? (S > K ? 1.0 : 0.0) : (S < K ? -1.0 : 0.0);
        res.gamma = 0;
        res.theta = 0;
        res.vega = 0;
        res.rho = 0;
        res.moneyness = classifyMoneyness(S, K);
        return res;
    }

    double sqrtT = std::sqrt(T);
    double d1 = (std::log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;

    double nd1 = normPDF(d1);
    double Nd1 = normCDF(d1);
    double Nd2 = normCDF(d2);

    double eq = std::exp(-q * T);
    double er = std::exp(-r * T);

    if (is_call) {
        res.price = S * eq * Nd1 - K * er * Nd2;
        res.delta = eq * Nd1;
        res.theta = (-S * eq * nd1 * sigma / (2.0 * sqrtT)
                     - r * K * er * Nd2
                     + q * S * eq * Nd1) / 365.0;
        res.rho = K * T * er * Nd2 / 100.0;
    } else {
        res.price = K * er * normCDF(-d2) - S * eq * normCDF(-d1);
        res.delta = eq * (Nd1 - 1.0);
        res.theta = (-S * eq * nd1 * sigma / (2.0 * sqrtT)
                     + r * K * er * normCDF(-d2)
                     - q * S * eq * normCDF(-d1)) / 365.0;
        res.rho = -K * T * er * normCDF(-d2) / 100.0;
    }

    res.gamma = eq * nd1 / (S * sigma * sqrtT);
    res.vega = S * eq * nd1 * sqrtT / 100.0;

    res.intrinsic_value = is_call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
    res.time_value = res.price - res.intrinsic_value;
    res.moneyness = classifyMoneyness(S, K);
    return res;
}

// ── Monte Carlo ──
MCResult monteCarlo(double S, double K, double T, double sigma,
                    double r, double q, bool is_call,
                    int n_paths, int n_steps, uint64_t seed) {
    MCResult res;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);

    double dt = T / n_steps;
    double drift = (r - q - 0.5 * sigma * sigma) * dt;
    double vol = sigma * std::sqrt(dt);

    res.payoff_distribution.resize(n_paths);
    double sum = 0, sum2 = 0;

    for (int i = 0; i < n_paths; ++i) {
        double logS = std::log(S);
        for (int j = 0; j < n_steps; ++j) {
            logS += drift + vol * normal(rng);
        }
        double ST = std::exp(logS);
        double payoff = is_call ? std::max(ST - K, 0.0) : std::max(K - ST, 0.0);
        res.payoff_distribution[i] = payoff;
        sum += payoff;
        sum2 += payoff * payoff;
    }

    double mean = sum / n_paths;
    double variance = sum2 / n_paths - mean * mean;
    res.price = mean * std::exp(-r * T);
    res.std_error = std::sqrt(std::max(variance, 0.0) / n_paths) * std::exp(-r * T);
    return res;
}

// ── 二叉树 CRR ──
TreeResult binomialTree(double S, double K, double T, double sigma,
                        double r, double q, bool is_call,
                        int n_steps, bool is_american) {
    TreeResult res;
    double dt = T / n_steps;
    double u = std::exp(sigma * std::sqrt(dt));
    double d = 1.0 / u;
    double eq = std::exp((r - q) * dt);
    double p = (eq - d) / (u - d);
    double er = std::exp(-r * dt);

    // 终端节点价格
    Vector<double> prices(n_steps + 1);
    for (int i = 0; i <= n_steps; ++i) {
        double ST = S * std::pow(u, n_steps - i) * std::pow(d, i);
        prices[i] = is_call ? std::max(ST - K, 0.0) : std::max(K - ST, 0.0);
    }

    // 后向归纳
    for (int step = n_steps - 1; step >= 0; --step) {
        for (int i = 0; i <= step; ++i) {
            double continuation = er * (p * prices[i] + (1.0 - p) * prices[i + 1]);
            if (is_american) {
                double ST = S * std::pow(u, step - i) * std::pow(d, i);
                double exercise = is_call ? std::max(ST - K, 0.0) : std::max(K - ST, 0.0);
                prices[i] = std::max(continuation, exercise);
            } else {
                prices[i] = continuation;
            }
        }
    }

    res.price = prices[0];

    // 提前行权溢价 = 美式价格 - 欧式价格
    if (is_american) {
        // 重新计算欧式
        Vector<double> euro_prices(n_steps + 1);
        for (int i = 0; i <= n_steps; ++i) {
            double ST = S * std::pow(u, n_steps - i) * std::pow(d, i);
            euro_prices[i] = is_call ? std::max(ST - K, 0.0) : std::max(K - ST, 0.0);
        }
        for (int step = n_steps - 1; step >= 0; --step) {
            for (int i = 0; i <= step; ++i) {
                euro_prices[i] = er * (p * euro_prices[i] + (1.0 - p) * euro_prices[i + 1]);
            }
        }
        res.early_exercise_premium = res.price - euro_prices[0];
    }
    return res;
}

// ── 收益曲线 ──
Vector<PayoffPoint> generatePayoffCurve(
    double S, double K, double T, double sigma,
    double r, double q, bool is_call,
    int n_points, double range_pct) {

    Vector<PayoffPoint> curve(n_points);
    double lo = S * (1.0 - range_pct);
    double hi = S * (1.0 + range_pct);
    double step = (hi - lo) / (n_points - 1);

    for (int i = 0; i < n_points; ++i) {
        double spot = lo + i * step;
        // 到期收益
        curve[i].spot = spot;
        curve[i].payoff_at_expiry = is_call
            ? std::max(spot - K, 0.0)
            : std::max(K - spot, 0.0);
        // 当前理论价值（BSM）
        auto bs = blackScholes(spot, K, T, sigma, r, q, is_call);
        curve[i].payoff_now = bs.price;
    }
    return curve;
}

// ── 统一入口 ──
PricingResult price(const String& method,
                    double S, double K, double T, double sigma,
                    double r, double q, bool is_call, bool is_american,
                    int n_paths, int n_steps) {
    PricingResult res;

    if (method == "black_scholes") {
        auto bs = blackScholes(S, K, T, sigma, r, q, is_call);
        res.price = bs.price;
        res.intrinsic_value = bs.intrinsic_value;
        res.time_value = bs.time_value;
        res.moneyness = bs.moneyness;
        res.delta = bs.delta;
        res.gamma = bs.gamma;
        res.theta = bs.theta;
        res.vega = bs.vega;
        res.rho = bs.rho;
    } else if (method == "monte_carlo") {
        auto mc = monteCarlo(S, K, T, sigma, r, q, is_call, n_paths, n_steps);
        res.price = mc.price;
        res.mc_std_error = mc.std_error;
        res.mc_payoff_distribution = std::move(mc.payoff_distribution);
        res.intrinsic_value = is_call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        res.time_value = res.price - res.intrinsic_value;
        res.moneyness = classifyMoneyness(S, K);
        // MC 的 Greeks 用 BSM 有限差分近似（MC 本身不适合算 Greeks）
        double dS = S * 0.01;
        auto bs_up = blackScholes(S + dS, K, T, sigma, r, q, is_call);
        auto bs_dn = blackScholes(S - dS, K, T, sigma, r, q, is_call);
        res.delta = (bs_up.price - bs_dn.price) / (2.0 * dS);
        res.gamma = (bs_up.price - 2.0 * res.price + bs_dn.price) / (dS * dS);
        auto bs_vega = blackScholes(S, K, T, sigma + 0.01, r, q, is_call);
        res.vega = (bs_vega.price - res.price) / 0.01 / 100.0;
        double dT = 1.0 / 365.0;
        auto bs_theta = blackScholes(S, K, std::max(T - dT, 1e-10), sigma, r, q, is_call);
        res.theta = (bs_theta.price - res.price) / 365.0;
        auto bs_rho = blackScholes(S, K, T, sigma, r + 0.0001, q, is_call);
        res.rho = (bs_rho.price - res.price) / 0.0001 / 100.0;
    } else if (method == "binomial") {
        auto tree = binomialTree(S, K, T, sigma, r, q, is_call, n_steps, is_american);
        res.price = tree.price;
        res.early_exercise_premium = tree.early_exercise_premium;
        res.intrinsic_value = is_call ? std::max(S - K, 0.0) : std::max(K - S, 0.0);
        res.time_value = res.price - res.intrinsic_value;
        res.moneyness = classifyMoneyness(S, K);
        // 二叉树 Greeks 用有限差分
        double dS = S * 0.01;
        auto up = binomialTree(S + dS, K, T, sigma, r, q, is_call, n_steps, is_american);
        auto dn = binomialTree(S - dS, K, T, sigma, r, q, is_call, n_steps, is_american);
        res.delta = (up.price - dn.price) / (2.0 * dS);
        res.gamma = (up.price - 2.0 * res.price + dn.price) / (dS * dS);
        auto bs_sigma_up = binomialTree(S, K, T, sigma + 0.01, r, q, is_call, n_steps, is_american);
        res.vega = (bs_sigma_up.price - res.price) / 0.01 / 100.0;
        double dT = 1.0 / 365.0;
        auto bs_t = binomialTree(S, K, std::max(T - dT, 1e-10), sigma, r, q, is_call, n_steps, is_american);
        res.theta = (bs_t.price - res.price) / 365.0;
        double dr = 0.0001;
        auto bs_r = binomialTree(S, K, T, sigma, r + dr, q, is_call, n_steps, is_american);
        res.rho = (bs_r.price - res.price) / dr / 100.0;
    }

    res.payoff_curve = generatePayoffCurve(S, K, T, sigma, r, q, is_call);
    return res;
}

} // namespace OptionPricer
