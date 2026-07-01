#include "Handler/CUSUMHandler.h"
#include "Util/data.h"
#include "KBarBuilder.h"
#include "Metric/CUSUMDetector.h"
#include "Metric/RiskMetric.h"
#include "Util/log.h"
#include "json.hpp"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <numeric>

// === Eigen 辅助函数 ===

/**
 * @brief 将 Eigen 矩阵转换为 JSON 数组
 */
static nlohmann::json eigen_matrix_to_json(const Eigen::MatrixXd& mat) {
    nlohmann::json result = nlohmann::json::array();
    for (int i = 0; i < mat.rows(); ++i) {
        nlohmann::json row = nlohmann::json::array();
        for (int j = 0; j < mat.cols(); ++j) {
            row.push_back(mat(i, j));
        }
        result.push_back(row);
    }
    return result;
}

void CUSUMHandler::get(const httplib::Request& req, httplib::Response& res) {
    res.set_content(R"({"error": "Use POST to run CUSUM analysis"})", "application/json");
}

void CUSUMHandler::post(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        auto symbols = body["symbols"].get<std::vector<String>>();
        auto modes = body["modes"].get<std::vector<String>>();
        String start = body.value("start", "");
        String end = body.value("end", "");
        String freq = body.value("freq", "Day");  // 默认日线
        double lambda = body.value("lambda", 0.5);
        double threshold_multiplier = body.value("threshold_multiplier", 4.0);
        size_t min_obs = body.value("min_obs", 30);

        if (symbols.empty()) {
            res.status = 400;
            res.set_content(R"({"error": "symbols is empty"})", "application/json");
            return;
        }

        // 解析频率
        BarFreq target_freq = BarFreq::Day;
        if (freq == "1m") target_freq = BarFreq::Min1;
        else if (freq == "5m") target_freq = BarFreq::Min5;
        else if (freq == "15m") target_freq = BarFreq::Min15;
        else if (freq == "30m") target_freq = BarFreq::Min30;
        else if (freq == "1h") target_freq = BarFreq::Hour1;

        // 1. 加载历史数据（各标的收盘价）
        Map<symbol_t, Vector<double>> returns_map;
        Vector<String> dates_str;
        for (auto& sym : symbols) {
            Vector<String> sym_dates;
            auto data = LoadHistoryDataWithFreq(sym, {"close"}, start, end, target_freq, AdjType::HFQ, &sym_dates);
            auto close_it = data.find("close");
            if (close_it == data.end() || close_it->second.size() < 2) {
                WARN("[CUSUM] Insufficient data for {}", sym);
                continue;
            }
            const auto& closes = close_it->second;
            Vector<double> sym_returns;
            sym_returns.reserve(closes.size() - 1);
            for (size_t i = 1; i < closes.size(); ++i) {
                double ret = (closes[i] - closes[i-1]) / closes[i-1];
                sym_returns.push_back(ret);
            }
            returns_map[to_symbol(sym)] = sym_returns;
            if (dates_str.empty()) {
                // 使用第一个有效标的的日期
                dates_str = sym_dates;
            }
        }

        if (returns_map.empty()) {
            res.status = 404;
            res.set_content(R"({"error": "No data found for symbols"})", "application/json");
            return;
        }

        // 2. 计算组合平均收益率（等权）
        size_t n_days = returns_map.begin()->second.size();
        Vector<double> portfolio_returns(n_days, 0.0);
        for (auto& [sym, rets] : returns_map) {
            for (size_t i = 0; i < n_days; ++i) {
                portfolio_returns[i] += rets[i] / returns_map.size();
            }
        }

        // 3. 计算波动率（收益率平方）
        Vector<double> squared_returns(n_days);
        for (size_t i = 0; i < n_days; ++i) {
            squared_returns[i] = portfolio_returns[i] * portfolio_returns[i];
        }

        // 计算全局均值和标准差
        double mean_ret = std::accumulate(portfolio_returns.begin(), portfolio_returns.end(), 0.0) / n_days;
        double var_ret = 0;
        for (double r : portfolio_returns) var_ret += (r - mean_ret) * (r - mean_ret);
        var_ret /= n_days;
        double sigma_ret = std::sqrt(var_ret);

        nlohmann::json result;

        // 日期字符串（out_dates 直接返回日期，不含 header）
        result["dates"] = dates_str;

        // 4. 均值漂移 CUSUM
        if (std::find(modes.begin(), modes.end(), "mean") != modes.end()) {
            CUSUMDetector mean_detector({
                .mu = mean_ret,
                .sigma = sigma_ret,
                .lambda = lambda,
                .threshold_multiplier = threshold_multiplier,
                .min_obs = min_obs,
            });
            auto cusum_result = mean_detector.detect_batch(portfolio_returns);

            nlohmann::json mean_json;
            mean_json["s_pos"] = Vector<double>(cusum_result.steps.size());
            mean_json["s_neg"] = Vector<double>(cusum_result.steps.size());
            Vector<size_t> change_points;
            for (size_t i = 0; i < cusum_result.steps.size(); ++i) {
                mean_json["s_pos"][i] = cusum_result.steps[i].cusum_positive;
                mean_json["s_neg"][i] = cusum_result.steps[i].cusum_negative;
                if (cusum_result.steps[i].change_point) {
                    change_points.push_back(i);
                }
            }
            mean_json["change_points"] = change_points;
            mean_json["threshold"] = mean_detector.get_config().threshold_multiplier * mean_detector.get_config().sigma * std::sqrt((double)cusum_result.steps.size());
            result["mean_cusum"] = mean_json;
        }

        // 5. 方差漂移 CUSUM
        if (std::find(modes.begin(), modes.end(), "variance") != modes.end()) {
            double mean_sq = std::accumulate(squared_returns.begin(), squared_returns.end(), 0.0) / n_days;
            double var_sq = 0;
            for (double s : squared_returns) var_sq += (s - mean_sq) * (s - mean_sq);
            var_sq /= n_days;
            double sigma_sq = std::sqrt(var_sq);

            CUSUMDetector var_detector({
                .mu = mean_sq,
                .sigma = sigma_sq,
                .lambda = lambda,
                .threshold_multiplier = threshold_multiplier,
                .min_obs = min_obs,
            });
            auto cusum_result = var_detector.detect_batch(squared_returns);

            nlohmann::json var_json;
            var_json["s_pos"] = Vector<double>(cusum_result.steps.size());
            var_json["s_neg"] = Vector<double>(cusum_result.steps.size());
            Vector<size_t> change_points;
            for (size_t i = 0; i < cusum_result.steps.size(); ++i) {
                var_json["s_pos"][i] = cusum_result.steps[i].cusum_positive;
                var_json["s_neg"][i] = cusum_result.steps[i].cusum_negative;
                if (cusum_result.steps[i].change_point) {
                    change_points.push_back(i);
                }
            }
            var_json["change_points"] = change_points;
            var_json["threshold"] = var_detector.get_config().threshold_multiplier * var_detector.get_config().sigma * std::sqrt((double)cusum_result.steps.size());
            result["variance_cusum"] = var_json;
        }

        // 6. 相关性结构变化（使用 Eigen + RiskMetric 函数）
        if (std::find(modes.begin(), modes.end(), "correlation") != modes.end() && returns_map.size() >= 2) {
            size_t n_assets = returns_map.size();
            size_t window = std::min((size_t)60, n_days);

            // 构建收益率矩阵 (n_assets × n_days)
            Eigen::MatrixXd ret_matrix(n_assets, n_days);
            size_t row = 0;
            for (auto& [sym, rets] : returns_map) {
                for (size_t i = 0; i < n_days; ++i) {
                    ret_matrix(row, i) = rets[i];
                }
                ++row;
            }

            // 计算滚动平均相关性
            Vector<double> rolling_corr;
            for (size_t i = window; i <= n_days; ++i) {
                Eigen::MatrixXd window_data = ret_matrix.rightCols(i).leftCols(window);
                // 使用 RiskMetric::compute_correlation_matrix
                Eigen::MatrixXd corr = compute_correlation_matrix(window_data);
                // 取上三角平均（排除对角线）
                double sum_corr = 0;
                int count = 0;
                for (size_t a = 0; a < n_assets; ++a) {
                    for (size_t b = a + 1; b < n_assets; ++b) {
                        sum_corr += corr(a, b);
                        ++count;
                    }
                }
                rolling_corr.push_back(count > 0 ? sum_corr / count : 0.0);
            }

            nlohmann::json corr_json;
            corr_json["rolling_avg"] = rolling_corr;
            corr_json["symbols"] = symbols;

            // 变点前 vs 变点后相关性矩阵对比
            if (n_days >= 120) {
                Eigen::MatrixXd before_data = ret_matrix.leftCols(60);
                Eigen::MatrixXd after_data = ret_matrix.rightCols(60);
                corr_json["matrix_before"] = eigen_matrix_to_json(compute_correlation_matrix(before_data));
                corr_json["matrix_after"] = eigen_matrix_to_json(compute_correlation_matrix(after_data));
            } else {
                // 数据不足时，用前半段 vs 后半段
                size_t half = n_days / 2;
                Eigen::MatrixXd before_data = ret_matrix.leftCols(half);
                Eigen::MatrixXd after_data = ret_matrix.rightCols(n_days - half);
                corr_json["matrix_before"] = eigen_matrix_to_json(compute_correlation_matrix(before_data));
                corr_json["matrix_after"] = eigen_matrix_to_json(compute_correlation_matrix(after_data));
            }

            corr_json["change_points"] = Vector<size_t>();
            result["correlation"] = corr_json;
        }

        // 7. 变点时间轴
        nlohmann::json timeline = nlohmann::json::array();
        if (result.contains("mean_cusum")) {
            for (size_t idx : result["mean_cusum"]["change_points"]) {
                timeline.push_back({
                    {"day", (int)idx},
                    {"type", "mean_shift"},
                    {"drift", result["mean_cusum"]["s_pos"][idx]},
                    {"action", "ewma_99"},
                });
            }
        }
        if (result.contains("variance_cusum")) {
            for (size_t idx : result["variance_cusum"]["change_points"]) {
                timeline.push_back({
                    {"day", (int)idx},
                    {"type", "variance_shift"},
                    {"drift", result["variance_cusum"]["s_pos"][idx]},
                    {"action", "ewma_99_5"},
                });
            }
        }
        result["timeline"] = timeline;

        res.set_content(result.dump(), "application/json");

    } catch (const std::exception& e) {
        WARN("[CUSUMHandler] Error: {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
    }
}
