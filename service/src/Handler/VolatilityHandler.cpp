#include "Handler/VolatilityHandler.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "boost/math/statistics/ljung_box.hpp"
#include "server.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

static PriceField parsePriceField(const std::string& field) {
    if (field == "open" || field == "O") return PriceField::Open;
    if (field == "high" || field == "H") return PriceField::High;
    if (field == "low" || field == "L") return PriceField::Low;
    if (field == "volume" || field == "V") return PriceField::Volume;
    return PriceField::Close;
}

// === 工具函数 ===

static double mean(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}

static double stddev(const std::vector<double>& data, double m = -1) {
    if (data.size() < 2) return 0.0;
    if (m < 0) m = mean(data);
    double sq_sum = 0;
    for (auto v : data) sq_sum += (v - m) * (v - m);
    return std::sqrt(sq_sum / (data.size() - 1));
}

// === 收益率计算 ===

std::vector<double> VolatilityHandler::simpleReturns(const std::vector<double>& prices) {
    std::vector<double> rets;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1] > 0) {
            rets.push_back((prices[i] - prices[i-1]) / prices[i-1]);
        } else {
            rets.push_back(0.0);
        }
    }
    return rets;
}

// === 单标的指标 ===

double VolatilityHandler::skewness(const std::vector<double>& data) {
    if (data.size() < 3) return 0.0;
    double m = mean(data);
    double s = stddev(data, m);
    if (s < 1e-10) return 0.0;
    double n = data.size();
    double sum3 = 0;
    for (auto v : data) {
        double z = (v - m) / s;
        sum3 += z * z * z;
    }
    // 样本偏度修正
    return (n / ((n-1) * (n-2))) * sum3;
}

double VolatilityHandler::kurtosis(const std::vector<double>& data) {
    if (data.size() < 4) return 0.0;
    double m = mean(data);
    double s = stddev(data, m);
    if (s < 1e-10) return 0.0;
    double n = data.size();
    double sum4 = 0;
    for (auto v : data) {
        double z = (v - m) / s;
        sum4 += z * z * z * z;
    }
    // 超额峰度 (excess kurtosis)
    double k = ((n*(n+1)) / ((n-1)*(n-2)*(n-3))) * sum4;
    double correction = (3.0 * (n-1) * (n-1)) / ((n-2) * (n-3));
    return k - correction;
}

// === ACF 衰减模式分析 ===

// 简单线性回归 y = a + b*x，返回 {a, b, r2}
static std::tuple<double, double, double> linearRegression(
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

    // R²
    double y_mean = sy / n;
    double ss_tot = 0, ss_res = 0;
    for (size_t i = 0; i < n; ++i) {
        ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
        ss_res += (y[i] - (a + b * x[i])) * (y[i] - (a + b * x[i]));
    }
    double r2 = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0;

    return {a, b, r2};
}

static const char* decayModeToString(ACFDecayMode mode) {
    switch (mode) {
        case ACFDecayMode::Exponential:  return "exponential";
        case ACFDecayMode::Hyperbolic:   return "hyperbolic";
        case ACFDecayMode::Inconclusive: return "inconclusive";
    }
    return "unknown";
}

ACFDecayAnalysis VolatilityHandler::analyzeACFDecay(
    const std::vector<double>& abs_acf,
    const std::vector<double>& abs_returns)
{
    ACFDecayAnalysis result;

    if (abs_acf.size() < 3 || abs_returns.size() < 4) {
        result.decay_mode = ACFDecayMode::Inconclusive;
        return result;
    }

    // 1. Ljung-Box 检验（对 abs_returns）
    int lags = static_cast<int>(abs_acf.size()) - 1;
    auto [Q, pval] = boost::math::statistics::ljung_box(abs_returns, lags, 0);
    result.lb_statistic = Q;
    result.lb_pvalue = pval;
    result.has_autocorrelation = (pval < 0.05);

    // 2. 拟合衰减模式（用 lag 1..max 的 |ACF|）
    std::vector<double> lag_vals;
    std::vector<double> acf_vals;
    for (size_t k = 1; k < abs_acf.size(); ++k) {
        double av = std::abs(abs_acf[k]);
        if (av < 1e-10) continue;
        lag_vals.push_back(static_cast<double>(k));
        acf_vals.push_back(av);
    }

    if (lag_vals.size() < 3) {
        result.decay_mode = ACFDecayMode::Inconclusive;
        return result;
    }

    // 2a. 指数拟合: ln(|ρ(k)|) = ln(a) - b*k
    std::vector<double> log_acf;
    for (auto v : acf_vals) log_acf.push_back(std::log(v));
    auto [exp_a, exp_b, exp_r2] = linearRegression(lag_vals, log_acf);
    result.exponential_r2 = std::max(0.0, exp_r2);

    // 2b. 双曲拟合: ln(|ρ(k)|) = ln(a) - b*ln(k)
    std::vector<double> log_lag;
    for (auto v : lag_vals) log_lag.push_back(std::log(v));
    auto [hyp_a, hyp_b, hyp_r2] = linearRegression(log_lag, log_acf);
    result.hyperbolic_r2 = std::max(0.0, hyp_r2);

    // 3. 判定衰减模式
    double delta_r2 = std::abs(result.exponential_r2 - result.hyperbolic_r2);
    double max_r2 = std::max(result.exponential_r2, result.hyperbolic_r2);

    if (delta_r2 > 0.1 && max_r2 > 0.3) {
        result.decay_mode = (result.exponential_r2 > result.hyperbolic_r2)
            ? ACFDecayMode::Exponential : ACFDecayMode::Hyperbolic;
    } else {
        result.decay_mode = ACFDecayMode::Inconclusive;
    }

    // 4. 半衰期 (仅指数模式)
    if (result.decay_mode == ACFDecayMode::Exponential && exp_b > 1e-10) {
        result.decay_half_life = std::log(2.0) / exp_b;
    }

    // 5. Hurst 指数估计 (从双曲拟合: H = 1 - b/2)
    if (hyp_b > 0) {
        result.hurst_estimate = 1.0 - hyp_b / 2.0;
    }

    return result;
}

