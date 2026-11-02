#include "Handler/VolatilityHandler.h"
#include "Algorithms/ARModel.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "boost/math/statistics/ljung_box.hpp"
#include "server.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {

const char* decayModeToString(ACFDecayMode mode) {
    switch (mode) {
        case ACFDecayMode::Exponential:  return "exponential";
        case ACFDecayMode::Hyperbolic:   return "hyperbolic";
        case ACFDecayMode::Inconclusive: return "inconclusive";
    }
    return "unknown";
}

String normalizePriceField(const std::string& field) {
    if (field == "open" || field == "O") return "open";
    if (field == "high" || field == "H") return "high";
    if (field == "low" || field == "L") return "low";
    if (field == "volume" || field == "V") return "volume";
    return "close";
}

double meanOf(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}

double stddevOf(const std::vector<double>& data, double m = -1) {
    if (data.size() < 2) return 0.0;
    if (m < 0) m = meanOf(data);
    double sq_sum = 0;
    for (auto v : data) sq_sum += (v - m) * (v - m);
    return std::sqrt(sq_sum / (data.size() - 1));
}

// 简单线性回归 y = a + b*x，返回 {a, b, r2}
std::tuple<double, double, double> linearRegression(
    const std::vector<double>& x, const std::vector<double>& y)
{
    size_t n = x.size();
    if (n < 2) return {0, 0, 0};

    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
    for (size_t i = 0; i < n; ++i) {
        sx += x[i]; sy += y[i];
        sxx += x[i] * x[i];
        sxy += x[i] * y[i];
        syy += y[i] * y[i];
    }
    double denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1e-15) return {0, 0, 0};

    double b = (n * sxy - sx * sy) / denom;
    double a = (sy - b * sx) / n;

    double y_mean = sy / n;
    double ss_tot = 0, ss_res = 0;
    for (size_t i = 0; i < n; ++i) {
        ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
        ss_res += (y[i] - (a + b * x[i])) * (y[i] - (a + b * x[i]));
    }
    double r2 = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0;
    return {a, b, r2};
}

nlohmann::json forecastToJson(const ar_model::ForecastResult& fc) {
    nlohmann::json j;
    j["source_series"] = fc.source_series;
    j["order_p"] = fc.order_p;
    j["ar_coeffs"] = fc.ar_coeffs;
    j["residual_var"] = fc.residual_var;
    j["forecast_values"] = fc.forecast_values;
    j["forecast_upper_1sigma"] = fc.forecast_upper_1sigma;
    j["forecast_lower_1sigma"] = fc.forecast_lower_1sigma;
    j["forecast_upper_2sigma"] = fc.forecast_upper_2sigma;
    j["forecast_lower_2sigma"] = fc.forecast_lower_2sigma;
    j["forecast_std"] = fc.forecast_std;
    j["has_autocorrelation"] = fc.has_autocorrelation;
    j["note"] = fc.note;
    return j;
}

}  // namespace

// === 请求解析 ===

bool VolatilityHandler::parseRequest(const httplib::Request& req,
                                     VolatilityRequest& out,
                                     std::string& error) const {
    out._db_path = _server->GetConfig().GetDatabasePath();

    const auto action_param = req.get_param_value("action");
    out._action = parseVolatilityAction(action_param, false);
    if (out._action == VolatilityAction::Unknown) {
        error = "unknown action: " + action_param +
                " (all|single|multi|spectrum|forecast|lead_lag|mp_noise)";
        return false;
    }

    const auto symbols_param = req.get_param_value("symbols");
    if (symbols_param.empty()) {
        error = "symbols parameter is required";
        return false;
    }

    std::istringstream symbols_stream(symbols_param);
    std::string symbol;
    while (std::getline(symbols_stream, symbol, ',')) {
        if (!symbol.empty()) out._symbols.push_back(symbol);
    }
    if (out._symbols.empty()) {
        error = "symbols parameter is required";
        return false;
    }

    out._start_date = req.get_param_value("start_date");
    out._end_date = req.get_param_value("end_date");

    const auto windows_param = req.get_param_value("windows");
    if (!windows_param.empty()) {
        out._windows.clear();
        std::istringstream windows_stream(windows_param);
        std::string w;
        while (std::getline(windows_stream, w, ',')) {
            if (w.empty()) continue;
            try {
                int value = std::stoi(w);
                if (value > 1) out._windows.push_back(value);
            } catch (...) {
                error = "invalid windows parameter: " + windows_param;
                return false;
            }
        }
        if (out._windows.empty()) out._windows = {20, 60, 120};
    }

    const auto band_window_param = req.get_param_value("band_window");
    if (!band_window_param.empty()) {
        try {
            out._band_window = std::clamp(std::stoi(band_window_param), 2, 120);
        } catch (...) {
            out._band_window = 20;
        }
    }

    out._price_field = normalizePriceField(req.get_param_value("field"));

    const auto fill_param = req.get_param_value("fill_method");
    out._fill = fill_param.empty() ? FillMethod::None : parseFillMethod(fill_param);

    const auto frequency_param = req.get_param_value("frequency");
    out._target_freq = frequency_param.empty()
        ? BarFreq::Day : parseBarFreq(frequency_param);

    const auto spectrum_window_param = req.get_param_value("spectrum_window");
    if (!spectrum_window_param.empty()) {
        try {
            out._spectrum_window = std::clamp(std::stoi(spectrum_window_param), 5, 500);
        } catch (...) {
            out._spectrum_window = 60;
        }
    }

    const auto max_clusters_param = req.get_param_value("max_clusters");
    if (!max_clusters_param.empty()) {
        try {
            out._max_clusters = std::clamp(std::stoi(max_clusters_param), 2, 100);
        } catch (...) {
            out._max_clusters = 10;
        }
    }

    const auto include_matrix_param = req.get_param_value("include_matrix");
    out._include_matrix = (include_matrix_param == "1" ||
                           include_matrix_param == "true" ||
                           include_matrix_param == "yes");

    return true;
}

// === 数据加载 ===

