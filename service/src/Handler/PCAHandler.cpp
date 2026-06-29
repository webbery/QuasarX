#include "Handler/PCAHandler.h"
#include "Util/data.h"
#include "Util/datetime.h"
#include "server.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>

// ──────────────────────────────────────────────────────────────────────
// 质量评估工具函数
// ──────────────────────────────────────────────────────────────────────

/// KMO 检验 (Kaiser-Meyer-Olkin)
/// 度量变量间偏相关性，>0.6 适合 PCA
static double computeKMO(const Eigen::MatrixXd& corr) {
    int n = corr.rows();
    if (n < 2) return 0;

    double sum_r2 = 0;  // 相关系数平方和（非对角线）
    double sum_a2 = 0;  // 偏相关系数平方和

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double r = corr(i, j);
            sum_r2 += r * r;

            // 偏相关系数近似: a_ij = -inv(corr)_ij / sqrt(inv_ii * inv_jj)
            // 简化: 用 (1 - r²) 近似偏相关
            double partial = (1.0 - r * r);
            sum_a2 += partial * partial;
        }
    }

    double denom = sum_r2 + sum_a2;
    return (denom > 1e-15) ? (sum_r2 / denom) : 0.0;
}

/// Bartlett 球形检验
/// 检验相关矩阵是否为单位阵（变量间是否独立）
/// 返回 {卡方统计量, p值}
static std::pair<double, double> bartlettTest(const Eigen::MatrixXd& corr, int n_obs) {
    int n = corr.rows();
    if (n < 2) return {0, 1};

    // 行列式
    double det = corr.determinant();
    if (det <= 0) return {0, 1};

    // Bartlett 统计量: χ² = -(N - 1 - (2p+5)/6) * ln(|R|)
    double df = n * (n - 1) / 2.0;
    double chi_sq = -(n_obs - 1.0 - (2.0 * n + 5.0) / 6.0) * std::log(det);
    chi_sq = std::max(chi_sq, 0.0);

    // p 值近似（卡方分布）
    // 使用 Wilson-Hilferty 近似: (χ²/df)^(1/3) ~ N(1 - 2/(9df), 2/(9df))
    double z = 0;
    if (df > 0) {
        double ratio = chi_sq / df;
        if (ratio > 0) {
            z = (std::pow(ratio, 1.0 / 3.0) - (1.0 - 2.0 / (9.0 * df))) /
                std::sqrt(2.0 / (9.0 * df));
        }
    }

    // 标准正态右尾概率
    double p_value = 0.5 * std::erfc(z / std::sqrt(2.0));
    return {chi_sq, p_value};
}

/// 重构误差（Frobenius 范数）
static double computeReconstructionError(
    const std::vector<std::vector<double>>& original,
    const std::vector<std::vector<double>>& reconstructed)
{
    if (original.empty() || reconstructed.empty()) return 0;
    int n = static_cast<int>(original.size());
    double error = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double diff = original[i][j] - reconstructed[i][j];
            error += diff * diff;
        }
    }
    return std::sqrt(error);
}

/// 评估等级判定
static String kmoGrade(double kmo) {
    if (kmo >= 0.8) return "非常适合";
    if (kmo >= 0.6) return "适合";
    if (kmo >= 0.5) return "一般";
    return "不适合";
}

static String condGrade(double cond) {
    if (cond < 100) return "优秀";
    if (cond < 1000) return "良好";
    if (cond < 10000) return "可接受";
    return "差";
}

static String varianceGrade(double var) {
    if (var >= 0.85) return "优秀";
    if (var >= 0.70) return "良好";
    if (var >= 0.50) return "可接受";
    return "差";
}

// ──────────────────────────────────────────────────────────────────────
// 质量评估计算
// ──────────────────────────────────────────────────────────────────────