// === 单标的完整计算 ===

VolatilitySingleResult VolatilityHandler::computeSingle(
    const std::vector<double>& prices,
    const std::vector<double>& volumes,
    const std::vector<int>& windows)
{
    VolatilitySingleResult result;
    result.prices = prices;
    result.volumes = volumes;
    result.returns = simpleReturns(prices);
    
    if (result.returns.empty()) return result;
    
    // 滚动波动率
    for (int w : windows) {
        result.rolling_vol[w] = rolling_volatility(result.returns, w);
    }
    
    // 波动率包络带 (基于滚动20日)
    int band_window = 20;
    size_t offset = (band_window - 1);
    for (size_t i = 0; i < prices.size(); ++i) {
        if (i < offset) {
            result.mean_price.push_back(prices[i]);
            result.upper_1sigma.push_back(prices[i]);
            result.upper_2sigma.push_back(prices[i]);
            result.lower_1sigma.push_back(prices[i]);
            result.lower_2sigma.push_back(prices[i]);
            continue;
        }
        
        // 计算窗口内的均值和标准差
        double sum = 0;
        for (int j = 0; j < band_window; ++j) sum += prices[i - j];
        double m = sum / band_window;
        double var = 0;
        for (int j = 0; j < band_window; ++j) {
            double d = prices[i - j] - m;
            var += d * d;
        }
        double s = std::sqrt(var / (band_window - 1));
        
        result.mean_price.push_back(m);
        result.upper_1sigma.push_back(m + s);
        result.upper_2sigma.push_back(m + 2 * s);
        result.lower_1sigma.push_back(m - s);
        result.lower_2sigma.push_back(m - 2 * s);
    }
    
    // 汇总指标
    result.annual_volatility = compute_annualized_volatility(result.returns);
    result.max_drawdown = max_drawdown_ratio(prices);
    result.skewness = skewness(result.returns);
    result.kurtosis = kurtosis(result.returns);
    result.var_95 = compute_var(result.returns, 0.95);
    result.cvar_95 = compute_cvar(result.returns, 0.95);
    
    // 波动率聚集 (|returns|)
    result.abs_returns.reserve(result.returns.size());
    for (auto r : result.returns) {
        result.abs_returns.push_back(std::abs(r));
    }
    
    // ACF / PACF (最多 lag 20)
    int max_lag = std::min(20, static_cast<int>(result.returns.size() / 4));
    result.returns_acf = finance::computeACF(result.returns, max_lag);
    result.returns_pacf = finance::computePACF(result.returns_acf, max_lag);
    result.abs_returns_acf = finance::computeACF(result.abs_returns, max_lag);
    result.acf_decay = analyzeACFDecay(result.abs_returns_acf, result.abs_returns);

    return result;
}

// === 多标的计算 ===