VolatilityHandler::VolatilityDataset VolatilityHandler::loadDataset(
    const VolatilityRequest& req, PerfStages& stages) const
{
    VolatilityDataset dataset;

    auto getPriceCol = [&](const Map<String, Vector<double>>& data) -> Vector<double> {
        auto it = data.find(req._price_field);
        if (it != data.end() && !it->second.empty()) return it->second;
        it = data.find("close");
        if (it != data.end()) return it->second;
        return {};
    };
    auto getVolumeCol = [&](const Map<String, Vector<double>>& data) -> Vector<double> {
        auto it = data.find("volume");
        if (it != data.end()) return it->second;
        return {};
    };

    for (const auto& symbol : req._symbols) {
        Vector<String> dates;
        Vector<double> prices_vec;
        Vector<double> volumes_vec;

        auto t_symbol = PerfClock::now();

        const auto slash = symbol.find('/');
        const bool is_macro =
            (slash != std::string::npos && symbol.size() > slash + 1 &&
             symbol.find('.', slash) == std::string::npos);

        {
            PERF_SCOPE(stages, "symbol_load");
            if (is_macro) {
                if (!FetchMacroData(symbol, req._db_path, dates, prices_vec)) {
                    WARN("[Volatility] No macro data for {}", symbol);
                    continue;
                }
                volumes_vec.assign(prices_vec.size(), 0.0);
            } else {
                symbol_t sym = to_symbol(toInternalSymbol(symbol));
                auto multi = LoadHistoryDataWithFreq(
                    sym, {"close", "open", "high", "low", "volume", "turnover"},
                    req._start_date, req._end_date, req._target_freq,
                    AdjType::HFQ, &dates, req._fill);

                auto prices = getPriceCol(multi);
                auto volumes = getVolumeCol(multi);
                if (prices.empty()) {
                    WARN("[Volatility] No data for {}", symbol);
                    continue;
                }
                prices_vec.assign(prices.begin(), prices.end());
                volumes_vec.assign(volumes.begin(), volumes.end());
                if (volumes_vec.empty()) {
                    volumes_vec.assign(prices_vec.size(), 0.0);
                }
            }
        }

        VolatilitySeries series;
        for (const auto& d : dates) series._dates.emplace_back(d);
        series._prices.assign(prices_vec.begin(), prices_vec.end());
        series._volumes.assign(volumes_vec.begin(), volumes_vec.end());
        series._returns = simpleReturns(series._prices);

        dataset._series[symbol] = std::move(series);
        dataset._symbols.push_back(symbol);

        if (dataset._common_dates.empty()) {
            dataset._common_dates = dataset._series[symbol]._dates;
            if (dataset._common_dates.size() !=
                dataset._series[symbol]._prices.size()) {
                WARN("[Volatility] Date/price length mismatch for {}: dates={}, prices={}",
                     symbol, dataset._common_dates.size(),
                     dataset._series[symbol]._prices.size());
            }
        }

        stages.addItem(symbol, ms_since(t_symbol));
    }

    stages.setValue("symbols", static_cast<double>(req._symbols.size()));
    stages.setValue("loaded", static_cast<double>(dataset._symbols.size()));
    return dataset;
}

// === 收益率矩阵 ===

VolatilityHandler::ReturnMatrix VolatilityHandler::buildReturnMatrix(
    const VolatilityDataset& dataset) const
{
    ReturnMatrix rm;
    size_t min_len = std::numeric_limits<size_t>::max();

    for (const auto& symbol : dataset._symbols) {
        auto it = dataset._series.find(symbol);
        if (it == dataset._series.end()) continue;
        if (it->second._returns.size() < 2) continue;
        min_len = std::min(min_len, it->second._returns.size());
        rm._symbols.push_back(symbol);
    }

    if (rm._symbols.size() < 2 || min_len == std::numeric_limits<size_t>::max() ||
        min_len < 2) {
        rm._symbols.clear();
        return rm;
    }

    rm._n = rm._symbols.size();
    rm._T = min_len;
    rm._values.resize(static_cast<Eigen::Index>(rm._n),
                      static_cast<Eigen::Index>(rm._T));

    for (size_t i = 0; i < rm._n; ++i) {
        const auto& rets = dataset._series.at(rm._symbols[i])._returns;
        const size_t offset = rets.size() - rm._T;
        for (size_t t = 0; t < rm._T; ++t) {
            rm._values(static_cast<Eigen::Index>(i),
                       static_cast<Eigen::Index>(t)) = rets[offset + t];
        }
    }
    return rm;
}

// === 收益率 / 单标的指标 ===

std::vector<double> VolatilityHandler::simpleReturns(
    const std::vector<double>& prices) const
{
    std::vector<double> rets;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i - 1] > 0) {
            rets.push_back((prices[i] - prices[i - 1]) / prices[i - 1]);
        } else {
            rets.push_back(0.0);
        }
    }
    return rets;
}

double VolatilityHandler::skewness(const std::vector<double>& data) const {
    if (data.size() < 3) return 0.0;
    double m = meanOf(data);
    double s = stddevOf(data, m);
    if (s < 1e-10) return 0.0;
    double n = static_cast<double>(data.size());
    double sum3 = 0;
    for (auto v : data) {
        double z = (v - m) / s;
        sum3 += z * z * z;
    }
    return (n / ((n - 1) * (n - 2))) * sum3;
}

double VolatilityHandler::kurtosis(const std::vector<double>& data) const {
    if (data.size() < 4) return 0.0;
    double m = meanOf(data);
    double s = stddevOf(data, m);
    if (s < 1e-10) return 0.0;
    double n = static_cast<double>(data.size());
    double sum4 = 0;
    for (auto v : data) {
        double z = (v - m) / s;
        sum4 += z * z * z * z;
    }
    double k = ((n * (n + 1)) / ((n - 1) * (n - 2) * (n - 3))) * sum4;
    double correction = (3.0 * (n - 1) * (n - 1)) / ((n - 2) * (n - 3));
    return k - correction;
}

