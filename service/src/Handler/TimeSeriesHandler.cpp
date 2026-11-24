// ──────────────────────────────────────────────────────────────────────
// 时间序列计量分析 Handler
//
// 路由: POST /v0/analysis/time_series （JSON body，支持直接传入序列）
//       GET  /v0/analysis/time_series （query 参数，按标的 + ohlcv 域取数）
//
// 本文件只做三件事: 取数 → 调 timeseries:: 的检验编排 → 序列化成 JSON。
// 所有数学计算在 Util/TimeSeries.cpp（编排）与 Util/finance.cpp（计量检验内核），
// 本文件不含任何统计算法。
//
// TODO(剥离重构计量检验工具):
//   见 Util/TimeSeries.h 顶部说明与 service/doc/time_series.md。
// ──────────────────────────────────────────────────────────────────────

#include "Handler/TimeSeriesHandler.h"
#include "Util/TimeSeries.h"
#include "Util/data.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "Util/log.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cctype>
#include <limits>

namespace {

const Set<String> kActions = {
    "adf", "kpss", "cointegration", "johansen_trace", "johansen_max_eigen", "granger"
};

/// ohlcv 域
const Set<String> kFields = {
    "close", "open", "high", "low", "volume", "turnover"
};

/// Set → "a | b | c"（错误信息用）
String joinSet(const Set<String>& values) {
    String out;
    for (const auto& v : values) {
        if (!out.empty()) out += " | ";
        out += v;
    }
    return out;
}

/// 一次请求装配好的数据集（已对齐到共同长度）
struct LoadedSeries {
    Vector<String> _names;
    Vector<String> _dates;
    Vector<Vector<double>> _columns;
};

void writeError(httplib::Response& res, int status, const String& msg) {
    res.status = status;
    res.set_content(nlohmann::json({{"error", msg}}).dump(), "application/json");
}

/// 数据来源 1: 调用方直接传入时间序列
void loadFromSeries(const nlohmann::json& params, LoadedSeries& out) {
    const auto& series = params["series"];
    for (size_t i = 0; i < series.size(); ++i) {
        Vector<double> col;
        for (const auto& v : series[i]) {
            col.push_back(v.is_number() ? v.get<double>() : 0.0);
        }
        out._columns.push_back(std::move(col));
    }

    auto names = split_list(params.value("symbols", ""));
    for (size_t i = 0; i < out._columns.size(); ++i) {
        if (i < names.size() && !names[i].empty()) {
            out._names.push_back(names[i]);
        } else {
            out._names.push_back("series_" + std::to_string(i));
        }
    }

    if (params.contains("dates") && params["dates"].is_array()) {
        for (const auto& d : params["dates"]) {
            if (d.is_string()) out._dates.push_back(d.get<String>());
        }
    }
}

/// 数据来源 2: 按标的 + ohlcv 域取数
/// 每个标的的取数复用通用加载器 LoadHistoryDataWithFreq，这里只负责收集列与公共日期轴
void loadBySymbols(const nlohmann::json& params, const String& field, LoadedSeries& out) {
    const String start_date = params.value("start_date", "");
    const String end_date = params.value("end_date", "");
    const BarFreq freq = parseBarFreq(params.value("freq", "1d"));
    const FillMethod fill = parseFillMethod(params.value("fill", "none"));

    for (const auto& symbol : split_list(params.value("symbols", ""))) {
        Vector<String> dates;
        Vector<double> values;

        symbol_t sym = to_symbol(toInternalSymbol(symbol));
        auto multi = LoadHistoryDataWithFreq(sym, {field}, start_date, end_date,
                                              freq, AdjType::HFQ, &dates, fill);
        auto it = multi.find(field);
        if (it == multi.end() || it->second.empty()) {
            WARN("[TimeSeries] No data for {} (field={})", symbol, field);
            continue;
        }

        out._names.push_back(symbol);
        out._columns.push_back(it->second);
        if (out._dates.empty()) {
            out._dates = dates;
        }
    }
}

/// 所有列与日期轴截断到共同长度
void alignSeries(LoadedSeries& data) {
    if (data._columns.empty()) return;

    size_t min_len = std::numeric_limits<size_t>::max();
    for (const auto& col : data._columns) {
        min_len = std::min(min_len, col.size());
    }
    if (min_len == std::numeric_limits<size_t>::max()) return;

    for (auto& col : data._columns) {
        col.resize(min_len);
    }
    if (data._dates.size() > min_len) {
        data._dates.resize(min_len);
    }
}

/// 列 → N 行 × T 列矩阵（行 = 变量，列 = 时间点）
Eigen::MatrixXd buildMatrix(const LoadedSeries& data) {
    int N = (int)data._columns.size();
    int T = data._columns.empty() ? 0 : (int)data._columns[0].size();
    Eigen::MatrixXd m(N, T);
    for (int i = 0; i < N; ++i) {
        for (int t = 0; t < T; ++t) {
            m(i, t) = data._columns[i][t];
        }
    }
    return m;
}

Eigen::VectorXd toVector(const Vector<double>& col) {
    Eigen::VectorXd v((Eigen::Index)col.size());
    for (size_t i = 0; i < col.size(); ++i) v((Eigen::Index)i) = col[i];
    return v;
}

std::vector<double> toStd(const Eigen::VectorXd& v) {
    return std::vector<double>(v.data(), v.data() + v.size());
}

// ──────────────────────────────────────────────────────────────────────
// JSON 序列化
// ──────────────────────────────────────────────────────────────────────

nlohmann::json adfToJson(const finance::ADFResult& r) {
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

nlohmann::json kpssToJson(const finance::KPSSResult& r) {
    return {
        {"statistic", r._statistic},
        {"p_value", r._p_value},
        {"lags", r._lags},
        {"lr_variance", r._lr_variance},
        {"is_stationary", r._is_stationary}
    };
}

nlohmann::json egToJson(const finance::EGFullResult& r) {
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
    j["ou"] = {
        {"theta", r._ou_fit._theta},
        {"mu", r._ou_fit._mu},
        {"sigma", r._ou_fit._sigma},
        {"half_life", r._ou_fit._half_life},
        {"log_likelihood", r._ou_fit._log_likelihood},
        {"aic", r._ou_fit._aic},
        {"se_theta", r._ou_fit._se_theta},
        {"se_mu", r._ou_fit._se_mu},
        {"se_sigma", r._ou_fit._se_sigma}
    };
    j["residuals"] = std::vector<double>(r._residuals.data(),
                                          r._residuals.data() + r._residuals.size());
    return j;
}

/// Johansen 迹检验部分（逐秩统计量 + 临界值 + 显著性）
nlohmann::json johansenTraceToJson(const finance::JohansenResult& r) {
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
    return j;
}

/// Johansen 最大特征值检验部分
nlohmann::json johansenMaxEigenToJson(const finance::JohansenResult& r) {
    nlohmann::json j;
    j["n_variables"] = r._n_variables;
    j["rank"] = r._rank;
    j["max_eigen_stats"] = std::vector<double>(r._max_eigen_stats.data(),
                                                r._max_eigen_stats.data() + r._max_eigen_stats.size());
    j["max_eigen_cv_95"] = std::vector<double>(r._max_eigen_cv_95.data(),
                                                r._max_eigen_cv_95.data() + r._max_eigen_cv_95.size());
    j["max_eigen_cv_99"] = std::vector<double>(r._max_eigen_cv_99.data(),
                                                r._max_eigen_cv_99.data() + r._max_eigen_cv_99.size());
    j["max_eigen_significant"] = r._max_eigen_significant;
    return j;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────
// 入口
// ──────────────────────────────────────────────────────────────────────

void TimeSeriesHandler::get(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    for (const char* key : {"action", "symbols", "field", "start_date", "end_date",
                            "freq", "fill", "on"}) {
        auto v = req.get_param_value(key);
        if (!v.empty()) params[key] = v;
    }

    auto lag = req.get_param_value("max_lag");
    if (!lag.empty()) {
        try {
            params["max_lag"] = std::stoi(lag);
        } catch (...) {
            writeError(res, 400, String("invalid max_lag: \"") + lag +
                                 "\"；期望正整数；示例: max_lag=10");
            return;
        }
    }

    handle(params, res);
}

void TimeSeriesHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        writeError(res, 400, String("invalid JSON body: ") + e.what() +
                             "；示例: {\"action\":\"adf\",\"symbols\":\"sh.600000\"}");
        return;
    }
    if (!params.is_object()) {
        writeError(res, 400, "request body must be a JSON object；"
                             "示例: {\"action\":\"adf\",\"symbols\":\"sh.600000\"}");
        return;
    }
    handle(params, res);
}

void TimeSeriesHandler::handle(const nlohmann::json& params, httplib::Response& res) {
    try {
        // ── 1. 参数 ──
        const String action = params.value("action", "");
        if (action.empty()) {
            writeError(res, 400, "missing 'action'；合法值: " + joinSet(kActions) +
                                 "；示例: {\"action\":\"adf\",\"symbols\":\"sh.600000\"}");
            return;
        }
        if (!kActions.count(action)) {
            writeError(res, 400, "unknown action: \"" + action + "\"；合法值: " + joinSet(kActions));
            return;
        }

        int max_lag = 10;
        if (params.contains("max_lag")) {
            const auto& v = params["max_lag"];
            if (!v.is_number_integer()) {
                writeError(res, 400, "invalid max_lag；期望正整数；示例: \"max_lag\": 10");
                return;
            }
            max_lag = v.get<int>();
            if (max_lag < 1) {
                writeError(res, 400, "invalid max_lag；期望正整数；示例: \"max_lag\": 10");
                return;
            }
        }

        const String on = params.value("on", "prices");
        if (on != "prices" && on != "residuals") {
            writeError(res, 400, "invalid 'on': \"" + on + "\"；合法值: prices | residuals");
            return;
        }
        const bool on_residuals = (on == "residuals");

        String field = params.value("field", "close");
        std::transform(field.begin(), field.end(), field.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (!kFields.count(field)) {
            writeError(res, 400, "invalid field: \"" + field + "\"；合法值: " + joinSet(kFields));
            return;
        }

        // ── 2. 取数 ──
        LoadedSeries data;
        const bool has_series = params.contains("series") &&
                                params["series"].is_array() &&
                                !params["series"].empty();
        if (has_series) {
            loadFromSeries(params, data);
        } else if (params.contains("symbols") && params["symbols"].is_string() &&
                   !params["symbols"].get<String>().empty()) {
            loadBySymbols(params, field, data);
        } else {
            writeError(res, 400, "missing data source；二选一: "
                                 "\"series\": [[1.0,2.0,...], ...] 或 "
                                 "\"symbols\": \"sh.600000,sh.600036\" + \"field\": one of " +
                                 joinSet(kFields));
            return;
        }

        alignSeries(data);

        if (data._columns.empty() || data._columns[0].empty()) {
            writeError(res, 400, "no data；请检查 symbols 是否已有行情（或 series 是否为空）");
            return;
        }

        // ── 3. 分派 ──
        nlohmann::json result;
        result["symbols"] = data._names;
        result["dates"] = data._dates;

        if (action == "adf" || action == "kpss") {
            nlohmann::json out = nlohmann::json::object();

            if (on_residuals) {
                if (data._columns.size() != 2) {
                    writeError(res, 400, "on=residuals 需要恰好 2 条序列；"
                                         "示例: \"symbols\": \"sh.600000,sh.600036\", \"on\": \"residuals\"");
                    return;
                }
                auto pair = timeseries::ComputeResiduals(toVector(data._columns[0]),
                                                          toVector(data._columns[1]));
                if (pair._residuals.size() < 2) {
                    writeError(res, 400, "协整回归残差为空；样本不足（至少 2 个点）");
                    return;
                }
                auto ur = timeseries::ComputeUnitRoot(pair._residuals);
                const String key = data._names[0] + "~" + data._names[1];
                if (action == "adf") {
                    out[key] = adfToJson(ur._adf);
                    result["adf"] = out;
                } else {
                    out[key] = kpssToJson(ur._kpss);
                    result["kpss"] = out;
                }
                result["pair"] = {
                    {"symbol_x", data._names[0]},
                    {"symbol_y", data._names[1]},
                    {"residuals", toStd(pair._residuals)}
                };
            } else {
                for (size_t i = 0; i < data._columns.size(); ++i) {
                    auto ur = timeseries::ComputeUnitRoot(toVector(data._columns[i]));
                    if (action == "adf") {
                        out[data._names[i]] = adfToJson(ur._adf);
                    } else {
                        out[data._names[i]] = kpssToJson(ur._kpss);
                    }
                }
                if (action == "adf") {
                    result["adf"] = out;
                } else {
                    result["kpss"] = out;
                }
            }

        } else if (action == "cointegration") {
            if (data._columns.size() < 2) {
                writeError(res, 400, "cointegration 至少需要 2 条序列");
                return;
            }
            nlohmann::json pairwise_eg = nlohmann::json::array();
            for (size_t i = 0; i < data._columns.size(); ++i) {
                for (size_t j = i + 1; j < data._columns.size(); ++j) {
                    auto eg = timeseries::ComputeEngleGranger(toVector(data._columns[i]),
                                                               toVector(data._columns[j]));
                    eg._symbol_x = data._names[i];
                    eg._symbol_y = data._names[j];
                    pairwise_eg.push_back(egToJson(eg));
                }
            }
            result["pairwise_eg"] = pairwise_eg;

        } else if (action == "johansen_trace" || action == "johansen_max_eigen") {
            if (data._columns.size() < 2) {
                writeError(res, 400, action + " 至少需要 2 条序列");
                return;
            }
            auto joh = timeseries::ComputeJohansen(buildMatrix(data));
            if (joh._n_variables < 2) {
                writeError(res, 400, "Johansen 计算失败；样本不足（T 需大于 N*lag+10）");
                return;
            }
            result["johansen"] = (action == "johansen_trace")
                ? johansenTraceToJson(joh)
                : johansenMaxEigenToJson(joh);

        } else { // granger
            if (data._columns.size() < 2) {
                writeError(res, 400, "granger 至少需要 2 条序列");
                return;
            }
            auto matrix = buildMatrix(data);

            nlohmann::json pairwise = nlohmann::json::array();
            for (const auto& g : timeseries::ComputePairwiseGranger(matrix, max_lag)) {
                pairwise.push_back({
                    {"from", data._names[g._cause_index]},
                    {"to", data._names[g._effect_index]},
                    {"f_statistic", g._f_statistic},
                    {"p_value", g._p_value},
                    {"is_significant", g._is_significant},
                    {"optimal_lag", g._optimal_lag}
                });
            }

            nlohmann::json granger;
            granger["pairwise"] = pairwise;

            if (data._columns.size() >= 3) {
                nlohmann::json mv = nlohmann::json::array();
                for (const auto& g : timeseries::ComputeMultivariateGranger(matrix, max_lag)) {
                    nlohmann::json conditions = nlohmann::json::array();
                    for (auto idx : g._condition_indices) {
                        if (idx < data._names.size()) conditions.push_back(data._names[idx]);
                    }
                    mv.push_back({
                        {"from", data._names[g._cause_index]},
                        {"to", data._names[g._effect_index]},
                        {"wald_stat", g._wald_stat},
                        {"p_value", g._p_value},
                        {"optimal_lag", g._optimal_lag},
                        {"is_significant", g._is_significant},
                        {"condition_set", conditions}
                    });
                }
                granger["multivariate"] = mv;
            }
            result["granger"] = granger;
        }

        res.set_content(result.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[TimeSeries] {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json({{"error", String("time_series 计算失败: ") + e.what()}}).dump(),
                        "application/json");
    }
}
