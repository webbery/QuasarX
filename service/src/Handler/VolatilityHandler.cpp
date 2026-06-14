#include "Handler/VolatilityHandler.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

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

// === 加载历史价格 ===

std::vector<double> VolatilityHandler::loadPrices(
    const std::string& symbol,
    const std::string& start_date,
    const std::string& end_date,
    std::vector<std::string>& out_dates,
    std::vector<double>& out_volumes)
{
    // 获取程序运行目录的绝对路径
    std::string base_dir = "./data";
    
    // 尝试多种可能的路径格式
    std::vector<std::string> search_paths;
    
    // 原始symbol格式可能是: sz.000001, SH.000001, 000001等
    // 数据文件名格式: sz.000001.csv (小写交易所前缀)
    std::string normalized_symbol = symbol;
    
    // 如果symbol没有交易所前缀，尝试添加常见前缀
    if (symbol.find('.') == std::string::npos) {
        // 纯数字代码，尝试常见交易所
        search_paths.push_back(base_dir + "/Astock/" + "sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/Astock/" + "sh." + symbol + ".csv");
        search_paths.push_back(base_dir + "/A_hfq/" + "sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/A_hfq/" + "sh." + symbol + ".csv");
    } else {
        // 已有前缀，统一转为小写
        std::transform(normalized_symbol.begin(), normalized_symbol.end(), 
                      normalized_symbol.begin(), ::tolower);
        search_paths.push_back(base_dir + "/Astock/" + normalized_symbol + ".csv");
        search_paths.push_back(base_dir + "/A_hfq/" + normalized_symbol + ".csv");
    }
    
    std::string data_path;
    for (const auto& path : search_paths) {
        if (fs::exists(path)) {
            data_path = path;
            break;
        }
    }
    
    if (data_path.empty()) {
        WARN("[Volatility] Data file not found for symbol: {}", symbol);
        for (const auto& path : search_paths) {
            WARN("  Tried: {}", path);
        }
        return {};
    }

    std::ifstream file(data_path);
    if (!file.is_open()) {
        WARN("[Volatility] Cannot open: {}", data_path);
        return {};
    }

    std::string line;
    std::getline(file, line); // skip header

    std::vector<double> closes;
    time_t start_t = FromStr(start_date, "%Y-%m-%d");
    time_t end_t = FromStr(end_date, "%Y-%m-%d");

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string date_str, open_str, close_str, high_str, low_str, vol_str;
        
        if (!std::getline(iss, date_str, ',')) continue;
        if (!std::getline(iss, open_str, ',')) continue;
        if (!std::getline(iss, close_str, ',')) continue;
        std::getline(iss, high_str, ','); // skip
        std::getline(iss, low_str, ',');  // skip
        std::getline(iss, vol_str, ',');

        time_t t = FromStr(date_str, "%Y-%m-%d");
        if (t < 0) continue;

        if (start_t > 0 && t < start_t) continue;
        if (end_t > 0 && t > end_t) continue;

        double close = std::stod(close_str);
        double vol = vol_str.empty() ? 0 : std::stod(vol_str);

        closes.push_back(close);
        out_dates.push_back(date_str);
        out_volumes.push_back(vol);
    }

    return closes;
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

double VolatilityHandler::annualVolatility(const std::vector<double>& returns) {
    if (returns.size() < 2) return 0.0;
    return stddev(returns) * std::sqrt(252.0);
}

