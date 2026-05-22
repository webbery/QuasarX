#include "Function/GaussianHMM.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
    constexpr double LOG_ZERO = -1e10;
    constexpr double PI = 3.14159265358979323846;
}

// ============================================================
// log_sum_exp: 安全的对数和
// ============================================================

double GaussianHMM::log_sum_exp(const Eigen::VectorXd& log_vals) {
    double max_val = log_vals.maxCoeff();
    if (max_val < LOG_ZERO) return LOG_ZERO;
    return max_val + std::log((log_vals.array() - max_val).exp().sum());
}

// Helper: exp(sum) in log space
static double rowwise_exp_sum(const Eigen::VectorXd& log_vals) {
    double max_v = log_vals.maxCoeff();
    double sum = 0.0;
    for (int i = 0; i < log_vals.size(); i++) {
        sum += std::exp(log_vals(i) - max_v);
    }
    return max_v + std::log(sum);
}

// ============================================================
// 发射概率: log N(x | μ_j, diag(Σ_j))
// obs: T×D, 返回 T×N
// ============================================================

Eigen::MatrixXd GaussianHMM::emission_log_prob(const Eigen::MatrixXd& obs) {
    int T = obs.rows();
    int N = config_.n_states;
    int D = config_.n_features;
    Eigen::MatrixXd log_b(T, N);

    double log_norm = -0.5 * D * std::log(2 * PI);

    for (int j = 0; j < N; j++) {
        for (int t = 0; t < T; t++) {
            double log_det = 0.0;
            double quad = 0.0;
            for (int d = 0; d < D; d++) {
                double diff = obs(t, d) - mu_(j, d);
                double var = cov_diag_(j, d);
                log_det += std::log(var);
                quad += diff * diff / var;
            }
            log_b(t, j) = log_norm - 0.5 * log_det - 0.5 * quad;
        }
    }
    return log_b;
}

// ============================================================
// Forward 算法（带 scaling）
// alpha[t,j] = P(o_1..o_t, s_t=j) / (c_1 * ... * c_t)
// scales[t] = c_t
// ============================================================

void GaussianHMM::forward(const Eigen::MatrixXd& obs, Eigen::MatrixXd& alpha, Eigen::VectorXd& scales) {
    int T = obs.rows();
    int N = config_.n_states;
    auto log_b = emission_log_prob(obs);

    // t = 0
    for (int j = 0; j < N; j++) {
        alpha(0, j) = std::log(std::max(pi_(j), 1e-300)) + log_b(0, j);
    }
    // scaling
    double c = rowwise_exp_sum(alpha.row(0));
    scales(0) = c;
    alpha.row(0).array() -= c;  // log 空间减 c 等价于除以 exp(c)

    for (int t = 1; t < T; t++) {
        for (int j = 0; j < N; j++) {
            Eigen::VectorXd log_terms(N);
            for (int i = 0; i < N; i++) {
                log_terms(i) = alpha(t-1, i) + std::log(std::max(A_(i, j), 1e-300));
            }
            alpha(t, j) = log_sum_exp(log_terms) + log_b(t, j);
        }
        // scaling
        double sum_exp = 0.0;
        double max_a = alpha.row(t).maxCoeff();
        for (int j = 0; j < N; j++) {
            sum_exp += std::exp(alpha(t, j) - max_a);
        }
        scales(t) = max_a + std::log(sum_exp);
        alpha.row(t).array() -= scales(t);
    }
}

// ============================================================
// Backward 算法（使用 forward 的 scales）
// beta[t,j] = P(o_{t+1}..o_T | s_t=j) / (c_{t+1} * ... * c_T)
// ============================================================

void GaussianHMM::backward(const Eigen::MatrixXd& obs, const Eigen::VectorXd& scales, Eigen::MatrixXd& beta) {
    int T = obs.rows();
    int N = config_.n_states;
    auto log_b = emission_log_prob(obs);

    // t = T-1
    beta.row(T - 1).setZero();  // log(1) = 0

    for (int t = T - 2; t >= 0; t--) {
        for (int i = 0; i < N; i++) {
            Eigen::VectorXd log_terms(N);
            for (int j = 0; j < N; j++) {
                log_terms(j) = std::log(std::max(A_(i, j), 1e-300))
                             + log_b(t + 1, j)
                             + beta(t + 1, j);
            }
            beta(t, i) = log_sum_exp(log_terms) - scales(t + 1);
        }
    }
}

// ============================================================
// EM 一步
// ============================================================

