#include "Metric/CUSUMCalibrator.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <numeric>

CUSUMCalibrationResult CUSUMCalibrator::calibrate(
    const std::vector<double>& returns,
    double detectable_shift_sigma,
    double target_arl0,
    int mc_simulations,
    int bootstrap_iters)
{
    CUSUMCalibrationResult result;
    size_t N = returns.size();

    // 1. 样本均值和标准差
    result.mu = std::accumulate(returns.begin(), returns.end(), 0.0) / N;

    double sq_sum = 0.0;
    for (double r : returns) {
        double d = r - result.mu;
        sq_sum += d * d;
    }
    result.sigma = std::sqrt(sq_sum / N);

    if (result.sigma < 1e-12) {
        // 常数序列，无法校准
        result.sigma = 1.0;
        result.lambda = 0.5;
        result.H = 4.0;
        result.min_obs = 30;
        return result;
    }

    // 2. lambda = delta / (2 * sigma)，其中 delta = detectable_shift_sigma * sigma
    //    所以 lambda = detectable_shift_sigma / 2
    result.lambda = detectable_shift_sigma / 2.0;

    // 3. Monte Carlo 二分搜索 H
    result.H = find_h_for_arl0(target_arl0, result.lambda, 0.02, mc_simulations);

    // 验证实际 ARL0
    result.actual_arl0 = simulate_arl0(result.H, result.lambda, mc_simulations);

    // 4. Bootstrap 确定 min_obs
    std::vector<double> sizes, cvs;
    result.min_obs = bootstrap_min_obs(returns, 0.15, bootstrap_iters, &sizes, &cvs);
    result.bootstrap_sizes = std::move(sizes);
    result.bootstrap_cv = std::move(cvs);

    // 5. Bootstrap sigma 95% CI
    std::mt19937 rng(42);
    std::vector<double> sigma_samples;
    sigma_samples.reserve(bootstrap_iters);

    for (int b = 0; b < bootstrap_iters; ++b) {
        std::vector<double> resampled(N);
        std::uniform_int_distribution<size_t> dist(0, N - 1);
        for (size_t i = 0; i < N; ++i) {
            resampled[i] = returns[dist(rng)];
        }
        double bmu = std::accumulate(resampled.begin(), resampled.end(), 0.0) / N;
        double bsq = 0.0;
        for (double r : resampled) {
            double d = r - bmu;
            bsq += d * d;
        }
        sigma_samples.push_back(std::sqrt(bsq / N));
    }

    std::sort(sigma_samples.begin(), sigma_samples.end());
    result.sigma_ci_lower = sigma_samples[(int)(bootstrap_iters * 0.025)];
    result.sigma_ci_upper = sigma_samples[(int)(bootstrap_iters * 0.975)];

    // sigma 的变异系数
    double sigma_mean = std::accumulate(sigma_samples.begin(), sigma_samples.end(), 0.0) / bootstrap_iters;
    double sigma_var = 0.0;
    for (double s : sigma_samples) {
        double d = s - sigma_mean;
        sigma_var += d * d;
    }
    sigma_var /= bootstrap_iters;
    result.sigma_cv = (sigma_mean > 1e-12) ? std::sqrt(sigma_var) / sigma_mean : 0.0;

    // 6. 生成 ARL-H 曲线（用于前端可视化）
    generate_arl_curve(result.lambda, 0.5, 8.0, 20, mc_simulations,
                       result.arl_curve_H, result.arl_curve_arl);

    return result;
}

double CUSUMCalibrator::simulate_arl0(
    double H, double lambda,
    int simulations, int max_length)
{
    std::mt19937 rng(12345);
    std::normal_distribution<double> normal(0.0, 1.0);

    double k = lambda;  // sigma=1（标准化空间）
    long long total_length = 0;

    for (int sim = 0; sim < simulations; ++sim) {
        double s_pos = 0.0, s_neg = 0.0;
        for (int t = 0; t < max_length; ++t) {
            double z = normal(rng);
            s_pos = std::max(0.0, s_pos + z - k);
            s_neg = std::max(0.0, s_neg - z - k);

            if (std::max(s_pos, s_neg) > H) {
                total_length += (t + 1);
                break;
            }
            if (t == max_length - 1) {
                // 未触发，记为 max_length
                total_length += max_length;
            }
        }
    }

    return (double)total_length / simulations;
}

