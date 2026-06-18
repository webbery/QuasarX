#include "Util/data.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

// === FillMethod 解析 ===

FillMethod parseFillMethod(const String& str) {
    if (str == "forward" || str == "ffill") return FillMethod::ForwardFill;
    if (str == "backward" || str == "bfill") return FillMethod::BackwardFill;
    if (str == "linear" || str == "interp") return FillMethod::Linear;
    if (str == "zero" || str == "zero_fill") return FillMethod::ZeroFill;
    return FillMethod::None;
}

String toString(FillMethod method) {
    switch (method) {
        case FillMethod::None:         return "none";
        case FillMethod::ForwardFill:  return "forward";
        case FillMethod::BackwardFill: return "backward";
        case FillMethod::Linear:       return "linear";
        case FillMethod::ZeroFill:     return "zero";
    }
    return "unknown";
}

// === 核心 CSV 数据加载 ===

static Map<String, Vector<double>> loadCsvData(
    const String& symbol,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    Vector<String>* out_dates)
{
    std::string base_dir = "./data";

    // 规范化 symbol（统一小写）
    std::string normalized = symbol;
    std::transform(normalized.begin(), normalized.end(),
                   normalized.begin(), ::tolower);

    // 尝试多个可能的路径
    Vector<String> search_paths;
    if (symbol.find('.') == String::npos) {
        search_paths.push_back(base_dir + "/A_hfq/sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/A_hfq/sh." + symbol + ".csv");
        search_paths.push_back(base_dir + "/Astock/sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/Astock/sh." + symbol + ".csv");
    } else {
        search_paths.push_back(base_dir + "/A_hfq/" + normalized + ".csv");
        search_paths.push_back(base_dir + "/Astock/" + normalized + ".csv");
    }

    String data_path;
    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            data_path = path;
            break;
        }
    }

    if (data_path.empty()) {
        WARN("[LoadHistoryData] Data file not found for symbol: {}", symbol);
        return {};
    }

    std::ifstream file(data_path);
    if (!file.is_open()) {
        WARN("[LoadHistoryData] Cannot open: {}", data_path);
        return {};
    }

    // CSV 列顺序: datetime,open,close,high,low,volume,turnover
    Map<String, int> field_col_map;
    field_col_map["open"] = 1;
    field_col_map["close"] = 2;
    field_col_map["high"] = 3;
    field_col_map["low"] = 4;
    field_col_map["volume"] = 5;
    field_col_map["turnover"] = 6;

    // 初始化结果 map
    Map<String, Vector<double>> result;
    for (const auto& f : fields) {
        String lower_f = f;
        std::transform(lower_f.begin(), lower_f.end(), lower_f.begin(), ::tolower);
        if (field_col_map.count(lower_f)) {
            result[lower_f] = Vector<double>{};
        }
    }

    time_t start_t = start_date.empty() ? 0 : FromStr(start_date, "%Y-%m-%d");
    time_t end_t = end_date.empty() ? 0 : FromStr(end_date, "%Y-%m-%d");

    Vector<String> dates;
    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        String tokens[7];
        int col = 0;
        while (col < 7 && std::getline(iss, tokens[col], ',')) ++col;
        if (col < 6) continue;

        time_t t = FromStr(tokens[0], "%Y-%m-%d");
        if (t < 0) continue;

        if (start_t > 0 && t < start_t) continue;
        if (end_t > 0 && t > end_t) continue;

        dates.push_back(tokens[0]);

        for (const auto& [f, ci] : field_col_map) {
            auto it = result.find(f);
            if (it != result.end()) {
                try {
                    it->second.push_back(std::stod(tokens[ci]));
                } catch (...) {
                    it->second.push_back(0.0);
                }
            }
        }
    }

    if (out_dates) {
        *out_dates = std::move(dates);
    }

    return result;
}

// === 填充实现 ===

static void applyForwardFill(Map<String, Vector<double>>& data, size_t target_len) {
    for (auto& [field, values] : data) {
        if (values.size() >= target_len) continue;
        size_t orig_len = values.size();
        values.resize(target_len);
        double last_val = (orig_len > 0) ? values[orig_len - 1] : 0.0;
        for (size_t i = orig_len; i < target_len; ++i) {
            values[i] = last_val;
        }
    }
}

static void applyBackwardFill(Map<String, Vector<double>>& data, size_t target_len) {
    for (auto& [field, values] : data) {
        if (values.size() >= target_len) continue;
        size_t orig_len = values.size();
        // 在前面填充，用第一个已知值
        double first_val = (orig_len > 0) ? values[0] : 0.0;
        values.insert(values.begin(), target_len - orig_len, first_val);
    }
}

static void applyZeroFill(Map<String, Vector<double>>& data, size_t target_len) {
    for (auto& [field, values] : data) {
        if (values.size() >= target_len) continue;
        size_t orig_len = values.size();
        values.insert(values.begin(), target_len - orig_len, 0.0);
    }
}

static void applyLinearFill(Map<String, Vector<double>>& data, size_t target_len) {
    for (auto& [field, values] : data) {
        if (values.size() >= target_len) continue;
        size_t orig_len = values.size();
        if (orig_len == 0) {
            values.assign(target_len, 0.0);
            continue;
        }
        if (orig_len == 1) {
            values.assign(target_len, values[0]);
            continue;
        }

        double first = values[0];
        double last = values[orig_len - 1];
        size_t pad_before = (target_len - orig_len + 1) / 2;
        size_t pad_after = target_len - orig_len - pad_before;

        // 前面：用 first 和 last 之间的线性外推（简化为常量 first）
        Vector<double> filled;
        for (size_t i = 0; i < pad_before; ++i) {
            filled.push_back(first);
        }
        // 原始数据
        for (size_t i = 0; i < orig_len; ++i) {
            filled.push_back(values[i]);
        }
        // 后面：用 last 值（线性简化）
        for (size_t i = 0; i < pad_after; ++i) {
            double t = (double)(i + 1) / (pad_after + 1);
            filled.push_back(last + t * (last - values[orig_len - 2]));
        }

        values = std::move(filled);
    }
}