PCAQualityMetrics PCAHandler::computeQuality(
    const std::vector<std::vector<double>>& corr_matrix,
    const std::vector<double>& eigenvalues,
    const std::vector<double>& variance_ratio,
    const std::vector<double>& cumulative_variance,
    const std::vector<std::vector<double>>& corr_reconstructed,
    int n_components)
{
    PCAQualityMetrics metrics{};

    int n = static_cast<int>(corr_matrix.size());
    if (n < 2) return metrics;

    // KMO
    Eigen::MatrixXd corr(n, n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            corr(i, j) = corr_matrix[i][j];
    metrics.kmo = computeKMO(corr);
    metrics.kmo_grade = kmoGrade(metrics.kmo);

    // Bartlett（假设样本量 = 252，一年交易日）
    auto [chi_sq, p_val] = bartlettTest(corr, 252);
    metrics.bartlett_stat = chi_sq;
    metrics.bartlett_pvalue = p_val;

    // 累计方差（前K个PC）
    if (!cumulative_variance.empty()) {
        int idx = std::min(n_components, static_cast<int>(cumulative_variance.size())) - 1;
        if (idx >= 0) {
            metrics.cumulative_variance = cumulative_variance[idx];
            metrics.variance_grade = varianceGrade(metrics.cumulative_variance);
        }
    }

    // 条件数
    if (!eigenvalues.empty()) {
        double lambda_min = eigenvalues.back();
        double lambda_max = eigenvalues[0];
        metrics.condition_number = (lambda_min > 1e-15) ? (lambda_max / lambda_min) : 1e15;
        metrics.cond_grade = condGrade(metrics.condition_number);
        metrics.is_positive_definite = (lambda_min > 1e-12);
    }

    // 重构误差
    if (!corr_reconstructed.empty()) {
        metrics.reconstruction_error = computeReconstructionError(corr_matrix, corr_reconstructed);
        // 归一化：除以原始矩阵的 Frobenius 范数
        double original_norm = 0;
        for (const auto& row : corr_matrix)
            for (double v : row) original_norm += v * v;
        original_norm = std::sqrt(original_norm);
        metrics.reconstruction_error_pct = (original_norm > 1e-15) ?
            (metrics.reconstruction_error / original_norm * 100.0) : 0;
    }

    return metrics;
}

// ──────────────────────────────────────────────────────────────────────
// 截面 PCA：多标的共同因子分析
// ──────────────────────────────────────────────────────────────────────

PCACrossSectionResult PCAHandler::computeCrossSection(
    const std::map<std::string, std::vector<double>>& returns_map,
    const std::vector<std::string>& symbols,
    int n_components)
{
    PCACrossSectionResult result{};
    result.symbols = symbols;
    int n = static_cast<int>(symbols.size());
    if (n < 2) return result;

    // 找到最小序列长度
    int T = std::numeric_limits<int>::max();
    for (const auto& sym : symbols) {
        auto it = returns_map.find(sym);
        if (it != returns_map.end()) {
            T = std::min(T, static_cast<int>(it->second.size()));
        }
    }
    if (T < n + 2) return result;

    result.n_observations = T;
    result.n_symbols = n;

    // 构建 T×N 收益率矩阵（去均值）
    Eigen::MatrixXd R(T, n);
    Vector<double> means(n, 0);
    for (int j = 0; j < n; ++j) {
        auto it = returns_map.find(symbols[j]);
        if (it == returns_map.end()) continue;
        const auto& data = it->second;
        double sum = 0;
        for (int i = 0; i < T; ++i) sum += data[i];
        means[j] = sum / T;
        for (int i = 0; i < T; ++i) R(i, j) = data[i] - means[j];
    }

    // 相关系数矩阵（标准化）
    Eigen::MatrixXd corr = Eigen::MatrixXd::Zero(n, n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) {
                corr(i, j) = 1.0;
            } else {
                double var_i = 0, var_j = 0, cov_ij = 0;
                for (int t = 0; t < T; ++t) {
                    double di = R(t, i), dj = R(t, j);
                    var_i += di * di;
                    var_j += dj * dj;
                    cov_ij += di * dj;
                }
                double denom = std::sqrt(var_i * var_j);
                corr(i, j) = (denom > 1e-15) ? (cov_ij / denom) : 0.0;
            }
        }
    }

    // 原始相关系数矩阵 → JSON
    result.corr_original.resize(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            result.corr_original[i][j] = corr(i, j);

    // 特征值分解
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(corr);
    if (solver.info() != Eigen::Success) return result;

    // eigenvalues 升序 → 反转为降序
    auto eigenvalues = solver.eigenvalues();
    auto eigenvectors = solver.eigenvectors();

    int total_pc = n;
    result.eigenvalues.resize(total_pc);
    result.variance_ratio.resize(total_pc);
    result.cumulative_variance.resize(total_pc);
    double total_var = eigenvalues.sum();

    for (int i = 0; i < total_pc; ++i) {
        int rev_i = total_pc - 1 - i;
        result.eigenvalues[i] = eigenvalues(rev_i);
        result.variance_ratio[i] = (total_var > 1e-15) ? eigenvalues(rev_i) / total_var : 0.0;
        result.cumulative_variance[i] = result.variance_ratio[i] +
            (i > 0 ? result.cumulative_variance[i - 1] : 0.0);
    }

    // 确定 n_components
    if (n_components <= 0 || n_components > n) {
        n_components = 0;
        for (double ev : result.eigenvalues) {
            if (ev > 1.0) n_components++;
        }
        if (n_components == 0) n_components = std::min(3, n);
    }
    result.n_components = n_components;

    // 载荷矩阵 = 特征向量 × sqrt(特征值)
    result.loadings.resize(n, std::vector<double>(n_components));
    for (int j = 0; j < n_components; ++j) {
        int ev_idx = n - 1 - j;
        double sqrt_ev = std::sqrt(std::max(result.eigenvalues[j], 0.0));
        for (int i = 0; i < n; ++i) {
            result.loadings[i][j] = eigenvectors(i, ev_idx) * sqrt_ev;
        }
    }

    // 得分矩阵
    result.scores.resize(T, std::vector<double>(n_components));
    for (int t = 0; t < T; ++t) {
        for (int j = 0; j < n_components; ++j) {
            double score = 0;
            for (int i = 0; i < n; ++i) {
                score += R(t, i) * result.loadings[i][j];
            }
            double ev = result.eigenvalues[j];
            result.scores[t][j] = (ev > 1e-15) ? (score / std::sqrt(ev)) : 0.0;
        }
    }

    // 重构相关系数矩阵
    result.corr_reconstructed.resize(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double recon = 0;
            for (int k = 0; k < n_components; ++k) {
                recon += result.loadings[i][k] * result.loadings[j][k];
            }
            result.corr_reconstructed[i][j] = recon;
        }
    }

    // 质量评估
    result.quality = computeQuality(
        result.corr_original, result.eigenvalues,
        result.variance_ratio, result.cumulative_variance,
        result.corr_reconstructed, n_components);

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// 时序 PCA：单标的多特征降维
// ──────────────────────────────────────────────────────────────────────