double VolatilityHandler::maxDrawdown(const std::vector<double>& prices) {
    if (prices.empty()) return 0.0;
    double peak = prices[0];
    double max_dd = 0.0;
    for (double v : prices) {
        if (v > peak) peak = v;
        double dd = (peak - v) / peak;
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd;
}

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

double VolatilityHandler::computeVar(const std::vector<double>& returns, double confidence) {
    if (returns.empty()) return 0.0;
    auto sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    int idx = static_cast<int>((1.0 - confidence) * sorted.size());
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return -sorted[idx];
}

double VolatilityHandler::computeCVaR(const std::vector<double>& returns, double confidence) {
    if (returns.empty()) return 0.0;
    auto sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    int idx = static_cast<int>((1.0 - confidence) * sorted.size());
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    if (idx <= 0) return -sorted[0];
    double sum = 0;
    for (int i = 0; i <= idx; ++i) sum += sorted[i];
    return -(sum / (idx + 1));
}

std::vector<double> VolatilityHandler::rollingVol(const std::vector<double>& returns, int window) {
    std::vector<double> result;
    if (returns.size() < window) return result;
    
    for (size_t i = window - 1; i < returns.size(); ++i) {
        double m = 0;
        for (int j = 0; j < window; ++j) m += returns[i - j];
        m /= window;
        double var = 0;
        for (int j = 0; j < window; ++j) {
            double d = returns[i - j] - m;
            var += d * d;
        }
        var /= (window - 1);
        result.push_back(std::sqrt(var) * std::sqrt(252.0));
    }
    return result;
}

// === ACF / PACF ===

std::vector<double> VolatilityHandler::computeACF(const std::vector<double>& data, int max_lag) {
    std::vector<double> acf;
    if (data.empty()) return acf;
    
    double m = mean(data);
    double var = 0;
    for (auto v : data) var += (v - m) * (v - m);
    if (var < 1e-10) {
        acf.resize(max_lag + 1, 1.0);
        return acf;
    }
    
    int n = data.size();
    for (int lag = 0; lag <= max_lag && lag < n; ++lag) {
        double cov = 0;
        for (int i = 0; i < n - lag; ++i) {
            cov += (data[i] - m) * (data[i + lag] - m);
        }
        acf.push_back(cov / var);
    }
    return acf;
}

std::vector<double> VolatilityHandler::computePACF(const std::vector<double>& acf, int max_lag) {
    std::vector<double> pacf;
    int n = acf.size() - 1;  // max lag
    if (n < 0) return pacf;
    
    // Durbin-Levinson algorithm
    std::vector<std::vector<double>> phi(n + 1, std::vector<double>(n + 1, 0));
    
    pacf.push_back(1.0);  // lag 0
    if (n >= 1) {
        phi[1][1] = acf[1];
        pacf.push_back(phi[1][1]);
    }
    
    for (int k = 2; k <= n; ++k) {
        double num = acf[k];
        for (int j = 1; j < k; ++j) {
            num -= phi[k-1][j] * acf[k - j];
        }
        double den = 1.0;
        for (int j = 1; j < k; ++j) {
            den -= phi[k-1][j] * acf[j];
        }
        if (std::abs(den) < 1e-10) {
            phi[k][k] = 0;
        } else {
            phi[k][k] = num / den;
        }
        pacf.push_back(phi[k][k]);
        
        for (int j = 1; j < k; ++j) {
            phi[k][j] = phi[k-1][j] - phi[k][k] * phi[k-1][k-j];
        }
    }
    
    return pacf;
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
        result.rolling_vol[w] = rollingVol(result.returns, w);
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
    result.annual_volatility = annualVolatility(result.returns);
    result.max_drawdown = maxDrawdown(prices);
    result.skewness = skewness(result.returns);
    result.kurtosis = kurtosis(result.returns);
    result.var_95 = computeVar(result.returns, 0.95);
    result.cvar_95 = computeCVaR(result.returns, 0.95);
    
    // 波动率聚集 (|returns|)
    result.abs_returns.reserve(result.returns.size());
    for (auto r : result.returns) {
        result.abs_returns.push_back(std::abs(r));
    }
    
    // ACF / PACF (最多 lag 20)
    int max_lag = std::min(20, static_cast<int>(result.returns.size() / 4));
    result.returns_acf = computeACF(result.returns, max_lag);
    result.returns_pacf = computePACF(result.returns_acf, max_lag);
    result.abs_returns_acf = computeACF(result.abs_returns, max_lag);
    
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
    const std::vector<std::string>& symbols,
    const std::string& start_date,
    const std::string& end_date,
    const std::vector<int>& windows)
{
    VolatilityResult result;
    result.symbols = symbols;
    
    std::map<std::string, std::vector<double>> returns_map;
    std::vector<std::string> common_dates;
    bool dates_set = false;
    
    for (const auto& symbol : symbols) {
        std::vector<std::string> dates;
        std::vector<double> volumes;
        auto prices = loadPrices(symbol, start_date, end_date, dates, volumes);
        
        if (prices.empty()) {
            WARN("[Volatility] No data for {}", symbol);
            continue;
        }
        
        auto single_result = computeSingle(prices, volumes, windows);
        result.single[symbol] = single_result;
        returns_map[symbol] = simpleReturns(prices);
        
        if (!dates_set) {
            common_dates = dates;
            dates_set = true;
        }
    }
    
    result.dates = common_dates;
    result.multi = computeMulti(returns_map);
    
    return result;
}

// === HTTP Handler (GET 请求) ===

void VolatilityHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        // 从 URL 参数获取
        auto symbols_param = req.get_param_value("symbols");
        auto start_date_param = req.get_param_value("start_date");
        auto end_date_param = req.get_param_value("end_date");
        auto windows_param = req.get_param_value("windows");

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

        auto result = compute(symbols, start_date, end_date, windows);

        // 构建 JSON 响应
        nlohmann::json json;
        json["symbols"] = result.symbols;
        json["dates"] = result.dates;

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