double CUSUMCalibrator::find_h_for_arl0(
    double target_arl0, double lambda,
    double tol, int mc_simulations)
{
    double h_low = 0.1, h_high = 12.0;

    // 先确认上界足够大
    double arl_high = simulate_arl0(h_high, lambda, mc_simulations / 2);
    while (arl_high < target_arl0 && h_high < 20.0) {
        h_high *= 1.5;
        arl_high = simulate_arl0(h_high, lambda, mc_simulations / 2);
    }

    // 二分搜索
    for (int iter = 0; iter < 30; ++iter) {
        double h_mid = (h_low + h_high) / 2.0;
        double arl = simulate_arl0(h_mid, lambda, mc_simulations);

        if (std::abs(arl - target_arl0) / target_arl0 < tol) {
            return h_mid;
        }

        if (arl < target_arl0) {
            h_low = h_mid;
        } else {
            h_high = h_mid;
        }
    }

    return (h_low + h_high) / 2.0;
}

size_t CUSUMCalibrator::bootstrap_min_obs(
    const std::vector<double>& returns,
    double cv_threshold,
    int bootstrap_iters,
    std::vector<double>* sizes,
    std::vector<double>* cvs)
{
    size_t N = returns.size();
    std::mt19937 rng(42);

    // 测试一系列样本量：从 10 到 N，步长为 max(1, N/20)
    size_t step = std::max((size_t)1, N / 20);
    std::vector<size_t> test_sizes;
    for (size_t n = std::min((size_t)10, N); n <= N; n += step) {
        test_sizes.push_back(n);
    }
    if (test_sizes.back() != N) {
        test_sizes.push_back(N);
    }

    size_t min_obs = N;  // 默认用全部数据

    for (size_t n : test_sizes) {
        std::vector<double> sigma_estimates;
        sigma_estimates.reserve(bootstrap_iters);

        for (int b = 0; b < bootstrap_iters; ++b) {
            std::vector<double> resampled(n);
            std::uniform_int_distribution<size_t> dist(0, N - 1);
            for (size_t i = 0; i < n; ++i) {
                resampled[i] = returns[dist(rng)];
            }
            double mean = std::accumulate(resampled.begin(), resampled.end(), 0.0) / n;
            double sq = 0.0;
            for (double r : resampled) {
                double d = r - mean;
                sq += d * d;
            }
            sigma_estimates.push_back(std::sqrt(sq / n));
        }

        double mean_sigma = std::accumulate(sigma_estimates.begin(), sigma_estimates.end(), 0.0) / bootstrap_iters;
        double var_sigma = 0.0;
        for (double s : sigma_estimates) {
            double d = s - mean_sigma;
            var_sigma += d * d;
        }
        var_sigma /= bootstrap_iters;
        double cv = (mean_sigma > 1e-12) ? std::sqrt(var_sigma) / mean_sigma : 0.0;

        if (sizes) sizes->push_back((double)n);
        if (cvs) cvs->push_back(cv);

        if (cv < cv_threshold && n < min_obs) {
            min_obs = n;
            break;
        }
    }

    // 向上取整到 5 的倍数（实用考虑）
    min_obs = ((min_obs + 4) / 5) * 5;
    return std::max(min_obs, (size_t)10);
}

void CUSUMCalibrator::generate_arl_curve(
    double lambda,
    double h_min, double h_max,
    int n_points,
    int mc_simulations,
    std::vector<double>& out_H,
    std::vector<double>& out_arl)
{
    out_H.clear();
    out_arl.clear();
    out_H.reserve(n_points);
    out_arl.reserve(n_points);

    for (int i = 0; i < n_points; ++i) {
        double h = h_min + (h_max - h_min) * i / (n_points - 1);
        double arl = simulate_arl0(h, lambda, mc_simulations);
        out_H.push_back(h);
        out_arl.push_back(arl);
    }
}