ACFDecayAnalysis VolatilityHandler::analyzeACFDecay(
    const std::vector<double>& abs_acf,
    const std::vector<double>& abs_returns) const
{
    ACFDecayAnalysis result;

    if (abs_acf.size() < 3 || abs_returns.size() < 4) {
        result._decay_mode = ACFDecayMode::Inconclusive;
        return result;
    }

    const int lags = static_cast<int>(abs_acf.size()) - 1;
    auto [Q, pval] = boost::math::statistics::ljung_box(abs_returns, lags, 0);
    result._lb_statistic = Q;
    result._lb_pvalue = pval;
    result._has_autocorrelation = (pval < 0.05);

    std::vector<double> lag_vals;
    std::vector<double> acf_vals;
    for (size_t k = 1; k < abs_acf.size(); ++k) {
        double av = std::abs(abs_acf[k]);
        if (av < 1e-10) continue;
        lag_vals.push_back(static_cast<double>(k));
        acf_vals.push_back(av);
    }

    if (lag_vals.size() < 3) {
        result._decay_mode = ACFDecayMode::Inconclusive;
        return result;
    }

    std::vector<double> log_acf;
    for (auto v : acf_vals) log_acf.push_back(std::log(v));
    auto [exp_a, exp_b, exp_r2] = linearRegression(lag_vals, log_acf);
    result._exponential_r2 = std::max(0.0, exp_r2);

    std::vector<double> log_lag;
    for (auto v : lag_vals) log_lag.push_back(std::log(v));
    auto [hyp_a, hyp_b, hyp_r2] = linearRegression(log_lag, log_acf);
    result._hyperbolic_r2 = std::max(0.0, hyp_r2);

    double delta_r2 = std::abs(result._exponential_r2 - result._hyperbolic_r2);
    double max_r2 = std::max(result._exponential_r2, result._hyperbolic_r2);

    if (delta_r2 > 0.1 && max_r2 > 0.3) {
        result._decay_mode = (result._exponential_r2 > result._hyperbolic_r2)
            ? ACFDecayMode::Exponential : ACFDecayMode::Hyperbolic;
    } else {
        result._decay_mode = ACFDecayMode::Inconclusive;
    }

    if (result._decay_mode == ACFDecayMode::Exponential && exp_b > 1e-10) {
        result._decay_half_life = std::log(2.0) / exp_b;
    }
    if (hyp_b > 0) {
        result._hurst_estimate = 1.0 - hyp_b / 2.0;
    }
    return result;
}

VolatilitySingleResult VolatilityHandler::computeSingle(
    const std::vector<double>& prices,
    const std::vector<double>& volumes,
    const std::vector<int>& windows,
    int band_window) const
{
    VolatilitySingleResult result;
    result._prices = prices;
    result._volumes = volumes;
    result._returns = simpleReturns(prices);

    if (result._returns.empty()) return result;

    for (int w : windows) {
        result._rolling_vol[w] = rolling_volatility(result._returns, w);
    }

    const size_t offset = (band_window > 1) ? (band_window - 1) : 0;
    for (size_t i = 0; i < prices.size(); ++i) {
        if (i < offset) {
            result._mean_price.push_back(prices[i]);
            result._upper_2sigma.push_back(prices[i]);
            result._lower_2sigma.push_back(prices[i]);
            continue;
        }

        double sum = 0;
        for (int j = 0; j < band_window; ++j) sum += prices[i - j];
        double m = sum / band_window;
        double var = 0;
        for (int j = 0; j < band_window; ++j) {
            double d = prices[i - j] - m;
            var += d * d;
        }
        double s = std::sqrt(var / (band_window - 1));

        result._mean_price.push_back(m);
        result._upper_2sigma.push_back(m + 2 * s);
        result._lower_2sigma.push_back(m - 2 * s);
    }

    result._annual_volatility = compute_annualized_volatility(result._returns);
    result._max_drawdown = max_drawdown_ratio(prices);
    result._skewness = skewness(result._returns);
    result._kurtosis = kurtosis(result._returns);
    result._var_95 = compute_var(result._returns, 0.95);
    result._cvar_95 = compute_cvar(result._returns, 0.95);

    result._abs_returns.reserve(result._returns.size());
    for (auto r : result._returns) {
        result._abs_returns.push_back(std::abs(r));
    }

    const int max_lag = std::min(20, static_cast<int>(result._returns.size() / 4));
    result._returns_acf = finance::computeACF(result._returns, max_lag);
    result._returns_pacf = finance::computePACF(result._returns_acf, max_lag);
    result._abs_returns_acf = finance::computeACF(result._abs_returns, max_lag);
    result._acf_decay = analyzeACFDecay(result._abs_returns_acf, result._abs_returns);

    const double last_price = result._prices.empty() ? 0.0 : result._prices.back();
    result._forecast_returns = ar_model::buildForecast(
        result._returns, result._returns_acf, ar_model::MAX_FORECAST_STEPS,
        "returns", last_price);

    if (result._rolling_vol.count(20)) {
        const auto& rv = result._rolling_vol.at(20);
        if (rv.size() >= 30) {
            auto vol_acf = finance::computeACF(rv, ar_model::MAX_FORECAST_STEPS);
            const double last_vol = rv.empty() ? 0.0 : rv.back();
            result._forecast_vol = ar_model::buildForecast(
                rv, vol_acf, ar_model::MAX_FORECAST_STEPS,
                "rolling_vol", last_vol);
        }
    }

    return result;
}

// === 多标的 section ===

void VolatilityHandler::computeMoments(const ReturnMatrix& rm,
                                       VolatilityMultiResult& result,
                                       PerfStages* perf_stages) const
{
    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;
    if (!rm.valid()) return;

    const int n = static_cast<int>(rm._n);
    const int T = static_cast<int>(rm._T);
    stages.setValue("multi_n", static_cast<double>(n));
    stages.setValue("multi_T", static_cast<double>(T));

    {
        PERF_SCOPE(stages, "multi_matrix");
        const Eigen::VectorXd means = rm._values.rowwise().mean();
        const Eigen::MatrixXd centered = rm._values.colwise() - means;
        Eigen::MatrixXd cov =
            (centered * centered.transpose()) / static_cast<double>(T - 1);

        Eigen::VectorXd std_devs = cov.diagonal().array().sqrt().matrix();
        for (int i = 0; i < n; ++i) {
            if (std_devs(i) < 1e-10) std_devs(i) = 1.0;
        }

        Eigen::MatrixXd corr = cov;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                corr(i, j) = cov(i, j) / (std_devs(i) * std_devs(j));
            }
        }
        corr.diagonal().setOnes();

        result._covariance_matrix = std::move(cov);
        result._correlation_matrix = std::move(corr);
    }

    result._annual_volatility.resize(n);
    for (int i = 0; i < n; ++i) {
        result._annual_volatility(i) =
            std::sqrt(result._covariance_matrix(i, i) * 252.0);
    }
    result._num_observations = static_cast<size_t>(T);

    {
        PERF_SCOPE(stages, "multi_eigen");
        Eigen::VectorXd eigenvalues;
        evaluateCovarianceQuality(result._covariance_matrix, eigenvalues,
                                  result._condition_number,
                                  result._is_positive_definite);
        result._eigenvalues = std::move(eigenvalues);
    }
}

