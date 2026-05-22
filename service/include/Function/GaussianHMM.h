#pragma once
#include "std_header.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <vector>
#include <random>

/**
 * @brief 对角协方差高斯隐马尔可夫模型
 *
 * 用于市场状态识别（牛/熊/震荡...）。
 * - 训练: Baum-Welch (EM)，离线批量
 * - 推理: Forward 算法计算 P(s_t | o_1..o_t)
 * - 解码: Viterbi 找最可能状态序列
 * - 协方差: diag（各特征独立）
 */
class GaussianHMM {
public:
    struct Config {
        int n_states = 3;
        int n_features = 1;
        int max_iter = 100;
        double tol = 1e-4;
        double regularization = 1e-6;    // 协方差正则化 ε
        uint32_t random_seed = 42;
    };

    GaussianHMM() = default;
    explicit GaussianHMM(const Config& cfg) : config_(cfg) {}

    /**
     * @brief 训练模型（EM 算法）
     * @param observations T×D 观测矩阵（T 个时间点，D 个特征）
     * @return 是否收敛
     */
    bool train(const Eigen::MatrixXd& observations);

    /**
     * @brief 推理：给定最新观测值，返回各状态概率分布
     * @param observation D 维向量
     * @return N 维概率向量
     */
    Eigen::VectorXd predict_proba(const Eigen::VectorXd& observation);

    /**
     * @brief Viterbi 解码：给定观测序列，返回最可能状态序列
     */
    std::vector<int> decode(const Eigen::MatrixXd& observations);

    /** @brief 当前最可能状态编号 */
    int current_state() const { return current_state_; }

    /** @brief 当前对数似然 */
    double log_likelihood() const { return log_likelihood_; }

    /** @brief 状态转移矩阵 A (N×N) */
    const Eigen::MatrixXd& transition_matrix() const { return A_; }

    /** @brief 各状态期望持续时间: 1/(1 - A_ii) */
    Eigen::VectorXd state_duration() const;

    /** @brief 模型是否已训练 */
    bool is_trained() const { return trained_; }

private:
    // Forward 算法（带 scaling）
    // alpha: T×N 前向概率, scales: T 缩放因子
    void forward(const Eigen::MatrixXd& obs, Eigen::MatrixXd& alpha, Eigen::VectorXd& scales);

    // Backward 算法（使用 forward 的 scales）
    // beta: T×N 后向概率
    void backward(const Eigen::MatrixXd& obs, const Eigen::VectorXd& scales, Eigen::MatrixXd& beta);

    // EM 一步，返回 log-likelihood
    double em_step(const Eigen::MatrixXd& obs,
                   Eigen::MatrixXd& gamma,    // T×N
                   Eigen::MatrixXd& xi);      // T×N×N 展平为 T×N²

    // 从 gamma/xi 更新参数
    void update_params(const Eigen::MatrixXd& obs,
                       const Eigen::MatrixXd& gamma,
                       const Eigen::MatrixXd& xi);

    // 计算观测在状态 j 下的发射概率: N(x | μ_j, diag(Σ_j))
    Eigen::MatrixXd emission_log_prob(const Eigen::MatrixXd& obs);

    // 对数-指数安全变换
    static double log_sum_exp(const Eigen::VectorXd& log_vals);

    // 参数
    Eigen::VectorXd pi_;        // 初始分布 (N,)
    Eigen::MatrixXd A_;         // 转移矩阵 (N×N)
    Eigen::MatrixXd mu_;        // 各状态均值 (N×D)
    Eigen::MatrixXd cov_diag_;  // 对角协方差 (N×D)
    int current_state_ = 0;
    double log_likelihood_ = 0;
    bool trained_ = false;
    Config config_;
    std::mt19937 rng_;
};
