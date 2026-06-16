#include "Handler/SignalHandler.h"
#include "Util/system.h"
#include "Util/finance.h"
#include "Algorithms/EMD_SIMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Util/datetime.h"
#include <sstream>

void SignalHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto field = req.get_param_value("field");
        auto method = req.get_param_value("method");
        auto num_imfs_param = req.get_param_value("num_imfs");

        if (symbols_param.empty()) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "symbols parameter is required";
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (field.empty()) field = "close";
        if (method.empty()) method = "emd";

        int num_imfs = 5;
        if (!num_imfs_param.empty()) {
            try { num_imfs = std::stoi(num_imfs_param); } catch (...) {}
        }
        if (num_imfs < 1 || num_imfs > 20) num_imfs = 5;

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
            Vector<String> dates;
            auto multi = LoadColumnDataMulti(symbols[0], {"close", "open", "high", "low", "volume", "turnover"},
                                              start_date, end_date, &dates);

            auto it = multi.find(field);
            if (it != multi.end() && !it->second.empty()) {
                original = it->second;
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
            nlohmann::json info_json = nlohmann::json::array();
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
            nlohmann::json info_json = nlohmann::json::array();

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

        res.set_content(json.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[SignalHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}
