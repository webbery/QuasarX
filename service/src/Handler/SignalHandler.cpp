#include "Handler/SignalHandler.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "Algorithms/EMD_SIMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Util/datetime.h"
#include "server.h"
#include <sstream>

void SignalHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();
        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto field = req.get_param_value("field");
        auto method = req.get_param_value("method");
        auto num_imfs_param = req.get_param_value("num_imfs");
        auto fill_param = req.get_param_value("fill_method");
        auto rolling_window_param = req.get_param_value("rolling_window");

        if (symbols_param.empty()) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "symbols parameter is required";
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (field.empty()) field = "close";
        if (method.empty()) method = "emd";
        FillMethod fill = fill_param.empty() ? FillMethod::None : parseFillMethod(fill_param);

        int num_imfs = 5;
        if (!num_imfs_param.empty()) {
            try { num_imfs = std::stoi(num_imfs_param); } catch (...) {}
        }
        if (num_imfs < 1 || num_imfs > 20) num_imfs = 5;

        int rolling_window = 0;
        if (!rolling_window_param.empty()) {
            try { rolling_window = std::stoi(rolling_window_param); } catch (...) {}
        }
        if (rolling_window < 0) rolling_window = 0;

        // 解析 symbols（逗号分隔）
        std::vector<String> symbols;
        std::istringstream ss(symbols_param);
        String sym;
        while (std::getline(ss, sym, ',')) {
            if (!sym.empty()) symbols.push_back(sym);
        }

        // 构建响应
        nlohmann::json json;
        json["symbols"] = symbols;
        json["field"] = field;
        json["method"] = method;

        Vector<String> dates;
        Vector<double> original;

        // 只处理第一个 symbol（单标的分析）
        if (!symbols.empty()) {
            // 检测是否为宏观指标 (格式: country/indicator)
            auto slash = symbols[0].find('/');
            bool is_macro = (slash != std::string::npos && symbols[0].size() > slash + 1 && symbols[0].find('.', slash) == std::string::npos);

            if (is_macro) {
                // 宏观指标数据
                Vector<String> macro_dates;
                Vector<double> macro_prices;
                if (!FetchMacroData(symbols[0], db_path, macro_dates, macro_prices)) {
                    res.status = 400;
                    nlohmann::json err;
                    err["error"] = fmt::format("No macro data for {}", symbols[0]);
                    res.set_content(err.dump(), "application/json");
                    return;
                }
                dates = macro_dates;
                original = macro_prices;
            } else {
                // 股票/ETF行情数据
                auto multi = LoadHistoryData(symbols[0], {"close", "open", "high", "low", "volume", "turnover"},
                                              start_date, end_date, &dates, fill);
                auto it = multi.find(field);
                if (it != multi.end() && !it->second.empty()) {
                    original = it->second;
                }
            }
            json["dates"] = dates;
            json["original"] = original;
        }

        if (original.size() < 10) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "Insufficient data (need at least 10 points)";
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 执行 EMD 或 CEEMDAN 分解
        nlohmann::json info_json = nlohmann::json::array();
        if (method == "ceemdan") {
            CEEMDAN ceemdan;
            CEEMDAN::Config cfg;
            cfg.numIMFs = num_imfs;
            cfg.ensembles = 30;
            cfg.noiseStd = 0.2;
            cfg.maxSiftingIter = 10;
            cfg.sdThreshold = 0.02;

            auto result = ceemdan.decompose(original, cfg);

            nlohmann::json imf_json = nlohmann::json::array();
            for (size_t i = 0; i < result.imfs.size(); ++i) {
                imf_json.push_back(result.imfs[i]);

                nlohmann::json info;
                info["index"] = static_cast<int>(i) + 1;
                info["mean_period"] = finance::estimateMeanPeriod(result.imfs[i]);
                info["energy_pct"] = finance::computeEnergyPct(result.imfs[i], original);
                info_json.push_back(info);
            }

            json["imf_components"] = imf_json;
            json["residual"] = result.residual;
            json["imf_info"] = info_json;
            json["reconstruction_error"] = result.reconstructionError;
        } else {
            // 默认 EMD
            auto imfs = simd_emd(original, num_imfs, 10, 0.02);

            nlohmann::json imf_json = nlohmann::json::array();

            Vector<double> residual = original;
            for (size_t i = 0; i < imfs.size(); ++i) {
                imf_json.push_back(imfs[i]);

                // 计算残差
                int sz = static_cast<int>(residual.size());
                for (int j = 0; j < sz; ++j) {
                    residual[j] -= imfs[i][j];
                }

                nlohmann::json info;
                info["index"] = static_cast<int>(i) + 1;
                info["mean_period"] = finance::estimateMeanPeriod(imfs[i]);
                info["energy_pct"] = finance::computeEnergyPct(imfs[i], original);
                info_json.push_back(info);
            }

            json["imf_components"] = imf_json;
            json["residual"] = residual;
            json["imf_info"] = info_json;

            // 重建误差
            Vector<double> recon = residual;
            for (const auto& imf : imfs) {
                int sz = static_cast<int>(recon.size());
                for (int j = 0; j < sz; ++j) {
                    recon[j] += imf[j];
                }
            }
            double rms = 0;
            for (size_t i = 0; i < original.size(); ++i) {
                double d = recon[i] - original[i];
                rms += d * d;
            }
            json["reconstruction_error"] = std::sqrt(rms / original.size());
        }

        // ─── 滚动 EMD 能量分析（信号结构稳定性追踪） ───
        if (rolling_window > 0 && (int)original.size() > rolling_window) {
            int rw = std::min(rolling_window, (int)original.size() / 2);
            auto energy_matrix = finance::computeRollingEMDEnergy(original, rw, num_imfs, dates);

            if (!energy_matrix.empty() && !energy_matrix[0].empty()) {
                int out_len = (int)energy_matrix[0].size();
                nlohmann::json rolling;
                rolling["window"] = rw;
                rolling["dates"] = Vector<String>(
                    dates.begin() + (rw - 1),
                    dates.end()
                );

                // 每个 IMF 的能量占比时间序列
                nlohmann::json by_imf = nlohmann::json::array();
                for (int i = 0; i < num_imfs; ++i) {
                    by_imf.push_back(energy_matrix[i]);
                }
                rolling["by_imf_energy"] = by_imf;
                rolling["residual_energy"] = energy_matrix[num_imfs];

                // 总能量（≈1.0，用于验证）+ 总能量滚动变化率
                Vector<double> total_energy(out_len, 0.0);
                for (int i = 0; i < num_imfs + 1 && i < (int)energy_matrix.size(); ++i) {
                    for (int j = 0; j < out_len; ++j) {
                        total_energy[j] += energy_matrix[i][j];
                    }
                }
                rolling["total_energy"] = total_energy;

                // 总能量变化率（相邻窗口的变化百分比）
                Vector<double> change_rate(out_len, 0.0);
                for (int j = 1; j < out_len; ++j) {
                    if (total_energy[j - 1] > 1e-10) {
                        change_rate[j] = (total_energy[j] - total_energy[j - 1]) / total_energy[j - 1] * 100.0;
                    }
                }
                rolling["change_rate"] = change_rate;

                json["rolling"] = rolling;
            }
        }

        // ─── 最低频 IMF 能量 / 成交量（量价分离信号） ───
        // 找最低频 IMF（mean_period 最大）
        if (!info_json.empty()) {
            int lowest_idx = 0;
            double max_period = 0;
            for (size_t i = 0; i < info_json.size(); ++i) {
                double p = info_json[i].value("mean_period", 0.0);
                if (p > max_period) {
                    max_period = p;
                    lowest_idx = (int)i;
                }
            }

            // 拉取成交量数据
            Vector<double> volume_series;
            try {
                Vector<String> vol_dates;
                auto vol_multi = LoadHistoryData(symbols[0], {"volume"},
                                                  start_date, end_date,
                                                  &vol_dates, fill);
                auto vit = vol_multi.find("volume");
                if (vit != vol_multi.end()) {
                    volume_series = vit->second;
                }
            } catch (...) {}

            // 如果 rolling 已存在，从其中提取最低频 IMF 的能量时间序列
            if (json.contains("rolling") && json["rolling"].contains("by_imf_energy")
                && lowest_idx < (int)json["rolling"]["by_imf_energy"].size()
                && !volume_series.empty()) {

                const auto& energy_series = json["rolling"]["by_imf_energy"][lowest_idx];
                int energy_len = (int)energy_series.size();

                // 成交量标准化（min-max to [0, 1]）
                double vmin = volume_series[0], vmax = volume_series[0];
                for (auto v : volume_series) {
                    if (v < vmin) vmin = v;
                    if (v > vmax) vmax = v;
                }
                double vrange = vmax - vmin;
                Vector<double> vol_norm(volume_series.size(), 0.0);
                if (vrange > 1e-10) {
                    for (size_t i = 0; i < volume_series.size(); ++i) {
                        vol_norm[i] = (volume_series[i] - vmin) / vrange;
                    }
                }

                // 对齐：能量序列对应 window-1..N-1，成交量取 [window-1..N-1]
                int offset = rolling_window - 1;
                int ratio_len = energy_len;
                Vector<double> ratio(ratio_len, 0.0);
                for (int i = 0; i < ratio_len; ++i) {
                    int vol_idx = offset + i;
                    if (vol_idx >= 0 && vol_idx < (int)vol_norm.size() && vol_norm[vol_idx] > 1e-10) {
                        ratio[i] = energy_series[i].get<double>() / vol_norm[vol_idx];
                    }
                }

                nlohmann::json lowest_freq;
                lowest_freq["imf_index"] = lowest_idx;
                lowest_freq["imf_mean_period"] = max_period;
                lowest_freq["energy_series"] = energy_series;
                lowest_freq["volume_normalized"] = Vector<double>(
                    vol_norm.begin() + offset,
                    vol_norm.end()
                );
                lowest_freq["energy_to_volume_ratio"] = ratio;

                json["lowest_freq"] = lowest_freq;
            }
        }

        res.set_content(json.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[SignalHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}