void VolatilityHandler::computeSpectrumSection(
    const ReturnMatrix& rm,
    const std::vector<std::string>& dates,
    int spectrum_window,
    VolatilityMultiResult& result,
    PerfStages* perf_stages) const
{
    if (!rm.valid() || dates.empty() || rm._T > dates.size()) return;

    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;
    result._num_observations = rm._T;

    PERF_SCOPE(stages, "multi_spectrum");
    Vector<String> dates_vec;
    dates_vec.reserve(rm._T);
    const size_t offset = dates.size() - rm._T;
    for (size_t i = offset; i < dates.size(); ++i) {
        dates_vec.emplace_back(dates[i]);
    }

    result._spectrum_indicators = finance::computeSpectrumIndicators(
        rm._values, dates_vec, spectrum_window, 10, &stages);
}

void VolatilityHandler::computeForecastSection(
    const ReturnMatrix& rm,
    VolatilityMultiResult& result,
    PerfStages* perf_stages) const
{
    if (!rm.valid()) return;
    if (result._covariance_matrix.rows() != static_cast<Eigen::Index>(rm._n) ||
        result._covariance_matrix.cols() != static_cast<Eigen::Index>(rm._n)) {
        return;
    }

    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;
    PERF_SCOPE(stages, "multi_forecast");

    std::vector<ar_model::Forecast> all_forecasts;
    std::vector<std::string> forecast_symbols;
    all_forecasts.reserve(rm._n);
    forecast_symbols.reserve(rm._n);

    for (size_t i = 0; i < rm._n; ++i) {
        Vector<double> rets(rm._T);
        for (size_t t = 0; t < rm._T; ++t) {
            rets[t] = rm._values(static_cast<Eigen::Index>(i),
                                 static_cast<Eigen::Index>(t));
        }

        bool fitted = false;
        if (rets.size() >= 30) {
            auto acf = finance::computeACF(rets, ar_model::MAX_FORECAST_STEPS);
            const int p = ar_model::findSignificantLag(
                acf, static_cast<int>(rets.size()), ar_model::MAX_FORECAST_STEPS);
            if (p > 0) {
                auto fit = ar_model::solveYuleWalker(acf, p);
                if (fit.stable && !fit.coeffs.empty()) {
                    std::vector<double> history(rets.end() - p, rets.end());
                    all_forecasts.push_back(ar_model::predict(
                        fit.coeffs, history, p, fit.residual_var));
                    fitted = true;
                }
            }
        }

        if (!fitted) {
            const double var = result._covariance_matrix(
                static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(i));
            const double vol = var > 0 ? std::sqrt(var) : 0.0;
            all_forecasts.push_back(ar_model::Forecast{
                .values = std::vector<double>(1, 0.0),
                .std_per_step = std::vector<double>(1, vol)
            });
        }
        forecast_symbols.push_back(rm._symbols[i]);
    }

    if (forecast_symbols.size() < 2) return;

    const int horizon = std::max(
        1, static_cast<int>(all_forecasts.front().values.size()));

    auto& mf = result._multi_forecast;
    mf._horizon = horizon;
    mf._symbols = forecast_symbols;
    mf._forecast_cov = ar_model::extrapolateCovariance(
        result._covariance_matrix, all_forecasts, horizon);

    const size_t N = forecast_symbols.size();
    mf._forecast_corr.resize(static_cast<Eigen::Index>(N),
                             static_cast<Eigen::Index>(N));
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) {
            const double vi = mf._forecast_cov(static_cast<Eigen::Index>(i),
                                               static_cast<Eigen::Index>(i));
            const double vj = mf._forecast_cov(static_cast<Eigen::Index>(j),
                                               static_cast<Eigen::Index>(j));
            if (vi > 1e-15 && vj > 1e-15) {
                mf._forecast_corr(static_cast<Eigen::Index>(i),
                                  static_cast<Eigen::Index>(j)) =
                    mf._forecast_cov(static_cast<Eigen::Index>(i),
                                     static_cast<Eigen::Index>(j)) /
                    std::sqrt(vi * vj);
            } else {
                mf._forecast_corr(static_cast<Eigen::Index>(i),
                                  static_cast<Eigen::Index>(j)) =
                    (i == j) ? 1.0 : 0.0;
            }
        }
    }

    mf._forecast_volatilities.resize(static_cast<Eigen::Index>(N));
    for (size_t i = 0; i < N; ++i) {
        mf._forecast_volatilities(static_cast<Eigen::Index>(i)) =
            std::sqrt(mf._forecast_cov(static_cast<Eigen::Index>(i),
                                       static_cast<Eigen::Index>(i)) * 252.0);
    }
}