double GaussianHMM::em_step(const Eigen::MatrixXd& obs,
                             Eigen::MatrixXd& gamma,
                             Eigen::MatrixXd& xi_flat) {
    int T = obs.rows();
    int N = config_.n_states;

    Eigen::MatrixXd alpha(T, N), beta(T, N);
    Eigen::VectorXd scales(T);
    forward(obs, alpha, scales);
    backward(obs, scales, beta);

    // gamma[t,j] = alpha[t,j] * beta[t,j] / P(O)
    double log_prob = 0.0;
    for (int t = 0; t < T; t++) log_prob += scales(t);
    log_likelihood_ = log_prob;

    for (int t = 0; t < T; t++) {
        Eigen::VectorXd log_gamma(N);
        for (int j = 0; j < N; j++) {
            log_gamma(j) = alpha(t, j) + beta(t, j);
        }
        double log_norm = log_sum_exp(log_gamma);
        for (int j = 0; j < N; j++) {
            gamma(t, j) = std::exp(log_gamma(j) - log_norm);
        }
    }

    // xi[t,i,j] (展平为 xi_flat[t, i*N+j])
    for (int t = 0; t < T - 1; t++) {
        double log_norm = LOG_ZERO;
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                double log_val = alpha(t, i)
                               + std::log(std::max(A_(i, j), 1e-300))
                               + emission_log_prob(obs)(t + 1, j)
                               + beta(t + 1, j);
                xi_flat(t, i * N + j) = log_val;
                log_norm = (log_norm > log_val) ? log_norm : log_val;
            }
        }
        // normalize
        double sum_exp = 0.0;
        for (int k = 0; k < N * N; k++) {
            xi_flat(t, k) -= log_norm;
            sum_exp += std::exp(xi_flat(t, k));
        }
        for (int k = 0; k < N * N; k++) {
            xi_flat(t, k) = std::exp(xi_flat(t, k)) / sum_exp;
        }
    }

    // 更新参数
    update_params(obs, gamma, xi_flat);

    return log_likelihood_;
}

// ============================================================
// 更新 π, A, μ, Σ
// ============================================================

void GaussianHMM::update_params(const Eigen::MatrixXd& obs,
                                 const Eigen::MatrixXd& gamma,
                                 const Eigen::MatrixXd& xi_flat) {
    int T = obs.rows();
    int N = config_.n_states;
    int D = config_.n_features;

    // π: 初始分布
    for (int j = 0; j < N; j++) {
        pi_(j) = gamma(0, j) + 1e-10;
    }
    pi_ /= pi_.sum();

    // A: 转移矩阵
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            double sum_xi = 0.0, sum_gamma = 0.0;
            for (int t = 0; t < T - 1; t++) {
                sum_xi += xi_flat(t, i * N + j);
                sum_gamma += gamma(t, i);
            }
            A_(i, j) = (sum_xi + 1e-10) / (sum_gamma + 1e-10);
        }
        // 行归一化
        A_.row(i) /= A_.row(i).sum();
    }

    // μ 和 Σ: 加权均值和方差
    for (int j = 0; j < N; j++) {
        double sum_gamma = gamma.col(j).sum();
        if (sum_gamma < 1e-10) {
            // 退化状态: 保持原值 + 小扰动
            mu_.row(j) += Eigen::VectorXd::Random(D) * 0.01;
            for (int d = 0; d < D; d++) {
                cov_diag_(j, d) += config_.regularization;
            }
            continue;
        }

        for (int d = 0; d < D; d++) {
            double mu_new = 0.0;
            for (int t = 0; t < T; t++) {
                mu_new += gamma(t, j) * obs(t, d);
            }
            mu_(j, d) = mu_new / sum_gamma;
        }

        for (int d = 0; d < D; d++) {
            double var = 0.0;
            for (int t = 0; t < T; t++) {
                double diff = obs(t, d) - mu_(j, d);
                var += gamma(t, j) * diff * diff;
            }
            var /= sum_gamma;
            // 正则化: 防止退化
            cov_diag_(j, d) = var + config_.regularization;
        }
    }
}

// ============================================================
// 训练
// ============================================================