PCATimeSeriesResult PCAHandler::computeTimeSeries(
    const std::map<std::string, std::vector<double>>& features_map,
    const std::vector<std::string>& features,
    const std::string& symbol,
    int n_components)
{
    PCATimeSeriesResult result{};
    result.symbol = symbol;
    result.features = features;

    int n = static_cast<int>(features.size());
    if (n < 2) return result;

    int T = std::numeric_limits<int>::max();
    for (const auto& feat : features) {
        auto it = features_map.find(feat);
        if (it != features_map.end()) {
            T = std::min(T, static_cast<int>(it->second.size()));
        }
    }
    if (T < n + 2) return result;

    result.n_observations = T;
    result.n_features = n;

    // 构建 T×N 特征矩阵（标准化）
    Eigen::MatrixXd X(T, n);
    Vector<double> means(n, 0), stds(n, 0);

    for (int j = 0; j < n; ++j) {
        auto it = features_map.find(features[j]);
        if (it == features_map.end()) continue;
        const auto& data = it->second;

        double sum = 0;
        for (int i = 0; i < T; ++i) sum += data[i];
        means[j] = sum / T;

        double ss = 0;
        for (int i = 0; i < T; ++i) {
            double d = data[i] - means[j];
            ss += d * d;
        }
        stds[j] = std::sqrt(ss / T);

        for (int i = 0; i < T; ++i) {
            X(i, j) = (stds[j] > 1e-15) ? ((data[i] - means[j]) / stds[j]) : 0.0;
        }
    }

    // 相关系数矩阵
    Eigen::MatrixXd corr = (X.transpose() * X) / static_cast<double>(T - 1);

    // 特征值分解
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(corr);
    if (solver.info() != Eigen::Success) return result;

    auto eigenvalues = solver.eigenvalues();
    auto eigenvectors = solver.eigenvectors();

    int total_pc = n;
    result.eigenvalues.resize(total_pc);
    result.variance_ratio.resize(total_pc);
    result.cumulative_variance.resize(total_pc);
    double total_var = eigenvalues.sum();

    for (int i = 0; i < total_pc; ++i) {
        int rev_i = total_pc - 1 - i;
        result.eigenvalues[i] = eigenvalues(rev_i);
        result.variance_ratio[i] = (total_var > 1e-15) ? eigenvalues(rev_i) / total_var : 0.0;
        result.cumulative_variance[i] = result.variance_ratio[i] +
            (i > 0 ? result.cumulative_variance[i - 1] : 0.0);
    }

    if (n_components <= 0 || n_components > n) {
        n_components = 0;
        for (double ev : result.eigenvalues) {
            if (ev > 1.0) n_components++;
        }
        if (n_components == 0) n_components = std::min(3, n);
    }
    result.n_components = n_components;

    // 载荷矩阵
    result.loadings.resize(n, std::vector<double>(n_components));
    for (int j = 0; j < n_components; ++j) {
        int ev_idx = n - 1 - j;
        for (int i = 0; i < n; ++i) {
            result.loadings[i][j] = eigenvectors(i, ev_idx);
        }
    }

    // 得分矩阵
    result.scores.resize(T, std::vector<double>(n_components));
    for (int t = 0; t < T; ++t) {
        for (int j = 0; j < n_components; ++j) {
            double score = 0;
            for (int i = 0; i < n; ++i) {
                score += X(t, i) * result.loadings[i][j];
            }
            result.scores[t][j] = score;
        }
    }

    // 重构相关系数矩阵（用于质量评估）
    std::vector<std::vector<double>> corr_orig(n, std::vector<double>(n));
    std::vector<std::vector<double>> corr_recon(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            corr_orig[i][j] = corr(i, j);
            double recon = 0;
            for (int k = 0; k < n_components; ++k) {
                recon += result.loadings[i][k] * result.loadings[j][k];
            }
            corr_recon[i][j] = recon;
        }
    }

    result.quality = computeQuality(
        corr_orig, result.eigenvalues,
        result.variance_ratio, result.cumulative_variance,
        corr_recon, n_components);

    return result;
}