void VolatilityHandler::computeLeadLagSection(
    const ReturnMatrix& rm,
    int max_lag,
    VolatilityMultiResult& result,
    PerfStages* perf_stages) const
{
    if (!rm.valid()) return;

    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;
    result._num_observations = rm._T;

    int ts_max_lag = std::min(max_lag, static_cast<int>(rm._T / 4));
    if (ts_max_lag < 1) ts_max_lag = 1;

    PerfCounter& c_pair_prep = stages.stage("pair_prep");
    PerfCounter& c_lead_lag = stages.stage("lead_lag");
    PerfCounter& c_granger = stages.stage("granger");
    const bool perf_on = stages.enabled();

    for (size_t i = 0; i < rm._n; ++i) {
        std::vector<double> rets_i(rm._T);
        for (size_t t = 0; t < rm._T; ++t) {
            rets_i[t] = rm._values(static_cast<Eigen::Index>(i),
                                   static_cast<Eigen::Index>(t));
        }

        for (size_t j = i + 1; j < rm._n; ++j) {
            const auto t_prep = PerfClock::now();
            std::vector<double> rets_j(rm._T);
            for (size_t t = 0; t < rm._T; ++t) {
                rets_j[t] = rm._values(static_cast<Eigen::Index>(j),
                                       static_cast<Eigen::Index>(t));
            }
            if (perf_on) c_pair_prep.add(ms_since(t_prep));

            const auto t_ccf = PerfClock::now();
            auto ccf = finance::crossCorrelation(rets_i, rets_j, ts_max_lag);
            if (perf_on) c_lead_lag.add(ms_since(t_ccf));

            result._lead_lag_results.push_back({
                rm._symbols[i], rm._symbols[j],
                ccf.lead_lag, ccf.max_correlation, ccf.ccf
            });

            const auto t_gr = PerfClock::now();
            const int granger_max_lag = std::min(ts_max_lag, 5);
            auto gr_yx = finance::grangerCausalityTest(
                rets_i, rets_j, granger_max_lag, rm._symbols[i], rm._symbols[j]);
            result._granger_results.push_back({
                rm._symbols[j], rm._symbols[i],
                gr_yx.f_statistic, gr_yx.p_value,
                gr_yx.is_significant, gr_yx.optimal_lag
            });

            auto gr_xy = finance::grangerCausalityTest(
                rets_j, rets_i, granger_max_lag, rm._symbols[j], rm._symbols[i]);
            if (perf_on) c_granger.add(ms_since(t_gr));
            result._granger_results.push_back({
                rm._symbols[i], rm._symbols[j],
                gr_xy.f_statistic, gr_xy.p_value,
                gr_xy.is_significant, gr_xy.optimal_lag
            });
        }
    }

    stages.setValue("pairs",
        static_cast<double>(rm._n * (rm._n - 1) / 2));
}

void VolatilityHandler::computeMpNoiseSection(
    const ReturnMatrix& rm,
    int max_clusters,
    VolatilityMultiResult& result,
    PerfStages* perf_stages) const
{
    if (!rm.valid()) return;

    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;
    result._num_observations = rm._T;

    PERF_SCOPE(stages, "multi_mp_noise");
    result._mp_noise = finance::computeMpNoise(rm._values, max_clusters);
    result._mp_symbols.clear();
    if (result._mp_noise._valid) {
        result._mp_symbols = rm._symbols;
    }
}

// === 主计算 ===

VolatilityResult VolatilityHandler::compute(
    const VolatilityRequest& req,
    const VolatilitySections& sections,
    PerfStages* perf_stages) const
{
    PerfStages local_stages;
    PerfStages& stages = perf_stages ? *perf_stages : local_stages;

    VolatilityResult result;
    auto dataset = loadDataset(req, stages);
    result._symbols = dataset._symbols;
    result._dates = dataset._common_dates;

    if (sections._single) {
        for (const auto& symbol : dataset._symbols) {
            const auto& series = dataset._series.at(symbol);
            PERF_SCOPE(stages, "symbol_compute");
            result._single[symbol] = computeSingle(
                series._prices, series._volumes, req._windows, req._band_window);
        }
    }

    const bool need_matrix =
        sections._multi || sections._spectrum || sections._forecast ||
        sections._lead_lag || sections._mp_noise;
    if (!need_matrix) return result;

    const auto rm = buildReturnMatrix(dataset);

    if (sections._multi || sections._forecast) {
        computeMoments(rm, result._multi, &stages);
    }
    if (sections._spectrum) {
        computeSpectrumSection(rm, dataset._common_dates,
                               req._spectrum_window, result._multi, &stages);
    }
    if (sections._forecast) {
        computeForecastSection(rm, result._multi, &stages);
    }
    if (sections._lead_lag) {
        computeLeadLagSection(rm, 10, result._multi, &stages);
    }
    if (sections._mp_noise) {
        computeMpNoiseSection(rm, req._max_clusters, result._multi, &stages);
    }

    return result;
}

// === JSON 序列化 ===

