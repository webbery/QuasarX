#include "Handler/CointegrationHandler.h"
#include "Util/data.h"
#include "server.h"
#include <Eigen/Dense>
#include <algorithm>
#include <numeric>

using namespace finance;
// ──────────────────────────────────────────────────────────────────────
// 数据加载
// ──────────────────────────────────────────────────────────────────────

static bool loadSymbolData(const String& db_path, const String& symbol,
                           const String& start_date, const String& end_date,
                           Vector<String>& dates, Vector<double>& prices)
{
    symbol_t sym = to_symbol(toInternalSymbol(symbol));
    auto multi = LoadHistoryDataWithFreq(sym, {"close"},
                                          start_date, end_date,
                                          BarFreq::Day, AdjType::HFQ, &dates);
    auto it = multi.find("close");
    if (it == multi.end() || it->second.empty()) return false;
    prices.assign(it->second.begin(), it->second.end());
    return true;
}

// ──────────────────────────────────────────────────────────────────────
// JSON 序列化辅助
// ──────────────────────────────────────────────────────────────────────

static nlohmann::json adfToJson(const ADFResult& r) {
    return {
        {"statistic", r._statistic},
        {"p_value", r._p_value},
        {"cv_1pct", r._cv_1pct},
        {"cv_5pct", r._cv_5pct},
        {"cv_10pct", r._cv_10pct},
        {"lags", r._lags},
        {"is_stationary", r._is_stationary}
    };
}

static nlohmann::json kpssToJson(const KPSSResult& r) {
    return {
        {"statistic", r._statistic},
        {"p_value", r._p_value},
        {"lags", r._lags},
        {"lr_variance", r._lr_variance},
        {"is_stationary", r._is_stationary}
    };
}

static nlohmann::json ouToJson(const OUProcessResult& r) {
    return {
        {"theta", r._theta},
        {"mu", r._mu},
        {"sigma", r._sigma},
        {"half_life", r._half_life},
        {"log_likelihood", r._log_likelihood},
        {"aic", r._aic},
        {"se_theta", r._se_theta},
        {"se_mu", r._se_mu},
        {"se_sigma", r._se_sigma}
    };
}

static nlohmann::json egToJson(const EGFullResult& r) {
    nlohmann::json j;
    j["symbol_x"] = r._symbol_x;
    j["symbol_y"] = r._symbol_y;
    j["alpha"] = r._alpha;
    j["beta"] = r._beta;
    j["r_squared"] = r._r_squared;
    j["adf"] = adfToJson(r._adf);
    j["kpss"] = kpssToJson(r._kpss);
    j["half_life"] = r._half_life;
    j["is_cointegrated"] = r._is_cointegrated;
    j["ou"] = ouToJson(r._ou_fit);
    // 残差序列
    std::vector<double> res_vec(r._residuals.data(),
                                 r._residuals.data() + r._residuals.size());
    j["residuals"] = res_vec;
    return j;
}

static nlohmann::json johansenToJson(const JohansenResult& r) {
    nlohmann::json j;
    j["n_variables"] = r._n_variables;
    j["rank"] = r._rank;
    j["trace_stats"] = std::vector<double>(r._trace_stats.data(),
                                            r._trace_stats.data() + r._trace_stats.size());
    j["trace_cv_95"] = std::vector<double>(r._trace_cv_95.data(),
                                            r._trace_cv_95.data() + r._trace_cv_95.size());
    j["trace_cv_99"] = std::vector<double>(r._trace_cv_99.data(),
                                            r._trace_cv_99.data() + r._trace_cv_99.size());
    j["trace_significant"] = r._trace_significant;
    j["max_eigen_stats"] = std::vector<double>(r._max_eigen_stats.data(),
                                                r._max_eigen_stats.data() + r._max_eigen_stats.size());
    j["max_eigen_cv_95"] = std::vector<double>(r._max_eigen_cv_95.data(),
                                                r._max_eigen_cv_95.data() + r._max_eigen_cv_95.size());
    j["max_eigen_cv_99"] = std::vector<double>(r._max_eigen_cv_99.data(),
                                                r._max_eigen_cv_99.data() + r._max_eigen_cv_99.size());
    j["max_eigen_significant"] = r._max_eigen_significant;
    // 特征向量矩阵
    std::vector<std::vector<double>> evecs(r._eigenvectors.rows(),
        std::vector<double>(r._eigenvectors.cols()));
    for (int i = 0; i < r._eigenvectors.rows(); ++i)
        for (int j = 0; j < r._eigenvectors.cols(); ++j)
            evecs[i][j] = r._eigenvectors(i, j);
    j["eigenvectors"] = evecs;
    return j;
}

static nlohmann::json grangerToJson(const MultivariateGrangerResult& r) {
    return {
        {"from", r._from},
        {"to", r._to},
        {"wald_stat", r._wald_stat},
        {"p_value", r._p_value},
        {"optimal_lag", r._optimal_lag},
        {"is_significant", r._is_significant},
        {"condition_set", r._condition_set}
    };
}

// ──────────────────────────────────────────────────────────────────────
// GET /v0/analysis/cointegration
// ──────────────────────────────────────────────────────────────────────

void CointegrationHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();

        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto max_lag_str = req.get_param_value("max_lag");
        int max_lag = max_lag_str.empty() ? 10 : std::stoi(max_lag_str);

        if (symbols_param.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"symbols parameter required"})", "application/json");
            return;
        }

        // 解析标的列表
        Vector<String> symbols;
        {
            std::istringstream ss(symbols_param);
            String token;
            while (std::getline(ss, token, ',')) {
                if (!token.empty()) symbols.push_back(token);
            }
        }

        if (symbols.size() < 2) {
            res.status = 400;
            res.set_content(R"({"error":"at least 2 symbols required"})", "application/json");
            return;
        }

        // 加载数据
        std::map<String, Vector<double>> price_map;
        Vector<String> valid_symbols;
        Vector<String> common_dates;

        for (const auto& sym : symbols) {
            Vector<String> dates;
            Vector<double> prices;
            if (!loadSymbolData(db_path, sym, start_date, end_date, dates, prices)) {
                WARN("[Cointegration] No data for {}", sym);
                continue;
            }
            price_map[sym] = prices;
            valid_symbols.push_back(sym);
            if (common_dates.empty()) {
                common_dates = dates;
            }
        }

        if (valid_symbols.size() < 2) {
            res.status = 400;
            res.set_content(R"({"error":"insufficient valid symbols with data"})", "application/json");
            return;
        }

        // 截断到共同长度
        size_t min_len = std::numeric_limits<size_t>::max();
        for (const auto& [sym, prices] : price_map) {
            min_len = std::min(min_len, prices.size());
        }
        for (auto& [sym, prices] : price_map) {
            prices.resize(min_len);
        }

        nlohmann::json result;

        // ── 1. 单位根检验 (各标的 ADF + KPSS) ──
        nlohmann::json unit_root;
        for (const auto& sym : valid_symbols) {
            const auto& prices = price_map[sym];
            finance::ADFResult adf = finance::adfTestFull(prices, -1, "c");
            finance::KPSSResult kpss = finance::kpssTest(prices, -1, "level");
            unit_root[sym] = {
                {"adf", adfToJson(adf)},
                {"kpss", kpssToJson(kpss)}
            };
        }
        result["unit_root"] = unit_root;

        // ── 2. 二元 Engle-Granger 两步法 ──
        nlohmann::json pairwise_eg = nlohmann::json::array();
        for (size_t i = 0; i < valid_symbols.size(); ++i) {
            for (size_t j = i + 1; j < valid_symbols.size(); ++j) {
                auto eg = finance::engleGrangerFull(
                    price_map[valid_symbols[i]],
                    price_map[valid_symbols[j]],
                    valid_symbols[i], valid_symbols[j]);
                pairwise_eg.push_back(egToJson(eg));
            }
        }
        result["pairwise_eg"] = pairwise_eg;

        // ── 3. Johansen 多元协整 (≥3 标的) ──
        if (valid_symbols.size() >= 3) {
            int N = (int)valid_symbols.size();
            int T = (int)min_len;
            Eigen::MatrixXd data(N, T);
            for (int i = 0; i < N; ++i) {
                const auto& prices = price_map[valid_symbols[i]];
                for (int t = 0; t < T; ++t) {
                    data(i, t) = prices[t];
                }
            }
            auto joh = finance::johansenTest(data, 1, "const");
            result["johansen"] = johansenToJson(joh);
        }

        // ── 4. Granger 因果检验 ──
        nlohmann::json granger;

        // 4a. 逐对 Granger (二元 F 检验)
        nlohmann::json pairwise_granger = nlohmann::json::array();
        for (size_t i = 0; i < valid_symbols.size(); ++i) {
            for (size_t j = i + 1; j < valid_symbols.size(); ++j) {
                // 双向检验
                auto g1 = finance::grangerCausalityTest(
                    price_map[valid_symbols[i]], price_map[valid_symbols[j]],
                    max_lag, valid_symbols[i], valid_symbols[j]);
                auto g2 = finance::grangerCausalityTest(
                    price_map[valid_symbols[j]], price_map[valid_symbols[i]],
                    max_lag, valid_symbols[j], valid_symbols[i]);

                pairwise_granger.push_back(nlohmann::json{
                    {"from", g1.direction},
                    {"f_statistic", g1.f_statistic},
                    {"p_value", g1.p_value},
                    {"is_significant", g1.is_significant},
                    {"optimal_lag", g1.optimal_lag}
                });
                pairwise_granger.push_back(nlohmann::json{
                    {"from", g2.direction},
                    {"f_statistic", g2.f_statistic},
                    {"p_value", g2.p_value},
                    {"is_significant", g2.is_significant},
                    {"optimal_lag", g2.optimal_lag}
                });
            }
        }
        granger["pairwise"] = pairwise_granger;

        // 4b. 多元 Granger (VAR + Wald, ≥3 标的)
        if (valid_symbols.size() >= 3) {
            int N = (int)valid_symbols.size();
            int T = (int)min_len;
            Eigen::MatrixXd data(N, T);
            for (int i = 0; i < N; ++i) {
                const auto& prices = price_map[valid_symbols[i]];
                for (int t = 0; t < T; ++t) {
                    data(i, t) = prices[t];
                }
            }
            auto mv_granger = finance::multivariateGrangerTest(data, valid_symbols, max_lag);
            nlohmann::json mv_arr = nlohmann::json::array();
            for (const auto& g : mv_granger) {
                mv_arr.push_back(grangerToJson(g));
            }
            granger["multivariate"] = mv_arr;
        }

        result["granger"] = granger;
        result["symbols"] = valid_symbols;
        result["dates"] = common_dates;

        res.set_content(result.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[Cointegration] {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
    }
}