VolatilityMultiResult VolatilityHandler::computeMulti(
    const std::map<std::string, std::vector<double>>& returns_map)
{
    VolatilityMultiResult result;
    size_t n = returns_map.size();
    if (n < 2) return result;
    
    // 对齐收益率序列 (取最短长度)
    size_t min_len = std::numeric_limits<size_t>::max();
    for (const auto& [sym, rets] : returns_map) {
        if (rets.size() < min_len) min_len = rets.size();
    }
    
    // 构建收益率矩阵 (n x T)
    std::vector<std::vector<double>> ret_matrix(n, std::vector<double>(min_len));
    size_t idx = 0;
    for (const auto& [sym, rets] : returns_map) {
        for (size_t t = 0; t < min_len; ++t) {
            ret_matrix[idx][t] = rets[rets.size() - min_len + t];
        }
        ++idx;
    }
    
    // 计算协方差矩阵 RᵀR / (T-1)
    result.covariance_matrix.resize(n, std::vector<double>(n, 0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j <= i; ++j) {
            double cov = 0;
            for (size_t t = 0; t < min_len; ++t) {
                cov += ret_matrix[i][t] * ret_matrix[j][t];
            }
            cov /= (min_len - 1);
            result.covariance_matrix[i][j] = cov;
            result.covariance_matrix[j][i] = cov;
        }
    }
    
    // 计算相关系数矩阵
    std::vector<double> stds(n);
    for (size_t i = 0; i < n; ++i) {
        stds[i] = std::sqrt(result.covariance_matrix[i][i]);
    }
    result.correlation_matrix.resize(n, std::vector<double>(n));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (stds[i] > 1e-10 && stds[j] > 1e-10) {
                result.correlation_matrix[i][j] = result.covariance_matrix[i][j] / (stds[i] * stds[j]);
            } else {
                result.correlation_matrix[i][j] = (i == j) ? 1.0 : 0.0;
            }
        }
    }
    
    // 年化波动率
    for (size_t i = 0; i < n; ++i) {
        result.annual_volatility.push_back(std::sqrt(result.covariance_matrix[i][i] * 252.0));
    }
    
    // 特征值分解 (幂迭代法求最大/最小特征值)
    // 简化：用 Gershgorin 圆估计条件数
    double max_eigen = 0;
    double min_eigen = std::numeric_limits<double>::max();
    for (size_t i = 0; i < n; ++i) {
        double center = result.covariance_matrix[i][i];
        double radius = 0;
        for (size_t j = 0; j < n; ++j) {
            if (i != j) radius += std::abs(result.covariance_matrix[i][j]);
        }
        max_eigen = std::max(max_eigen, center + radius);
        min_eigen = std::min(min_eigen, std::max(0.0, center - radius));
    }
    
    result.condition_number = (min_eigen > 1e-15) ? (max_eigen / min_eigen) : 1e15;
    result.is_positive_definite = (min_eigen > 1e-15);
    
    // 简化特征值 (对角线作为近似)
    result.eigenvalues.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.eigenvalues.push_back(result.covariance_matrix[i][i]);
    }
    std::sort(result.eigenvalues.rbegin(), result.eigenvalues.rend());
    
    return result;
}

// === 主计算函数 ===