// ──────────────────────────────────────────────────────────────────────
// HTTP GET Handler
// ──────────────────────────────────────────────────────────────────────

void PCAHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();
        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto field = req.get_param_value("field");
        auto fill_param = req.get_param_value("fill_method");
        auto n_components_param = req.get_param_value("n_components");
        auto mode_param = req.get_param_value("mode");

        if (symbols_param.empty()) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "symbols parameter is required";
            res.set_content(err.dump(), "application/json");
            return;
        }

        std::vector<std::string> symbols;
        std::istringstream ss(symbols_param);
        std::string sym;
        while (std::getline(ss, sym, ',')) {
            if (!sym.empty()) symbols.push_back(sym);
        }

        if (field.empty()) field = "return";
        FillMethod fill = fill_param.empty() ? FillMethod::ForwardFill : parseFillMethod(fill_param);

        int n_components = 0;
        if (!n_components_param.empty()) {
            try { n_components = std::stoi(n_components_param); } catch (...) {}
        }

        PCAMode mode = PCAMode::CrossSection;
        if (mode_param == "timeseries" || mode_param == "ts") {
            mode = PCAMode::TimeSeries;
        }

        nlohmann::json json;
        json["mode"] = (mode == PCAMode::CrossSection) ? "cross_section" : "time_series";

        std::map<std::string, std::vector<double>> data_map;
        Vector<String> dates;

        if (mode == PCAMode::CrossSection) {
            for (const auto& symbol : symbols) {
                std::vector<std::string> fields;
                if (field == "return") {
                    fields = {"close"};
                } else {
                    fields = {field};
                }
                auto multi = LoadHistoryData(symbol, fields, start_date, end_date, &dates, fill);
                auto it = multi.find(field == "return" ? "close" : field);
                if (it != multi.end() && it->second.size() >= 2) {
                    if (field == "return") {
                        const auto& prices = it->second;
                        std::vector<double> returns;
                        returns.reserve(prices.size() - 1);
                        for (size_t i = 1; i < prices.size(); ++i) {
                            if (prices[i - 1] > 1e-15) {
                                returns.push_back((prices[i] - prices[i - 1]) / prices[i - 1]);
                            } else {
                                returns.push_back(0.0);
                            }
                        }
                        data_map[symbol] = returns;
                    } else {
                        data_map[symbol] = it->second;
                    }
                }
            }

            if (data_map.size() < 2) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "Need at least 2 symbols with valid data for cross-section PCA";
                res.set_content(err.dump(), "application/json");
                return;
            }

            auto pca_result = computeCrossSection(data_map, symbols, n_components);

            if (field == "return" && dates.size() > 1) {
                dates.erase(dates.begin());
            }
            json["dates"] = dates;

            nlohmann::json cs;
            cs["symbols"] = pca_result.symbols;
            cs["eigenvalues"] = pca_result.eigenvalues;
            cs["variance_ratio"] = pca_result.variance_ratio;
            cs["cumulative_variance"] = pca_result.cumulative_variance;
            cs["loadings"] = pca_result.loadings;
            cs["scores"] = pca_result.scores;
            cs["corr_original"] = pca_result.corr_original;
            cs["corr_reconstructed"] = pca_result.corr_reconstructed;
            cs["n_components"] = pca_result.n_components;
            cs["n_symbols"] = pca_result.n_symbols;
            cs["n_observations"] = pca_result.n_observations;

            // 质量评估
            nlohmann::json q;
            q["kmo"] = pca_result.quality.kmo;
            q["kmo_grade"] = pca_result.quality.kmo_grade;
            q["bartlett_stat"] = pca_result.quality.bartlett_stat;
            q["bartlett_pvalue"] = pca_result.quality.bartlett_pvalue;
            q["cumulative_variance"] = pca_result.quality.cumulative_variance;
            q["variance_grade"] = pca_result.quality.variance_grade;
            q["condition_number"] = pca_result.quality.condition_number;
            q["cond_grade"] = pca_result.quality.cond_grade;
            q["is_positive_definite"] = pca_result.quality.is_positive_definite;
            q["reconstruction_error"] = pca_result.quality.reconstruction_error;
            q["reconstruction_error_pct"] = pca_result.quality.reconstruction_error_pct;
            cs["quality"] = q;

            json["cross_section"] = cs;

        } else {
            if (symbols.empty()) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "Need exactly 1 symbol for time-series PCA";
                res.set_content(err.dump(), "application/json");
                return;
            }

            std::vector<std::string> features = {"close", "open", "high", "low", "volume"};
            auto multi = LoadHistoryData(symbols[0], features, start_date, end_date, &dates, fill);

            for (const auto& feat : features) {
                auto it = multi.find(feat);
                if (it != multi.end() && it->second.size() >= 2) {
                    std::vector<double> returns;
                    returns.reserve(it->second.size() - 1);
                    for (size_t i = 1; i < it->second.size(); ++i) {
                        if (it->second[i - 1] > 1e-15) {
                            returns.push_back((it->second[i] - it->second[i - 1]) / it->second[i - 1]);
                        } else {
                            returns.push_back(0.0);
                        }
                    }
                    data_map["return_" + feat] = returns;
                }
            }

            std::vector<std::string> actual_features;
            for (const auto& kv : data_map) actual_features.push_back(kv.first);

            if (actual_features.size() < 2) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = "Need at least 2 features for time-series PCA";
                res.set_content(err.dump(), "application/json");
                return;
            }

            auto pca_result = computeTimeSeries(data_map, actual_features, symbols[0], n_components);

            if (!dates.empty()) dates.erase(dates.begin());
            json["dates"] = dates;

            nlohmann::json ts;
            ts["symbol"] = pca_result.symbol;
            ts["features"] = pca_result.features;
            ts["eigenvalues"] = pca_result.eigenvalues;
            ts["variance_ratio"] = pca_result.variance_ratio;
            ts["cumulative_variance"] = pca_result.cumulative_variance;
            ts["loadings"] = pca_result.loadings;
            ts["scores"] = pca_result.scores;
            ts["n_components"] = pca_result.n_components;
            ts["n_features"] = pca_result.n_features;
            ts["n_observations"] = pca_result.n_observations;

            nlohmann::json q;
            q["kmo"] = pca_result.quality.kmo;
            q["kmo_grade"] = pca_result.quality.kmo_grade;
            q["bartlett_stat"] = pca_result.quality.bartlett_stat;
            q["bartlett_pvalue"] = pca_result.quality.bartlett_pvalue;
            q["cumulative_variance"] = pca_result.quality.cumulative_variance;
            q["variance_grade"] = pca_result.quality.variance_grade;
            q["condition_number"] = pca_result.quality.condition_number;
            q["cond_grade"] = pca_result.quality.cond_grade;
            q["is_positive_definite"] = pca_result.quality.is_positive_definite;
            q["reconstruction_error"] = pca_result.quality.reconstruction_error;
            q["reconstruction_error_pct"] = pca_result.quality.reconstruction_error_pct;
            ts["quality"] = q;

            json["time_series"] = ts;
        }

        res.set_content(json.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[PCAHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}
