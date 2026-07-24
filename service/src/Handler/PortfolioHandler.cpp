#include "Handler/PortfolioHandler.h"
#include "Util/QuoteDB.h"
#include "Util/finance.h"

#include <Eigen/Dense>
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

PortfolioHandler::PortfolioHandler(Server* handle)
  :HttpHandler(handle)
{
}

namespace {

/// 把 JSON 中的 securities 字符串数组解析为 vector<string>
Vector<String> parseSecurities(const nlohmann::json& j) {
    Vector<String> result;
    if (!j.is_array()) return result;
    for (const auto& s : j) {
        if (s.is_string()) result.push_back(s.get<std::string>());
    }
    return result;
}

/// 把"YYYY-MM-DD"扩成 DuckDB 可识别的"YYYY-MM-DD 00:00:00" / "23:59:59"
std::string expandStartTime(const std::string& d) {
    return d.empty() ? "" : (d + " 00:00:00");
}
std::string expandEndTime(const std::string& d) {
    return d.empty() ? "" : (d + " 23:59:59");
}

/// 加载单标的 close (HFQ 优先), 计算简单收益率序列
/// 失败抛 std::runtime_error
Vector<double> loadReturns(const std::string& symbol,
                            const std::string& table,
                            const std::string& start_date,
                            const std::string& end_date,
                            size_t min_bars = 60) {
    auto& qdb = QuoteDB::instance();
    if (!qdb.isInitialized()) {
        throw std::runtime_error("QuoteDB not initialized");
    }
    auto bars = qdb.query(table, symbol, expandStartTime(start_date),
                          expandEndTime(end_date), 100000);
    if (bars.size() < min_bars) {
        throw std::runtime_error(symbol + ": insufficient bars (" +
                                  std::to_string(bars.size()) + " < " +
                                  std::to_string(min_bars) + ")");
    }
    Vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.adj_close > 0 ? bar.adj_close : bar.close);
    }
    Vector<double> rets;
    rets.reserve(closes.size() - 1);
    for (size_t i = 1; i < closes.size(); ++i) {
        if (closes[i - 1] > 0) {
            rets.push_back((closes[i] - closes[i - 1]) / closes[i - 1]);
        }
    }
    return rets;
}

/// 对齐多标的 returns 到共同尾部 (与 finance::computeMulti 行为一致)
Eigen::MatrixXd alignToMinLength(const Vector<Vector<double>>& rets_per_sym) {
    if (rets_per_sym.empty()) return Eigen::MatrixXd();
    size_t n = rets_per_sym.size();
    size_t min_len = rets_per_sym[0].size();
    for (const auto& r : rets_per_sym) {
        min_len = std::min(min_len, r.size());
    }
    Eigen::MatrixXd out(n, static_cast<Eigen::Index>(min_len));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < min_len; ++j) {
            out(i, static_cast<Eigen::Index>(j)) =
                rets_per_sym[i][rets_per_sym[i].size() - min_len + j];
        }
    }
    return out;
}

nlohmann::json serialize(const Vector<String>& securities,
                         const finance::RiskParityResult& rp,
                         double shrinkage) {
    nlohmann::json out;
    out["model_type"] = "risk_parity";
    out["securities"] = securities;
    out["weights"] = nlohmann::json::array();
    for (Eigen::Index i = 0; i < rp._weights.size(); ++i) {
        out["weights"].push_back({
            {"code", securities[static_cast<size_t>(i)]},
            {"weight", rp._weights(i)}
        });
    }
    nlohmann::json diag;
    diag["shrinkage"] = shrinkage;
    diag["risk_contributions"] = nlohmann::json::array();
    for (Eigen::Index i = 0; i < rp._risk_contributions.size(); ++i) {
        diag["risk_contributions"].push_back(rp._risk_contributions(i));
    }
    diag["max_rc_deviation"] = rp._max_rc_deviation;
    diag["iterations"] = rp._iterations;
    diag["converged"] = rp._converged;
    out["diagnostics"] = diag;
    return out;
}

}  // namespace

void PortfolioHandler::put(const httplib::Request& req, httplib::Response& res) {
    using json = nlohmann::json;
    json resp;
    try {
        json j = json::parse(req.body);

        const std::string model_type = j.value("model_type", "");
        if (model_type != "risk_parity") {
            resp["error"] = "unsupported model_type '" + model_type +
                            "', only 'risk_parity' is implemented";
            res.status = 400;
            res.set_content(resp.dump(), "application/json");
            return;
        }

        if (!j.contains("securities")) {
            throw std::runtime_error("missing 'securities'");
        }
        Vector<String> securities = parseSecurities(j["securities"]);
        if (securities.size() < 2) {
            throw std::runtime_error("risk_parity requires at least 2 securities");
        }

        const std::string start_date = j.value("start_date", "");
        const std::string end_date = j.value("end_date", "");
        const std::string table = j.value("table", "stock_1d");

        // 加载收益率
        Vector<Vector<double>> rets_per_sym;
        rets_per_sym.reserve(securities.size());
        for (const auto& sym : securities) {
            rets_per_sym.push_back(loadReturns(sym, table, start_date, end_date));
        }

        // 对齐
        Eigen::MatrixXd ret_mat = alignToMinLength(rets_per_sym);
        if (ret_mat.cols() < 30) {
            throw std::runtime_error("aligned returns too short: T=" +
                                    std::to_string(ret_mat.cols()));
        }

        // 求解 Risk Parity (协方差自动由内部 Ledoit-Wolf OAS 收缩)
        finance::RiskParityResult rp = finance::riskParityWeights(ret_mat);
        if (rp._weights.size() == 0) {
            throw std::runtime_error("risk_parity solver returned empty weights");
        }

        // shrinkage δ 单独取一次 (复用同一 Eigen 矩阵, LW 是 O(N²T), 可忽略)
        finance::LedoitWolfResult lw = finance::ledoitWolfShrinkage(ret_mat);
        double shrinkage = lw._covariance.size() > 0 ? lw._shrinkage : 0.0;

        resp = serialize(securities, rp, shrinkage);
        res.set_content(resp.dump(), "application/json");
        res.status = 200;
    } catch (const std::exception& e) {
        resp["error"] = e.what();
        res.status = 400;
        res.set_content(resp.dump(), "application/json");
    }
}
