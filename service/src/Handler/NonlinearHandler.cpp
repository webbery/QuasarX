#include "Handler/NonlinearHandler.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Algorithms/MMAR.h"
#include "Algorithms/PhaseSpace.h"
#include "server.h"
#include <sstream>

void NonlinearHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();
        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto field = req.get_param_value("field");
        auto fill_param = req.get_param_value("fill_method");

        if (symbols_param.empty()) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "symbols parameter is required";
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (field.empty()) field = "close";
        FillMethod fill = fill_param.empty() ? FillMethod::ForwardFill : parseFillMethod(fill_param);

        // MMAR 参数
        double q_min = -5.0, q_max = 5.0, q_step = 0.5;
        int min_window = 10;
        if (auto v = req.get_param_value("q_min"); !v.empty()) q_min = std::stod(v);
        if (auto v = req.get_param_value("q_max"); !v.empty()) q_max = std::stod(v);
        if (auto v = req.get_param_value("q_step"); !v.empty()) q_step = std::stod(v);
        if (auto v = req.get_param_value("min_window"); !v.empty()) min_window = std::stoi(v);

        // 相空间参数
        int embed_dim = 3, time_delay = 0, lyap_horizon = 50;
        if (auto v = req.get_param_value("embed_dim"); !v.empty()) embed_dim = std::stoi(v);
        if (auto v = req.get_param_value("time_delay"); !v.empty()) time_delay = std::stoi(v);
        if (auto v = req.get_param_value("lyapunov_horizon"); !v.empty()) lyap_horizon = std::stoi(v);
        if (embed_dim < 2) embed_dim = 2;
        if (embed_dim > 7) embed_dim = 7;
        if (lyap_horizon < 10) lyap_horizon = 10;
        if (lyap_horizon > 200) lyap_horizon = 200;

        // 解析 symbols（取第一个）
        String symbol;
        {
            std::istringstream ss(symbols_param);
            std::getline(ss, symbol, ',');
        }

        // 加载数据
        Vector<String> dates;
        Vector<double> prices;

        auto slash = symbol.find('/');
        bool is_macro = (slash != std::string::npos && symbol.size() > slash + 1 && symbol.find('.', slash) == std::string::npos);

        if (is_macro) {
            if (!FetchMacroData(symbol, db_path, dates, prices)) {
                res.status = 400;
                nlohmann::json err;
                err["error"] = fmt::format("No macro data for {}", symbol);
                res.set_content(err.dump(), "application/json");
                return;
            }
        } else {
            auto multi = LoadHistoryData(symbol, {field}, start_date, end_date, &dates, fill);
            auto it = multi.find(field);
            if (it != multi.end() && !it->second.empty()) {
                prices = it->second;
            }
        }

        if (prices.size() < 100) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = fmt::format("Insufficient data ({} points, need at least 100)", prices.size());
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 计算收益率
        int N = static_cast<int>(prices.size());
        Vector<double> returns(N - 1);
        for (int i = 0; i < N - 1; ++i) {
            if (std::abs(prices[i]) > 1e-15)
                returns[i] = (prices[i + 1] - prices[i]) / prices[i];
            else
                returns[i] = 0.0;
        }

        // 构建响应
        nlohmann::json json;
        json["symbol"] = symbol;
        json["dates"] = Vector<String>(dates.begin() + 1, dates.end());
        json["returns"] = returns;
        json["data_points"] = N - 1;

        // MMAR 分析
        auto mmar = MMAR::analyze(returns, q_min, q_max, q_step, min_window);

        nlohmann::json mmar_json;
        mmar_json["q_values"] = mmar.q_values;
        mmar_json["hq"] = mmar.hq;
        mmar_json["tau_q"] = mmar.tau_q;
        mmar_json["hurst"] = mmar.hurst;
        mmar_json["width"] = mmar.spectrum_width;

        // 多分形谱（过滤掉无效点）
        nlohmann::json spec_alpha = nlohmann::json::array();
        nlohmann::json spec_f = nlohmann::json::array();
        for (size_t i = 0; i < mmar.alpha.size(); ++i) {
            if (std::isfinite(mmar.alpha[i]) && std::isfinite(mmar.f_alpha[i])) {
                spec_alpha.push_back(mmar.alpha[i]);
                spec_f.push_back(mmar.f_alpha[i]);
            }
        }
        nlohmann::json spectrum;
        spectrum["alpha"] = spec_alpha;
        spectrum["f_alpha"] = spec_f;
        mmar_json["multifractal_spectrum"] = spectrum;
        json["mmar"] = mmar_json;

        // 相空间分析
        auto ps = PhaseSpace::analyze(returns, embed_dim, time_delay, lyap_horizon);

        nlohmann::json ps_json;
        ps_json["embed_dim"] = ps.embed_dim;
        ps_json["time_delay"] = ps.time_delay;
        ps_json["delay_method"] = ps.delay_method;
        ps_json["correlation_dimension"] = ps.correlation_dimension;
        ps_json["max_lyapunov"] = ps.max_lyapunov;
        ps_json["is_deterministic"] = ps.is_deterministic;
        ps_json["diagnosis"] = ps.diagnosis;

        // 轨迹
        nlohmann::json traj = nlohmann::json::array();
        for (const auto& pt : ps.trajectory) {
            traj.push_back(pt);
        }
        ps_json["trajectory"] = traj;
        ps_json["trajectory_time"] = ps.trajectory_time;

        // 关联积分
        ps_json["corr_r_values"] = ps.corr_r_values;
        ps_json["corr_c_values"] = ps.corr_c_values;

        // Lyapunov 发散曲线
        ps_json["lyap_divergence"] = ps.lyap_divergence;

        json["phase_space"] = ps_json;

        res.set_content(json.dump(), "application/json");
    } catch (const std::exception& e) {
        res.status = 500;
        nlohmann::json err;
        err["error"] = fmt::format("Nonlinear analysis failed: {}", e.what());
        res.set_content(err.dump(), "application/json");
    }
}
