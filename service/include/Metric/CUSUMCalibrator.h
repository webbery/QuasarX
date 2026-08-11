#pragma once
#include "Metric/CUSUMDetector.h"
#include <vector>
#include <cstddef>

/**
 * @brief CUSUM 自动校准结果
 */
struct CUSUMCalibrationResult {
    double mu = 0.0;                        // 参考期均值
    double sigma = 1.0;                     // 参考期标准差
    double lambda = 0.5;                    // k = lambda * sigma = delta / 2
    double H = 4.0;                         // h = H * sigma
    size_t min_obs = 30;                    // Bootstrap 确定的最小观测数

    // 诊断信息
    double sigma_ci_lower = 0.0;            // sigma Bootstrap 95% CI 下界
    double sigma_ci_upper = 0.0;            // sigma Bootstrap 95% CI 上界
    double sigma_cv = 0.0;                  // sigma Bootstrap 变异系数
    double actual_arl0 = 0.0;               // Monte Carlo 验证的实际 ARL0
    std::vector<double> arl_curve_H;        // ARL 曲线 H 值
    std::vector<double> arl_curve_arl;      // ARL 曲线 ARL 值
    std::vector<double> bootstrap_sizes;    // Bootstrap 样本量序列
    std::vector<double> bootstrap_cv;       // Bootstrap CV 序列
};

/**
 * @brief CUSUM 参数自动校准器
 *
 * 基于统计检验方法确定 CUSUM 最优参数：
 *   1. mu/sigma：参考期样本均值/标准差 + Bootstrap 置信区间
 *   2. lambda：由最小可检测偏移 delta 推导 (lambda = delta / (2*sigma))
 *   3. H：Monte Carlo 模拟反算，使 ARL0 匹配目标值
 *   4. min_obs：Bootstrap 确定 sigma 估计稳定的最小样本量
 */
class CUSUMCalibrator {
public:
    /**
     * @brief 对收益率序列进行 CUSUM 参数校准
     * @param returns 参考期收益率序列
     * @param detectable_shift_sigma 最小可检测偏移（以 sigma 倍数表示，如 0.5 表示 0.5σ）
     * @param target_arl0 目标平均无误报运行长度（天）
     * @param mc_simulations Monte Carlo 模拟次数（默认 5000）
     * @param bootstrap_iters Bootstrap 重采样次数（默认 1000）
     */
    static CUSUMCalibrationResult calibrate(
        const std::vector<double>& returns,
        double detectable_shift_sigma = 0.5,
        double target_arl0 = 500.0,
        int mc_simulations = 5000,
        int bootstrap_iters = 1000
    );

    /**
     * @brief Monte Carlo 模拟：给定 H 和 lambda，估计 ARL0
     * @param H 阈值倍数
     * @param lambda 容许偏差倍数
     * @param simulations 模拟次数
     * @param max_length 单次模拟最大长度（防止无限运行）
     */
    static double simulate_arl0(
        double H, double lambda,
        int simulations = 5000,
        int max_length = 10000
    );

    /**
     * @brief 二分搜索：找到使 ARL0 匹配目标值的 H
     */
    static double find_h_for_arl0(
        double target_arl0, double lambda,
        double tol = 0.02,
        int mc_simulations = 5000
    );

    /**
     * @brief Bootstrap 确定 sigma 估计稳定的最小样本量
     * @param returns 参考期收益率
     * @param cv_threshold 变异系数阈值（默认 0.15 = 15%）
     * @param bootstrap_iters Bootstrap 重采样次数
     * @param[out] sizes 测试的样本量序列
     * @param[out] cvs 对应的 CV 序列
     */
    static size_t bootstrap_min_obs(
        const std::vector<double>& returns,
        double cv_threshold = 0.15,
        int bootstrap_iters = 1000,
        std::vector<double>* sizes = nullptr,
        std::vector<double>* cvs = nullptr
    );

    /**
     * @brief 生成 ARL-H 曲线（用于前端可视化）
     */
    static void generate_arl_curve(
        double lambda,
        double h_min, double h_max,
        int n_points,
        int mc_simulations,
        std::vector<double>& out_H,
        std::vector<double>& out_arl
    );
};
