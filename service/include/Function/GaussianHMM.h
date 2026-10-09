#pragma once
#include "std_header.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <vector>
#include <random>
#include "json.hpp"

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

    /**
     * @brief 平滑后验 P(s_t | o_1..o_T)，返回 T×N 矩阵，每行和为 1
     *
     * decode() 只给出 Viterbi 最可能路径（每个时间点一个状态编号），无法回答
     * 「该时刻有多大把握处于状态 j」。画堆叠概率图需要的是完整后验分布。
     * 参数固定后跑一次前向后向即可，与 em_step 内部的 gamma 同源。
     */
    Eigen::MatrixXd state_posterior(const Eigen::MatrixXd& observations);

    /** @brief 当前最可能状态编号 */
    int current_state() const { return current_state_; }

    /** @brief 当前对数似然 */
    double log_likelihood() const { return log_likelihood_; }

    /** @brief EM 每次迭代的 log-likelihood 序列，用于画收敛曲线 */
    const std::vector<double>& log_likelihood_history() const { return ll_history_; }

    /** @brief 状态转移矩阵 A (N×N) */
    const Eigen::MatrixXd& transition_matrix() const { return A_; }

    /** @brief 各状态期望持续时间: 1/(1 - A_ii) */
    Eigen::VectorXd state_duration() const;

    /**
     * @brief 重排状态编号，使状态语义在多次重训之间保持可比
     *
     * EM 每次训练都从随机初始化开始，状态编号的含义（哪个是震荡、哪个是上涨）
     * 完全可能翻转。下游按 `state == 1` 这类固定编号分支的策略会看到毫无规律的
     * 跳变。调用方按某个绝对判据（如各状态观测均值的升序）算出重排方案后调用本方法。
     *
     * @param perm 新编号 i 对应的旧编号，perm 必须是 [0, n_states) 的完整排列
     */
    void reorder_states(const Vector<int>& perm);

    /** @brief 模型是否已训练 */
    bool is_trained() const { return trained_; }

    /**
     * @brief 序列化为 JSON
     */
    nlohmann::json to_json() const;

    /**
     * @brief 从 JSON 加载模型
     */
    static GaussianHMM from_json(const nlohmann::json& j);

    /** @brief 获取配置 */
    const Config& config() const { return config_; }

    /** @brief 获取初始分布 */
    const Eigen::VectorXd& initial_distribution() const { return pi_; }

    /** @brief 获取各状态均值 */
    const Eigen::MatrixXd& means() const { return mu_; }

    /** @brief 获取对角协方差 */
    const Eigen::MatrixXd& cov_diag() const { return cov_diag_; }

private:
    // Forward 算法（带 scaling），log_b 由调用方预先算好（T×N 发射概率）
    // alpha: T×N 前向概率, scales: T 缩放因子
    void forward(const Eigen::MatrixXd& obs, const Eigen::MatrixXd& log_b,
                 Eigen::MatrixXd& alpha, Eigen::VectorXd& scales);

    // Backward 算法（使用 forward 的 scales），log_b 由调用方预先算好
    // beta: T×N 后向概率
    void backward(const Eigen::MatrixXd& obs, const Eigen::MatrixXd& log_b,
                  const Eigen::VectorXd& scales, Eigen::MatrixXd& beta);

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
    std::vector<double> ll_history_;   // EM 每次迭代的 log-likelihood
    bool trained_ = false;
    Config config_;
    std::mt19937 rng_;
};
