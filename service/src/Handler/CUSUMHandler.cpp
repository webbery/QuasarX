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
        Map<String, Vector<double>> returns_map;
        Vector<String> dates_str;
        for (auto& sym : symbols) {
            Vector<String> sym_dates;
            // 将 symbol 字符串转换为 symbol_t 类型
            symbol_t sym_t = to_symbol(toInternalSymbol(sym));
            auto data = LoadHistoryDataWithFreq(sym_t, {"close"}, start, end, target_freq, AdjType::HFQ, &sym_dates);
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
            returns_map[sym] = sym_returns;
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
        // 取所有标的收益率长度的最小值，避免不同标的上市时间不同导致越界
        size_t n_days = returns_map.begin()->second.size();
        for (auto& [sym, rets] : returns_map) {
            if (rets.size() < n_days) {
                n_days = rets.size();
            }
        }

        // 裁剪日期到共同长度
        // dates_str 含 header 行时长度为 n_days+1，需要去掉第一个元素
        if (dates_str.size() > n_days) {
            Vector<String> trimmed_dates;
            trimmed_dates.reserve(n_days);
            for (size_t i = 1; i < dates_str.size() && trimmed_dates.size() < n_days; ++i) {
                trimmed_dates.push_back(dates_str[i]);
            }
            dates_str = std::move(trimmed_dates);
        }

        Vector<double> portfolio_returns(n_days, 0.0);
        for (auto& [sym, rets] : returns_map) {
            for (size_t i = 0; i < n_days; ++i) {
                portfolio_returns[i] += rets[i] / returns_map.size();
            }
        }

        // 3. 计算波动率（收益率平方）— 组合用于方差检测
        Vector<double> squared_returns(n_days);
        for (size_t i = 0; i < n_days; ++i) {
            double port_ret = 0;
            for (auto& [sym, rets] : returns_map) {
                port_ret += rets[i] / returns_map.size();
            }
            squared_returns[i] = port_ret * port_ret;
        }

        // 3b. 计算组合均值和标准差 — 用于组合方差检测
        double port_mean_ret = 0;
        for (auto& [sym, rets] : returns_map) {
            double sym_sum = 0;
            for (double r : rets) sym_sum += r;
            port_mean_ret += sym_sum / (rets.size() * returns_map.size());
        }
        double port_var_ret = 0;
        for (size_t i = 0; i < n_days; ++i) {
            double port_ret = 0;
            for (auto& [sym, rets] : returns_map) port_ret += rets[i] / returns_map.size();
            port_var_ret += (port_ret - port_mean_ret) * (port_ret - port_mean_ret);
        }
        port_var_ret /= n_days;
        double port_sigma_ret = std::sqrt(port_var_ret);

        nlohmann::json result;

        // 日期字符串（out_dates 直接返回日期，不含 header）
        result["dates"] = dates_str;
        result["symbols"] = nlohmann::json::array();
        for (auto& [sym, _] : returns_map) {
            result["symbols"].push_back(sym);
        }

        // 4. 均值漂移 CUSUM — 对每个标的单独检测 + 组合
        if (std::find(modes.begin(), modes.end(), "mean") != modes.end()) {
            nlohmann::json per_symbol = nlohmann::json::array();

            // 对每个标的单独检测
            for (auto& [sym, rets] : returns_map) {
                if (rets.size() < min_obs) continue;

                double sym_mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
                double sym_var = 0;
                for (double r : rets) sym_var += (r - sym_mean) * (r - sym_mean);
                sym_var /= rets.size();
                double sym_sigma = std::sqrt(sym_var);

                CUSUMDetector mean_detector({
                    ._mu = sym_mean,
                    ._sigma = sym_sigma,
                    ._lambda = lambda,
                    ._threshold_multiplier = threshold_multiplier,
                    ._min_obs = min_obs,
                });
                auto cusum_result = mean_detector.detect_batch(rets);

                nlohmann::json sym_json;
                sym_json["symbol"] = sym;
                sym_json["s_pos"] = Vector<double>(cusum_result._steps.size());
                sym_json["s_neg"] = Vector<double>(cusum_result._steps.size());
                Vector<size_t> change_points;
                for (size_t i = 0; i < cusum_result._steps.size(); ++i) {
                    sym_json["s_pos"][i] = cusum_result._steps[i]._cusum_positive;
                    sym_json["s_neg"][i] = cusum_result._steps[i]._cusum_negative;
                    if (cusum_result._steps[i]._change_point) {
                        change_points.push_back(i);
                    }
                }
                sym_json["change_points"] = change_points;
                sym_json["threshold"] = mean_detector.get_config()._threshold_multiplier * mean_detector.get_config()._sigma * std::sqrt((double)cusum_result._steps.size());
                per_symbol.push_back(sym_json);
            }

            result["mean_cusum"] = per_symbol;
        }

        // 5. 方差漂移 CUSUM — 对每个标的单独检测 + 组合
        if (std::find(modes.begin(), modes.end(), "variance") != modes.end()) {
            nlohmann::json per_symbol_var = nlohmann::json::array();

            // 对每个标的单独检测
            for (auto& [sym, rets] : returns_map) {
                if (rets.size() < min_obs) continue;

                // 计算平方收益率
                Vector<double> sq_rets(rets.size());
                for (size_t i = 0; i < rets.size(); ++i) sq_rets[i] = rets[i] * rets[i];

                double sym_mean_sq = std::accumulate(sq_rets.begin(), sq_rets.end(), 0.0) / sq_rets.size();
                double sym_var_sq = 0;
                for (double s : sq_rets) sym_var_sq += (s - sym_mean_sq) * (s - sym_mean_sq);
                sym_var_sq /= sq_rets.size();
                double sym_sigma_sq = std::sqrt(sym_var_sq);

                CUSUMDetector var_detector({
                    ._mu = sym_mean_sq,
                    ._sigma = sym_sigma_sq,
                    ._lambda = lambda,
                    ._threshold_multiplier = threshold_multiplier,
                    ._min_obs = min_obs,
                });
                auto cusum_result = var_detector.detect_batch(sq_rets);

                nlohmann::json sym_json;
                sym_json["symbol"] = sym;
                sym_json["s_pos"] = Vector<double>(cusum_result._steps.size());
                sym_json["s_neg"] = Vector<double>(cusum_result._steps.size());
                Vector<size_t> change_points;
                for (size_t i = 0; i < cusum_result._steps.size(); ++i) {
                    sym_json["s_pos"][i] = cusum_result._steps[i]._cusum_positive;
                    sym_json["s_neg"][i] = cusum_result._steps[i]._cusum_negative;
                    if (cusum_result._steps[i]._change_point) {
                        change_points.push_back(i);
                    }
                }
                sym_json["change_points"] = change_points;
                sym_json["threshold"] = var_detector.get_config()._threshold_multiplier * var_detector.get_config()._sigma * std::sqrt((double)cusum_result._steps.size());
                per_symbol_var.push_back(sym_json);
            }

            result["variance_cusum"] = per_symbol_var;
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

        // 7. 变点时间轴 — 从数组中收集所有标的的变点
        nlohmann::json timeline = nlohmann::json::array();
        if (result.contains("mean_cusum") && result["mean_cusum"].is_array()) {
            for (auto& item : result["mean_cusum"]) {
                String sym = item.value("symbol", "");
                for (size_t idx : item["change_points"]) {
                    double s_pos_val = item["s_pos"][idx];
                    double s_neg_val = item["s_neg"][idx];
                    // 根据 S+ 和 S- 的相对大小判断漂移方向
                    String drift_direction = "positive";
                    double drift_value = s_pos_val;
                    if (s_neg_val > s_pos_val) {
                        drift_direction = "negative";
                        drift_value = -s_neg_val;  // 负向漂移用负值表示
                    }
                    timeline.push_back({
                        {"day", (int)idx},
                        {"type", "mean_shift"},
                        {"symbol", sym},
                        {"drift", drift_value},
                        {"s_pos", s_pos_val},
                        {"s_neg", s_neg_val},
                        {"direction", drift_direction},
                        {"action", "ewma_99"},
                    });
                }
            }
        }
        if (result.contains("variance_cusum") && result["variance_cusum"].is_array()) {
            for (auto& item : result["variance_cusum"]) {
                String sym = item.value("symbol", "");
                for (size_t idx : item["change_points"]) {
                    double s_pos_val = item["s_pos"][idx];
                    double s_neg_val = item["s_neg"][idx];
                    String drift_direction = "positive";
                    double drift_value = s_pos_val;
                    if (s_neg_val > s_pos_val) {
                        drift_direction = "negative";
                        drift_value = -s_neg_val;
                    }
                    timeline.push_back({
                        {"day", (int)idx},
                        {"type", "variance_shift"},
                        {"symbol", sym},
                        {"drift", drift_value},
                        {"s_pos", s_pos_val},
                        {"s_neg", s_neg_val},
                        {"direction", drift_direction},
                        {"action", "ewma_99_5"},
                    });
                }
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