// === 主接口 ===

Map<String, Vector<double>> LoadHistoryData(
    const String& symbol,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    Vector<String>* out_dates,
    FillMethod fill)
{
    // 第一步：加载原始数据
    auto data = loadCsvData(symbol, fields, start_date, end_date, out_dates);
    if (data.empty()) return {};

    // 第二步：计算最大长度
    size_t max_len = 0;
    for (const auto& [f, values] : data) {
        if (values.size() > max_len) max_len = values.size();
    }

    // 如果所有列长度一致且不需要填充，直接返回
    bool all_aligned = true;
    for (const auto& [f, values] : data) {
        if (values.size() != max_len) {
            all_aligned = false;
            break;
        }
    }
    if (all_aligned) return data;

    // 第三步：按策略填充较短的列
    switch (fill) {
        case FillMethod::None: {
            // 截断到最短长度
            size_t min_len = max_len;
            for (const auto& [f, values] : data) {
                if (values.size() < min_len) min_len = values.size();
            }
            for (auto& [f, values] : data) {
                if (values.size() > min_len) {
                    values.resize(min_len);
                }
            }
            break;
        }
        case FillMethod::ForwardFill:
            applyForwardFill(data, max_len);
            break;
        case FillMethod::BackwardFill:
            applyBackwardFill(data, max_len);
            break;
        case FillMethod::Linear:
            applyLinearFill(data, max_len);
            break;
        case FillMethod::ZeroFill:
            applyZeroFill(data, max_len);
            break;
    }

    return data;
}

// === 宏观经济数据获取 ===

namespace {
namespace fs = std::filesystem;

String GetMacroCachePath(const String& db_path, const String& country, const String& indicator) {
    return db_path + "/macro/" + country + "/" + indicator + ".json";
}

bool ReadMacroCache(const String& db_path, const String& country, const String& indicator, nlohmann::json& out) {
    String path = GetMacroCachePath(db_path, country, indicator);
    if (!fs::exists(path)) return false;
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        ifs >> out;
        return out.contains("data") && out["data"].is_array();
    }
    catch (...) { return false; }
}

bool IsMacroCacheExpired(const nlohmann::json& cached) {
    if (!cached.contains("updated_at")) return true;
    String updated_at = cached["updated_at"];
    struct tm tm_val = {};
    if (sscanf(updated_at.c_str(), "%d-%d-%dT%d:%d:%d",
               &tm_val.tm_year, &tm_val.tm_mon, &tm_val.tm_mday,
               &tm_val.tm_hour, &tm_val.tm_min, &tm_val.tm_sec) != 6) return true;
    tm_val.tm_year -= 1900;
    tm_val.tm_mon -= 1;
    time_t cache_time = mktime(&tm_val);
    auto now = std::chrono::system_clock::now();
    time_t now_t = std::chrono::system_clock::to_time_t(now);
    return (now_t - cache_time) > (7 * 24 * 3600);  // 7天
}

bool FetchMacroFromPython(const String& country, const String& indicator, nlohmann::json& out) {
    String cmd = fmt::format("python3 tools/fetch_macro_data.py --indicator {} --country {} --json", indicator, country);
    String output;
    bool success = RunCommand(cmd, output);
    if (!success || output.empty()) {
        WARN("[MacroData] Python脚本执行失败: {}", cmd);
        return false;
    }
    try {
        out = nlohmann::json::parse(output);
        return out.contains("data") && out["data"].is_array();
    }
    catch (const std::exception& e) {
        WARN("[MacroData] 解析Python输出失败: {}", e.what());
        return false;
    }
}

bool SaveMacroCache(const String& db_path, const String& country, const String& indicator, const nlohmann::json& data) {
    String path = GetMacroCachePath(db_path, country, indicator);
    String dir = fs::path(path).parent_path().string();
    try {
        fs::create_directories(dir);
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << data.dump(2);
        return true;
    }
    catch (...) { return false; }
}
} // anonymous namespace

bool FetchMacroData(
    const String& symbol,
    const String& db_path,
    Vector<String>& out_dates,
    Vector<double>& out_prices)
{
    auto slash = symbol.find('/');
    if (slash == std::string::npos) return false;
    std::string country = symbol.substr(0, slash);
    std::string indicator = symbol.substr(slash + 1);

    nlohmann::json cached_data;
    bool cache_ok = ReadMacroCache(db_path, country, indicator, cached_data);
    bool cache_expired = !cache_ok || IsMacroCacheExpired(cached_data);

    nlohmann::json data;
    bool fetch_ok = false;
    if (cache_ok && !cache_expired) {
        data = cached_data;
        fetch_ok = true;
    } else {
        fetch_ok = FetchMacroFromPython(country, indicator, data);
        if (fetch_ok) SaveMacroCache(db_path, country, indicator, data);
        else if (cache_ok) data = cached_data;
        else return false;
    }

    out_dates.clear();
    out_prices.clear();
    for (const auto& item : data["data"]) {
        if (item.contains("date") && item.contains("value")) {
            std::string d = item["date"].get<std::string>();
            // 跳过非标准日期（如"美国CPI月率报告"）
            if (d.size() < 10 || d[4] != '-') continue;
            auto v = item["value"];
            if (v.is_number()) {
                out_dates.push_back(d);
                out_prices.push_back(v.get<double>());
            }
        }
    }
    return !out_prices.empty();
}
