#pragma once
#include "std_header.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <numeric>

// 期权定价引擎: Black-Scholes / Monte Carlo / Binomial Tree
// 欧式 + 美式，支持分红

struct BSResult {
    double price = 0;
    double intrinsic_value = 0;
    double time_value = 0;
    double delta = 0, gamma = 0, theta = 0, vega = 0, rho = 0;
    String moneyness;   // "ITM" / "ATM" / "OTM"
};

struct MCResult {
    double price = 0;
    double std_error = 0;
    Vector<double> payoff_distribution;  // 终值分布（供直方图）
};

struct TreeResult {
    double price = 0;
    double early_exercise_premium = 0;   // 美式提前行权溢价
};

// 收益曲线数据点
struct PayoffPoint {
    double spot;
    double payoff_at_expiry;   // 到期收益
    double payoff_now;         // 含时间价值的当前理论价值
};

struct PricingResult {
    double price = 0;
    double intrinsic_value = 0;
    double time_value = 0;
    String moneyness;
    double delta = 0, gamma = 0, theta = 0, vega = 0, rho = 0;
    Vector<PayoffPoint> payoff_curve;
    // MC 专有
    double mc_std_error = 0;
    Vector<double> mc_payoff_distribution;
    // Tree 专有
    double early_exercise_premium = 0;
};

namespace OptionPricer {

// ── Black-Scholes 解析解（欧式） ──
BSResult blackScholes(double S, double K, double T, double sigma,
                      double r, double q, bool is_call);

// ── Monte Carlo（几何布朗运动，欧式） ──
MCResult monteCarlo(double S, double K, double T, double sigma,
                    double r, double q, bool is_call,
                    int n_paths = 100000, int n_steps = 252,
                    uint64_t seed = 42);

// ── 二叉树 CRR（支持美式提前行权） ──
TreeResult binomialTree(double S, double K, double T, double sigma,
                        double r, double q, bool is_call,
                        int n_steps = 200, bool is_american = true);

// ── 统一入口 ──
PricingResult price(const String& method,
                    double S, double K, double T, double sigma,
                    double r, double q, bool is_call, bool is_american,
                    int n_paths = 100000, int n_steps = 252);

// ── 收益曲线生成 ──
Vector<PayoffPoint> generatePayoffCurve(
    double S, double K, double T, double sigma,
    double r, double q, bool is_call,
    int n_points = 100, double range_pct = 0.3);

// ── 虚实度判定 ──
String classifyMoneyness(double S, double K, double tolerance = 0.01);

} // namespace OptionPricer