VolatilityResult VolatilityHandler::compute(
    const std::string& db_path,
    const std::vector<std::string>& symbols,
    const std::string& start_date,
    const std::string& end_date,
    const std::vector<int>& windows,
    PriceField field,
    FillMethod fill)
{
    VolatilityResult result;
    result.symbols = symbols;

    std::map<std::string, std::vector<double>> returns_map;
    std::vector<std::string> common_dates;

    auto getPriceCol = [&](const Map<String, Vector<double>>& data) -> Vector<double> {
        static const char* names[] = {"close", "open", "high", "low", "volume"};
        auto it = data.find(names[(int)field]);
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

    for (const auto& symbol : symbols) {
        Vector<String> dates;
        Vector<double> prices_vec;
        Vector<double> volumes_vec;

        // 检测是否为宏观指标 (格式: country/indicator)
        auto slash = symbol.find('/');
        bool is_macro = (slash != std::string::npos && symbol.size() > slash + 1 && symbol.find('.', slash) == std::string::npos);

        if (is_macro) {
            // 宏观指标数据
            if (!FetchMacroData(symbol, db_path, dates, prices_vec)) {
                WARN("[Volatility] No macro data for {}", symbol);
                continue;
            }
            volumes_vec.assign(prices_vec.size(), 0.0);  // 宏观数据无成交量
        } else {
            // 股票/ETF行情数据
            auto multi = LoadHistoryData(symbol, {"close", "open", "high", "low", "volume", "turnover"},
                                          start_date, end_date, &dates, fill);
            Vector<double> prices = getPriceCol(multi);
            Vector<double> volumes = getVolumeCol(multi);
            if (prices.empty()) {
                WARN("[Volatility] No data for {}", symbol);
                continue;
            }
            prices_vec.assign(prices.begin(), prices.end());
            volumes_vec.assign(volumes.begin(), volumes.end());
            if (volumes_vec.empty()) volumes_vec.assign(prices_vec.size(), 0.0);
        }

        auto single_result = computeSingle(prices_vec, volumes_vec, windows);
        result.single[symbol] = single_result;
        returns_map[symbol] = simpleReturns(prices_vec);

        if (common_dates.empty()) {
            common_dates.assign(dates.begin(), dates.end());
        }
    }

    result.dates = common_dates;
    result.multi = computeMulti(returns_map);

    return result;
}

// === HTTP Handler (GET 请求) ===

void VolatilityHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();

        // 从 URL 参数获取
        auto symbols_param = req.get_param_value("symbols");
        auto start_date_param = req.get_param_value("start_date");
        auto end_date_param = req.get_param_value("end_date");
        auto windows_param = req.get_param_value("windows");
        auto field_param = req.get_param_value("field");
        auto fill_param = req.get_param_value("fill_method");

        if (symbols_param.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"symbols parameter is required"})", "application/json");
            return;
        }

        // 解析 symbols (逗号分隔)
        std::vector<std::string> symbols;
        std::istringstream symbols_stream(symbols_param);
        std::string symbol;
        while (std::getline(symbols_stream, symbol, ',')) {
            if (!symbol.empty()) {
                symbols.push_back(symbol);
            }
        }

        std::string start_date = start_date_param.empty() ? "" : start_date_param;
        std::string end_date = end_date_param.empty() ? "" : end_date_param;

        std::vector<int> windows = {20, 60, 120};
        if (!windows_param.empty()) {
            windows.clear();
            std::istringstream windows_stream(windows_param);
            std::string w;
            while (std::getline(windows_stream, w, ',')) {
                if (!w.empty()) {
                    windows.push_back(std::stoi(w));
                }
            }
        }

        PriceField field = parsePriceField(field_param);
        FillMethod fill = fill_param.empty() ? FillMethod::None : parseFillMethod(fill_param);

        auto result = compute(db_path, symbols, start_date, end_date, windows, field, fill);

        // 构建 JSON 响应
        nlohmann::json json;
        json["symbols"] = result.symbols;
        json["dates"] = result.dates;

        // 返回使用的字段名
        static const char* field_names[] = {"close", "open", "high", "low", "volume"};
        json["field"] = field_names[static_cast<int>(field)];

        // 单标的
        nlohmann::json single_json;
        for (const auto& [symbol, sr] : result.single) {
            nlohmann::json item;
            item["prices"] = sr.prices;
            item["returns"] = sr.returns;
            item["volumes"] = sr.volumes;
            item["abs_returns"] = sr.abs_returns;

            nlohmann::json rolling;
            for (const auto& [w, vals] : sr.rolling_vol) {
                rolling[std::to_string(w)] = vals;
            }
            item["rolling_vol"] = rolling;

            item["upper_2sigma"] = sr.upper_2sigma;
            item["upper_1sigma"] = sr.upper_1sigma;
            item["mean_price"] = sr.mean_price;
            item["lower_1sigma"] = sr.lower_1sigma;
            item["lower_2sigma"] = sr.lower_2sigma;

            item["annual_volatility"] = sr.annual_volatility;
            item["max_drawdown"] = sr.max_drawdown;
            item["skewness"] = sr.skewness;
            item["kurtosis"] = sr.kurtosis;
            item["var_95"] = sr.var_95;
            item["cvar_95"] = sr.cvar_95;

            item["returns_acf"] = sr.returns_acf;
            item["returns_pacf"] = sr.returns_pacf;
            item["abs_returns_acf"] = sr.abs_returns_acf;

            // ACF 衰减分析
            nlohmann::json decay;
            decay["lb_statistic"] = sr.acf_decay.lb_statistic;
            decay["lb_pvalue"] = sr.acf_decay.lb_pvalue;
            decay["has_autocorrelation"] = sr.acf_decay.has_autocorrelation;
            decay["exponential_r2"] = sr.acf_decay.exponential_r2;
            decay["hyperbolic_r2"] = sr.acf_decay.hyperbolic_r2;
            decay["decay_mode"] = decayModeToString(sr.acf_decay.decay_mode);
            decay["decay_half_life"] = sr.acf_decay.decay_half_life;
            decay["hurst_estimate"] = sr.acf_decay.hurst_estimate;
            item["acf_decay"] = decay;

            single_json[symbol] = item;
        }
        json["single"] = single_json;

        // 多标的
        nlohmann::json multi_json;
        multi_json["correlation_matrix"] = result.multi.correlation_matrix;
        multi_json["covariance_matrix"] = result.multi.covariance_matrix;
        multi_json["eigenvalues"] = result.multi.eigenvalues;
        multi_json["condition_number"] = result.multi.condition_number;
        multi_json["is_positive_definite"] = result.multi.is_positive_definite;
        multi_json["annual_volatility"] = result.multi.annual_volatility;
        json["multi"] = multi_json;

        res.set_content(json.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[Volatility] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}