bool GaussianHMM::train(const Eigen::MatrixXd& observations) {
    int T = observations.rows();
    int N = config_.n_states;
    int D = config_.n_features;

    if (T < N * 2 || D != config_.n_features) {
        return false;
    }

    // 初始化参数
    pi_.resize(N);
    A_.resize(N, N);
    mu_.resize(N, D);
    cov_diag_.resize(N, D);

    rng_.seed(config_.random_seed);
    std::uniform_real_distribution<double> unif(0.0, 1.0);

    // π: 均匀 + 小噪声
    for (int j = 0; j < N; j++) pi_(j) = 1.0 / N + unif(rng_) * 0.01;
    pi_ /= pi_.sum();

    // A: 随机 + 行归一化
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A_(i, j) = 1.0 / N + unif(rng_) * 0.1;
        }
        A_.row(i) /= A_.row(i).sum();
    }

    // μ: K-means 风格初始化（用数据分位数）
    Eigen::VectorXd sorted_data = observations.col(0);
    std::sort(sorted_data.data(), sorted_data.data() + sorted_data.size());
    for (int j = 0; j < N; j++) {
        int idx = (j + 0.5) * T / N;
        idx = std::min(idx, T - 1);
        mu_(j, 0) = sorted_data(idx);
        for (int d = 1; d < D; d++) {
            mu_(j, d) = observations(idx, d);
        }
    }

    // Σ: 数据总方差
    for (int d = 0; d < D; d++) {
        double var = (observations.col(d).array() - observations.col(d).mean()).square().mean();
        for (int j = 0; j < N; j++) {
            cov_diag_(j, d) = var + config_.regularization;
        }
    }

    // EM 循环
    Eigen::MatrixXd gamma(T, N);
    Eigen::MatrixXd xi_flat(T, N * N);
    double prev_ll = -std::numeric_limits<double>::infinity();
    bool converged = false;

    for (int iter = 0; iter < config_.max_iter; iter++) {
        double ll = em_step(observations, gamma, xi_flat);

        if (iter > 0 && std::abs(ll - prev_ll) < config_.tol) {
            converged = true;
            break;
        }
        prev_ll = ll;
    }

    // 当前最可能状态（最后时刻）
    Eigen::VectorXd last_gamma = gamma.row(T - 1);
    last_gamma.maxCoeff(&current_state_);

    trained_ = true;
    return converged;
}

// ============================================================
// predict_proba: 单步推理
// ============================================================

Eigen::VectorXd GaussianHMM::predict_proba(const Eigen::VectorXd& observation) {
    int N = config_.n_states;
    Eigen::VectorXd prob(N);

    if (!trained_) return prob;

    // 用最后一个 forward 步的 gamma 近似
    // 简单做法: 贝叶斯更新 P(s|o) ∝ P(o|s) * P(s)
    auto log_b = emission_log_prob(observation);  // 1×N
    for (int j = 0; j < N; j++) {
        prob(j) = std::exp(log_b(0, j)) * pi_(j);
    }
    double sum = prob.sum();
    if (sum > 0) prob /= sum;
    else prob.setConstant(1.0 / N);

    // 更新当前状态
    prob.maxCoeff(&current_state_);
    return prob;
}

// ============================================================
// Viterbi 解码
// ============================================================

std::vector<int> GaussianHMM::decode(const Eigen::MatrixXd& observations) {
    int T = observations.rows();
    int N = config_.n_states;
    auto log_b = emission_log_prob(observations);

    Eigen::MatrixXd delta(T, N);
    Eigen::MatrixXi psi(T, N);

    // 初始化
    for (int j = 0; j < N; j++) {
        delta(0, j) = std::log(std::max(pi_(j), 1e-300)) + log_b(0, j);
        psi(0, j) = 0;
    }

    // 递推
    for (int t = 1; t < T; t++) {
        for (int j = 0; j < N; j++) {
            double max_val = -std::numeric_limits<double>::infinity();
            int max_idx = 0;
            for (int i = 0; i < N; i++) {
                double val = delta(t-1, i) + std::log(std::max(A_(i, j), 1e-300));
                if (val > max_val) {
                    max_val = val;
                    max_idx = i;
                }
            }
            delta(t, j) = max_val + log_b(t, j);
            psi(t, j) = max_idx;
        }
    }

    // 回溯
    std::vector<int> path(T);
    delta.row(T - 1).maxCoeff(&path[T - 1]);
    for (int t = T - 2; t >= 0; t--) {
        path[t] = psi(t + 1, path[t + 1]);
    }
    return path;
}

// ============================================================
// state_duration
// ============================================================

Eigen::VectorXd GaussianHMM::state_duration() const {
    int N = config_.n_states;
    Eigen::VectorXd dur(N);
    for (int i = 0; i < N; i++) {
        double stay_prob = A_(i, i);
        if (stay_prob >= 1.0) stay_prob = 0.9999;
        dur(i) = 1.0 / (1.0 - stay_prob);
    }
    return dur;
}
