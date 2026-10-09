#include "Function/GaussianHMM.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include "json.hpp"

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

void GaussianHMM::forward(const Eigen::MatrixXd& obs, const Eigen::MatrixXd& log_b,
                          Eigen::MatrixXd& alpha, Eigen::VectorXd& scales) {
    int T = obs.rows();
    int N = config_.n_states;

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

void GaussianHMM::backward(const Eigen::MatrixXd& obs, const Eigen::MatrixXd& log_b,
                           const Eigen::VectorXd& scales, Eigen::MatrixXd& beta) {
    int T = obs.rows();
    int N = config_.n_states;

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
    // 发射概率整轮只依赖观测和当前参数，forward/backward/下面的 xi 循环共用同一份。
    // 之前 xi 的三重循环里每次迭代都重算一遍 emission_log_prob(obs)：那是 T×N 的
    // 矩阵分配 + T×N×D 的浮点运算，被调用 (T-1)×N×N 次，单个 EM 迭代上亿次运算，
    // 7 年日线训练直接跑不完。
    const Eigen::MatrixXd log_b = emission_log_prob(obs);

    forward(obs, log_b, alpha, scales);
    backward(obs, log_b, scales, beta);

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
                               + log_b(t + 1, j)
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

    // 未训练时返回均匀分布：prob 是值初始化的全零向量，直接返回会让调用方
    // 拿到和为 0 的「概率」，归一化断言和下游 argmax 全部失效。
    if (!trained_) return prob.setConstant(1.0 / N);

    // 用最后一个 forward 步的 gamma 近似
    // 简单做法: 贝叶斯更新 P(s|o) ∝ P(o|s) * P(s)
    // emission_log_prob 按 T×D 矩阵访问 obs(t, d)，而 observation 是 1 维列向量：
    // 直接传进去时 d≥1 越界，Debug 下触发 Eigen 断言 abort，Release 下读野内存。
    // 显式构造成 1×D 行矩阵，与下面 log_b(0, j) 的取法保持一致。
    Eigen::MatrixXd obs_row(1, observation.size());
    for (Eigen::Index d = 0; d < observation.size(); ++d) {
        obs_row(0, d) = observation(d);
    }
    auto log_b = emission_log_prob(obs_row);  // 1×N
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

// ============================================================
// reorder_states
// ============================================================

void GaussianHMM::reorder_states(const Vector<int>& perm) {
    int N = config_.n_states;
    if (static_cast<int>(perm.size()) != N) {
        throw std::runtime_error(
            fmt::format("reorder_states: perm size {} != n_states {}", perm.size(), N));
    }

    // 必须是 [0, N) 的完整排列。重复或越界的编号会让 A_ 的行和不再为 1，
    // 而行归一是 Forward 算法概率缩放的隐含前提，出错后不会立刻暴露。
    Vector<int> seen(N, 0);
    for (int i = 0; i < N; i++) {
        if (perm[i] < 0 || perm[i] >= N) {
            throw std::runtime_error(
                fmt::format("reorder_states: perm[{}]={} out of range [0,{})", i, perm[i], N));
        }
        seen[perm[i]]++;
    }
    for (int i = 0; i < N; i++) {
        if (seen[i] != 1) {
            throw std::runtime_error(
                fmt::format("reorder_states: perm is not a permutation, {} appears {} times",
                            i, seen[i]));
        }
    }

    if (pi_.size() == N) {
        Eigen::VectorXd pi_new(N);
        for (int i = 0; i < N; i++) pi_new(i) = pi_(perm[i]);
        pi_ = pi_new;
    }

    if (A_.rows() == N && A_.cols() == N) {
        Eigen::MatrixXd A_new(N, N);
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++)
                A_new(i, j) = A_(perm[i], perm[j]);
        A_ = A_new;
    }

    if (mu_.rows() == N) {
        Eigen::MatrixXd mu_new(N, mu_.cols());
        for (int i = 0; i < N; i++) mu_new.row(i) = mu_.row(perm[i]);
        mu_ = mu_new;
    }

    if (cov_diag_.rows() == N) {
        Eigen::MatrixXd cov_new(N, cov_diag_.cols());
        for (int i = 0; i < N; i++) cov_new.row(i) = cov_diag_.row(perm[i]);
        cov_diag_ = cov_new;
    }

    // current_state_ 是旧编号，同步成新编号，否则 reorder 与下一次 predict_proba
    // 之间读到的仍是上一个模型的编号。
    for (int i = 0; i < N; i++) {
        if (perm[i] == current_state_) current_state_ = i;
    }
}

// ============================================================
// Serialization
// ============================================================

nlohmann::json GaussianHMM::to_json() const {
    nlohmann::json j;
    
    j["model_type"] = "hmm";
    j["version"] = "1.0";
    
    // 模型配置
    j["model_config"]["n_states"] = config_.n_states;
    j["model_config"]["n_features"] = config_.n_features;
    j["model_config"]["covariance_type"] = "diag";
    j["model_config"]["random_seed"] = config_.random_seed;
    
    // 模型参数（Eigen → JSON 数组）
    // pi: 初始分布
    j["model_params"]["pi"] = std::vector<double>(pi_.data(), pi_.data() + pi_.size());
    
    // Eigen 默认列主序：row(i).data() 指向 A(0,i)，连续读 cols() 个元素拿到的是
    // 第 i 列而非第 i 行。原来的裸指针区间构造会把整个矩阵转置写进 JSON，
    // 读回来的转移矩阵也是转置的。改为逐元素拷贝。
    auto row_to_vec = [](const Eigen::MatrixXd& m, int i) {
        std::vector<double> v(static_cast<size_t>(m.cols()));
        for (Eigen::Index k = 0; k < m.cols(); ++k) v[k] = m(i, k);
        return v;
    };

    // A: 转移矩阵
    std::vector<std::vector<double>> A_vec;
    for (int i = 0; i < A_.rows(); ++i) {
        A_vec.push_back(row_to_vec(A_, i));
    }
    j["model_params"]["A"] = A_vec;

    // mu: 各状态均值
    std::vector<std::vector<double>> mu_vec;
    for (int i = 0; i < mu_.rows(); ++i) {
        mu_vec.push_back(row_to_vec(mu_, i));
    }
    j["model_params"]["mu"] = mu_vec;

    // cov_diag: 对角协方差
    std::vector<std::vector<double>> cov_vec;
    for (int i = 0; i < cov_diag_.rows(); ++i) {
        cov_vec.push_back(row_to_vec(cov_diag_, i));
    }
    j["model_params"]["cov_diag"] = cov_vec;
    
    // 训练信息
    j["training_info"]["log_likelihood"] = log_likelihood_;
    j["training_info"]["converged"] = trained_;
    j["training_info"]["current_state"] = current_state_;
    
    return j;
}

GaussianHMM GaussianHMM::from_json(const nlohmann::json& j) {
    Config cfg;
    cfg.n_states = j["model_config"]["n_states"];
    cfg.n_features = j["model_config"]["n_features"];
    cfg.random_seed = j["model_config"]["random_seed"];
    
    GaussianHMM hmm(cfg);
    
    // 加载模型参数
    const auto& pi_vec = j["model_params"]["pi"];
    hmm.pi_ = Eigen::VectorXd(cfg.n_states);
    for (int i = 0; i < cfg.n_states; ++i) {
        hmm.pi_(i) = pi_vec[i];
    }
    
    const auto& A_vec = j["model_params"]["A"];
    hmm.A_ = Eigen::MatrixXd(cfg.n_states, cfg.n_states);
    for (int i = 0; i < cfg.n_states; ++i) {
        for (int k = 0; k < cfg.n_states; ++k) {
            hmm.A_(i, k) = A_vec[i][k];
        }
    }
    
    const auto& mu_vec = j["model_params"]["mu"];
    hmm.mu_ = Eigen::MatrixXd(cfg.n_states, cfg.n_features);
    for (int i = 0; i < cfg.n_states; ++i) {
        for (int k = 0; k < cfg.n_features; ++k) {
            hmm.mu_(i, k) = mu_vec[i][k];
        }
    }
    
    const auto& cov_vec = j["model_params"]["cov_diag"];
    hmm.cov_diag_ = Eigen::MatrixXd(cfg.n_states, cfg.n_features);
    for (int i = 0; i < cfg.n_states; ++i) {
        for (int k = 0; k < cfg.n_features; ++k) {
            hmm.cov_diag_(i, k) = cov_vec[i][k];
        }
    }
    
    hmm.trained_ = true;
    hmm.log_likelihood_ = j.value("training_info/log_likelihood", 0.0);
    hmm.current_state_ = j.value("training_info/current_state", 0);
    
    return hmm;
}