nlohmann::json VolatilityHandler::matrixToJson(const Eigen::MatrixXd& matrix) const {
    nlohmann::json rows = nlohmann::json::array();
    for (Eigen::Index i = 0; i < matrix.rows(); ++i) {
        nlohmann::json row = nlohmann::json::array();
        for (Eigen::Index j = 0; j < matrix.cols(); ++j) {
            row.push_back(matrix(i, j));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

nlohmann::json VolatilityHandler::vectorToJson(const Eigen::VectorXd& vector) const {
    nlohmann::json arr = nlohmann::json::array();
    for (Eigen::Index i = 0; i < vector.size(); ++i) {
        arr.push_back(vector(i));
    }
    return arr;
}

nlohmann::json VolatilityHandler::singleToJson(
    const VolatilitySingleResult& sr) const
{
    nlohmann::json item;
    item["prices"] = sr._prices;
    item["returns"] = sr._returns;
    item["volumes"] = sr._volumes;
    item["abs_returns"] = sr._abs_returns;

    nlohmann::json rolling;
    for (const auto& [w, vals] : sr._rolling_vol) {
        rolling[std::to_string(w)] = vals;
    }
    item["rolling_vol"] = rolling;

    item["upper_2sigma"] = sr._upper_2sigma;
    item["mean_price"] = sr._mean_price;
    item["lower_2sigma"] = sr._lower_2sigma;

    item["annual_volatility"] = sr._annual_volatility;
    item["max_drawdown"] = sr._max_drawdown;
    item["skewness"] = sr._skewness;
    item["kurtosis"] = sr._kurtosis;
    item["var_95"] = sr._var_95;
    item["cvar_95"] = sr._cvar_95;

    item["returns_acf"] = sr._returns_acf;
    item["returns_pacf"] = sr._returns_pacf;
    item["abs_returns_acf"] = sr._abs_returns_acf;

    nlohmann::json decay;
    decay["lb_statistic"] = sr._acf_decay._lb_statistic;
    decay["lb_pvalue"] = sr._acf_decay._lb_pvalue;
    decay["has_autocorrelation"] = sr._acf_decay._has_autocorrelation;
    decay["exponential_r2"] = sr._acf_decay._exponential_r2;
    decay["hyperbolic_r2"] = sr._acf_decay._hyperbolic_r2;
    decay["decay_mode"] = decayModeToString(sr._acf_decay._decay_mode);
    decay["decay_half_life"] = sr._acf_decay._decay_half_life;
    decay["hurst_estimate"] = sr._acf_decay._hurst_estimate;
    item["acf_decay"] = decay;

    item["forecast_returns"] = forecastToJson(sr._forecast_returns);
    item["forecast_vol"] = forecastToJson(sr._forecast_vol);
    return item;
}

nlohmann::json VolatilityHandler::multiMomentsToJson(
    const VolatilityMultiResult& multi) const
{
    nlohmann::json j;
    j["correlation_matrix"] = matrixToJson(multi._correlation_matrix);
    j["covariance_matrix"] = matrixToJson(multi._covariance_matrix);
    j["eigenvalues"] = vectorToJson(multi._eigenvalues);
    j["condition_number"] = multi._condition_number;
    j["is_positive_definite"] = multi._is_positive_definite;
    j["annual_volatility"] = vectorToJson(multi._annual_volatility);
    j["num_observations"] = multi._num_observations;
    return j;
}

nlohmann::json VolatilityHandler::spectrumToJson(
    const finance::SpectrumIndicatorResult& spectrum) const
{
    nlohmann::json j;
    j["dates"] = nlohmann::json::array();
    for (const auto& d : spectrum.dates) {
        j["dates"].push_back(std::string(d.begin(), d.end()));
    }
    j["m_plus"] = spectrum.m_plus;
    j["m_minus"] = spectrum.m_minus;
    j["n_effective"] = spectrum.n_effective;
    j["signal_var_ratio"] = spectrum.signal_var_ratio;
    j["lambda_max"] = spectrum.lambda_max;
    j["lambda_max_ratio"] = spectrum.lambda_max_ratio;
    j["lambda_plus"] = spectrum.lambda_plus;
    j["lambda_minus"] = spectrum.lambda_minus;
    j["original_n"] = spectrum.original_n;
    j["window_size"] = spectrum.window_size;
    return j;
}

nlohmann::json VolatilityHandler::multiForecastToJson(
    const VolatilityMultiResult::MultiForecast& forecast) const
{
    nlohmann::json j;
    j["horizon"] = forecast._horizon;
    j["symbols"] = forecast._symbols;
    j["forecast_cov"] = matrixToJson(forecast._forecast_cov);
    j["forecast_corr"] = matrixToJson(forecast._forecast_corr);
    j["forecast_volatilities"] = vectorToJson(forecast._forecast_volatilities);
    return j;
}

nlohmann::json VolatilityHandler::timeSeriesToJson(
    const VolatilityMultiResult& multi) const
{
    nlohmann::json ts;

    nlohmann::json lead_lag = nlohmann::json::array();
    for (const auto& ll : multi._lead_lag_results) {
        nlohmann::json item;
        item["symbol_x"] = ll._symbol_x;
        item["symbol_y"] = ll._symbol_y;
        item["lead_lag"] = ll._lead_lag;
        item["max_correlation"] = ll._max_correlation;
        item["ccf"] = ll._ccf;
        lead_lag.push_back(std::move(item));
    }
    ts["lead_lag"] = std::move(lead_lag);

    nlohmann::json granger = nlohmann::json::array();
    for (const auto& gr : multi._granger_results) {
        nlohmann::json item;
        item["from"] = gr._from;
        item["to"] = gr._to;
        item["f_statistic"] = gr._f_statistic;
        item["p_value"] = gr._p_value;
        item["is_significant"] = gr._is_significant;
        item["optimal_lag"] = gr._optimal_lag;
        granger.push_back(std::move(item));
    }
    ts["granger_causality"] = std::move(granger);
    return ts;
}

nlohmann::json VolatilityHandler::mpNoiseToJson(
    const finance::MpNoiseResult& noise,
    const std::vector<std::string>& symbols,
    bool include_matrix) const
{
    nlohmann::json j;
    j["valid"] = noise._valid;
    j["original_n"] = noise._original_n;
    j["n_effective"] = noise._n_effective;
    j["n_observations"] = noise._n_observations;
    j["q"] = noise._q;
    j["lambda_plus"] = noise._lambda_plus;
    j["lambda_minus"] = noise._lambda_minus;
    j["eigenvalues"] = vectorToJson(noise._eigenvalues);
    j["lambda_max"] = noise._lambda_max;
    j["lambda_max_ratio"] = noise._lambda_max_ratio;
    j["n_signal"] = noise._n_signal;
    j["n_bulk"] = noise._n_bulk;
    j["n_below"] = noise._n_below;
    j["signal_var_ratio"] = noise._signal_var_ratio;
    j["noise_var_ratio"] = noise._noise_var_ratio;
    j["below_edge_var_ratio"] = noise._below_edge_var_ratio;
    j["symbols"] = symbols;
    j["signal_share"] = vectorToJson(noise._signal_share);
    j["noise_share"] = vectorToJson(noise._noise_share);
    j["below_edge_share"] = vectorToJson(noise._below_edge_share);
    j["cluster_labels"] = noise._cluster_labels;
    j["condition_number_original"] = noise._condition_number_original;
    j["condition_number_denoised"] = noise._condition_number_denoised;
    j["denoise_frobenius"] = noise._denoise_frobenius;

    if (include_matrix && noise._valid) {
        j["correlation"] = matrixToJson(noise._correlation);
        j["denoised_correlation"] = matrixToJson(noise._denoised_correlation);
    }
    return j;
}

// === 协方差质量诊断 ===

void VolatilityHandler::evaluateCovarianceQuality(
    const Eigen::MatrixXd& cov,
    Eigen::VectorXd& eigenvalues_out,
    double& condition_number_out,
    bool& is_positive_definite_out) const
{
    const Eigen::Index n = cov.rows();
    eigenvalues_out.resize(0);

    if (n == 0 || cov.cols() != n) {
        condition_number_out = 0;
        is_positive_definite_out = false;
        return;
    }

    if (n == 1) {
        eigenvalues_out.resize(1);
        eigenvalues_out(0) = cov(0, 0);
        is_positive_definite_out = (cov(0, 0) > 1e-12);
        condition_number_out = is_positive_definite_out ? 1.0 : 1e15;
        return;
    }

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(cov);
    if (es.info() == Eigen::Success) {
        const Eigen::VectorXd evals = es.eigenvalues();  // 升序
        const double min_eigen = evals(0);
        const double max_eigen = evals(n - 1);
        eigenvalues_out.resize(n);
        for (Eigen::Index i = 0; i < n; ++i) {
            eigenvalues_out(i) = evals(n - 1 - i);  // 降序
        }
        condition_number_out = (min_eigen > 1e-12)
            ? (max_eigen / min_eigen) : 1e15;
        is_positive_definite_out = (min_eigen > 1e-12);
        return;
    }

    // 回退: Gershgorin 圆估计
    double max_eigen = 0;
    double min_eigen = std::numeric_limits<double>::max();
    eigenvalues_out.resize(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        const double center = cov(i, i);
        double radius = 0;
        for (Eigen::Index j = 0; j < n; ++j) {
            if (i != j) radius += std::abs(cov(i, j));
        }
        max_eigen = std::max(max_eigen, center + radius);
        min_eigen = std::min(min_eigen, std::max(0.0, center - radius));
        eigenvalues_out(i) = center;
    }
    std::sort(eigenvalues_out.data(),
              eigenvalues_out.data() + eigenvalues_out.size(),
              [](double a, double b) { return a > b; });
    condition_number_out = (min_eigen > 1e-15)
        ? (max_eigen / min_eigen) : 1e15;
    is_positive_definite_out = (min_eigen > 1e-15);
    WARN("[Volatility] SelfAdjointEigenSolver failed (n={}), fell back to Gershgorin estimate", n);
}

// === POST 处理 ===

void VolatilityHandler::handleCovarianceDiagnosticsPost(
    const nlohmann::json& params,
    httplib::Response& res) const
{
    if (!params.contains("covariance_matrix") ||
        !params["covariance_matrix"].is_array()) {
        throw std::runtime_error("missing 'covariance_matrix' (NxN array)");
    }

    const auto& mat = params["covariance_matrix"];
    const size_t n = mat.size();
    if (n == 0 || !mat[0].is_array() || mat[0].size() != n) {
        throw std::runtime_error(
            "covariance_matrix must be a non-empty square NxN array");
    }

    Eigen::MatrixXd cov(static_cast<Eigen::Index>(n),
                        static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        if (!mat[i].is_array() || mat[i].size() != n) {
            throw std::runtime_error("covariance_matrix is not square");
        }
        for (size_t j = 0; j < n; ++j) {
            if (!mat[i][j].is_number()) {
                throw std::runtime_error(
                    "non-numeric element at [" + std::to_string(i) + "][" +
                    std::to_string(j) + "]");
            }
            cov(static_cast<Eigen::Index>(i),
                static_cast<Eigen::Index>(j)) = mat[i][j].get<double>();
        }
    }

    Eigen::VectorXd eigenvalues;
    double condition_number = 0;
    bool is_positive_definite = false;
    evaluateCovarianceQuality(cov, eigenvalues,
                              condition_number, is_positive_definite);

    nlohmann::json resp;
    resp["n"] = n;
    resp["eigenvalues"] = vectorToJson(eigenvalues);
    resp["condition_number"] = condition_number;
    resp["is_positive_definite"] = is_positive_definite;

    res.set_content(resp.dump(), "application/json");
    res.status = 200;
}

void VolatilityHandler::handleMpNoisePost(
    const nlohmann::json& params,
    httplib::Response& res) const
{
    const int max_clusters = std::clamp(
        params.value("max_clusters", 10), 2, 100);
    const bool include_matrix = params.value("include_matrix", false);

    std::vector<std::string> symbols;
    auto fillSymbols = [&](size_t n) {
        if (params.contains("symbols")) {
            const auto& s = params["symbols"];
            if (!s.is_array() || s.size() != n) {
                throw std::runtime_error(
                    "'symbols' must be an array with the same length as assets");
            }
            for (const auto& x : s) {
                if (!x.is_string()) {
                    throw std::runtime_error("'symbols' must contain strings");
                }
                symbols.push_back(x.get<std::string>());
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                symbols.push_back("asset_" + std::to_string(i));
            }
        }
    };

    finance::MpNoiseResult noise;

    if (params.contains("returns") || params.contains("returns_matrix")) {
        const auto& mat = params.contains("returns")
            ? params["returns"] : params["returns_matrix"];
        if (!mat.is_array() || mat.size() < 2 || !mat[0].is_array()) {
            throw std::runtime_error(
                "'returns' must be a non-empty NxT array (N >= 2)");
        }

        const size_t n = mat.size();
        const size_t T = mat[0].size();
        if (T < 3) {
            throw std::runtime_error("'returns' must have at least 3 observations");
        }

        Eigen::MatrixXd ret(static_cast<Eigen::Index>(n),
                            static_cast<Eigen::Index>(T));
        for (size_t i = 0; i < n; ++i) {
            if (!mat[i].is_array() || mat[i].size() != T) {
                throw std::runtime_error(
                    "'returns' rows must have the same length");
            }
            for (size_t t = 0; t < T; ++t) {
                if (!mat[i][t].is_number()) {
                    throw std::runtime_error(
                        "non-numeric returns at [" + std::to_string(i) + "][" +
                        std::to_string(t) + "]");
                }
                ret(static_cast<Eigen::Index>(i),
                    static_cast<Eigen::Index>(t)) = mat[i][t].get<double>();
            }
        }

        fillSymbols(n);
        noise = finance::computeMpNoise(ret, max_clusters);
    } else if (params.contains("correlation_matrix") ||
               params.contains("covariance_matrix")) {
        const bool from_cov = params.contains("covariance_matrix");
        const auto& mat = from_cov
            ? params["covariance_matrix"] : params["correlation_matrix"];
        if (!mat.is_array() || mat.size() < 2 || !mat[0].is_array()) {
            throw std::runtime_error(
                "matrix must be a non-empty square NxN array (N >= 2)");
        }

        const size_t n = mat.size();
        if (!mat[0].is_array() || mat[0].size() != n) {
            throw std::runtime_error("matrix must be square NxN");
        }
        const int n_obs = params.value("n_observations", 0);
        if (n_obs < 3) {
            throw std::runtime_error(
                "'n_observations' (>= 3) is required for matrix input");
        }
        if (static_cast<int>(n) >= n_obs) {
            throw std::runtime_error(
                "matrix input requires n < n_observations; pass 'returns' "
                "to enable clustering-based dimension reduction");
        }

        Eigen::MatrixXd corr(static_cast<Eigen::Index>(n),
                             static_cast<Eigen::Index>(n));
        for (size_t i = 0; i < n; ++i) {
            if (!mat[i].is_array() || mat[i].size() != n) {
                throw std::runtime_error("matrix is not square");
            }
            for (size_t j = 0; j < n; ++j) {
                if (!mat[i][j].is_number()) {
                    throw std::runtime_error(
                        "non-numeric element at [" + std::to_string(i) + "][" +
                        std::to_string(j) + "]");
                }
                corr(static_cast<Eigen::Index>(i),
                     static_cast<Eigen::Index>(j)) = mat[i][j].get<double>();
            }
        }

        if (from_cov) {
            Eigen::VectorXd diag(static_cast<Eigen::Index>(n));
            for (size_t i = 0; i < n; ++i) {
                diag(static_cast<Eigen::Index>(i)) =
                    corr(static_cast<Eigen::Index>(i),
                         static_cast<Eigen::Index>(i));
                if (!(diag(static_cast<Eigen::Index>(i)) > 1e-15)) {
                    throw std::runtime_error(
                        "covariance_matrix diagonal must be positive");
                }
            }
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    corr(static_cast<Eigen::Index>(i),
                         static_cast<Eigen::Index>(j)) /=
                        std::sqrt(diag(static_cast<Eigen::Index>(i)) *
                                  diag(static_cast<Eigen::Index>(j)));
                }
            }
        }
        corr.diagonal().setOnes();

        fillSymbols(n);
        noise = finance::computeMpNoiseFromCorrelation(corr, n_obs);
    } else {
        throw std::runtime_error(
            "missing 'returns'/'returns_matrix' or "
            "'correlation_matrix'/'covariance_matrix'");
    }

    auto resp = mpNoiseToJson(noise, symbols, include_matrix);
    resp["action"] = "mp_noise";
    res.set_content(resp.dump(), "application/json");
    res.status = 200;
}

// === HTTP GET ===

void VolatilityHandler::get(const httplib::Request& req, httplib::Response& res) {
    PerfStages stages;

    try {
        VolatilityRequest vr;
        std::string error;
        if (!parseRequest(req, vr, error)) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", error}}.dump(),
                            "application/json");
            return;
        }

        const auto sections = VolatilitySections::forAction(vr._action);
        auto result = compute(vr, sections, &stages);

        const auto t_json = PerfClock::now();
        nlohmann::json json;
        json["symbols"] = result._symbols;
        json["dates"] = result._dates;
        json["field"] = std::string(vr._price_field.begin(), vr._price_field.end());
        json["action"] = volatilityActionToString(vr._action);

        if (sections._single) {
            nlohmann::json single_json;
            for (const auto& [symbol, sr] : result._single) {
                single_json[symbol] = singleToJson(sr);
            }
            json["single"] = std::move(single_json);
        }

        nlohmann::json multi = nlohmann::json::object();
        if (sections._multi) {
            multi = multiMomentsToJson(result._multi);
        }
        if (sections._spectrum) {
            if (vr._action == VolatilityAction::All) {
                if (!result._multi._spectrum_indicators.dates.empty()) {
                    multi["spectrum_indicators"] =
                        spectrumToJson(result._multi._spectrum_indicators);
                }
            } else {
                multi["spectrum_indicators"] =
                    spectrumToJson(result._multi._spectrum_indicators);
            }
        }
        if (sections._forecast) {
            if (vr._action == VolatilityAction::All) {
                if (result._multi._multi_forecast._horizon > 0) {
                    multi["multi_forecast"] =
                        multiForecastToJson(result._multi._multi_forecast);
                }
            } else {
                multi["multi_forecast"] =
                    multiForecastToJson(result._multi._multi_forecast);
            }
        }
        if (sections._lead_lag) {
            if (vr._action == VolatilityAction::All) {
                if (!result._multi._lead_lag_results.empty()) {
                    multi["time_series_analysis"] =
                        timeSeriesToJson(result._multi);
                }
            } else {
                multi["time_series_analysis"] =
                    timeSeriesToJson(result._multi);
            }
        }
        if (sections._mp_noise) {
            multi["mp_noise"] = mpNoiseToJson(
                result._multi._mp_noise, result._multi._mp_symbols,
                vr._include_matrix);
        }

        if (!sections._multi &&
            (sections._spectrum || sections._forecast ||
             sections._lead_lag || sections._mp_noise)) {
            multi["num_observations"] = result._multi._num_observations;
        }

        if (sections._multi || sections._spectrum || sections._forecast ||
            sections._lead_lag || sections._mp_noise) {
            json["multi"] = std::move(multi);
        }

        stages.stage("json_build").add(ms_since(t_json));
        const auto t_dump = PerfClock::now();
        auto body = json.dump();
        stages.stage("json_dump").add(ms_since(t_dump));

        res.set_content(body, "application/json");
        res.status = 200;
    } catch (const std::exception& e) {
        FATAL("[Volatility] Error: {}", e.what());
        res.status = 500;
        res.set_content(nlohmann::json{{"error", e.what()}}.dump(),
                        "application/json");
    }

    INFO("[Volatility][perf] action={} {}", volatilityActionToString(
             parseVolatilityAction(req.get_param_value("action"))),
         stages.summary());
    auto slowest = stages.itemsSummary(5);
    if (!slowest.empty()) {
        INFO("[Volatility][perf] slowest symbols: {}", slowest);
    }
}

// === HTTP POST ===

void VolatilityHandler::post(const httplib::Request& req, httplib::Response& res) {
    try {
        const auto params = nlohmann::json::parse(req.body);

        std::string action = params.value("action", "covariance_diagnostics");
        if (action.empty()) action = "covariance_diagnostics";

        if (action == "mp_noise" || action == "mp" || action == "noise") {
            handleMpNoisePost(params, res);
        } else if (action == "covariance_diagnostics" ||
                   action == "diagnostics") {
            handleCovarianceDiagnosticsPost(params, res);
        } else {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"error",
                    "unknown action: " + action +
                    " (covariance_diagnostics|mp_noise)"}}.dump(),
                "application/json");
        }
    } catch (const std::exception& e) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", e.what()}}.dump(),
                        "application/json");
    }
}
