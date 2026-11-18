#include "Util/data.h"
#include "Util/QuoteDB.h"
#include "Util/FinanceDB.h"
#include "Util/PythonRunner.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

// === BarFreq 解析 ===

BarFreq parseBarFreq(const String& str) {
    String lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "1m" || lower == "min1") return BarFreq::Min1;
    if (lower == "5m" || lower == "min5") return BarFreq::Min5;
    if (lower == "15m" || lower == "min15") return BarFreq::Min15;
    if (lower == "30m" || lower == "min30") return BarFreq::Min30;
    if (lower == "1h" || lower == "hour1") return BarFreq::Hour1;
    if (lower == "2h" || lower == "hour2") return BarFreq::Hour2;
    if (lower == "4h" || lower == "hour4") return BarFreq::Hour4;
    if (lower == "1d" || lower == "day" || lower == "daily") return BarFreq::Day;
    if (lower == "1w" || lower == "week" || lower == "weekly") return BarFreq::Week;
    if (lower == "1M" || lower == "month" || lower == "monthly") return BarFreq::Month;
    return BarFreq::Day;  // 默认日线
}

String toString(BarFreq freq) {
    switch (freq) {
        case BarFreq::Min1:  return "1m";
        case BarFreq::Min5:  return "5m";
        case BarFreq::Min15: return "15m";
        case BarFreq::Min30: return "30m";
        case BarFreq::Hour1: return "1h";
        case BarFreq::Hour2: return "2h";
        case BarFreq::Hour4: return "4h";
        case BarFreq::Day:   return "1d";
        case BarFreq::Week:  return "1w";
        case BarFreq::Month: return "1M";
    }
    return "unknown";
}

// === ETF 代码判断 ===

/// 根据股票代码字符串判断是否为场内ETF
/// 上交所ETF: 51xxxx, 58xxxx; 深交所ETF: 15xxxx
static bool is_etf_symbol(const String& symbol_normalized) {
    // 尝试从完整 symbol 中提取纯数字代码
    String code = symbol_normalized;
    auto dot_pos = code.find('.');
    if (dot_pos != String::npos) {
        code = code.substr(0, dot_pos);
    }
    if (code.size() < 6) return false;

    int prefix = 0;
    try {
        prefix = std::stoi(code.substr(0, 3));
    } catch (...) {
        return false;
    }

    // 上交所ETF: 510xxx~519xxx, 588xxx
    // 深交所ETF: 159xxx
    return (prefix >= 510 && prefix <= 519) || (prefix == 588) || (prefix == 159);
}

// === 静态路径表（方案 C）===

// 编译期常量，避免运行时分支
// [资产类型][复权类型] → 子目录名
// 资产类型：0=股票, 1=ETF
static constexpr const char* kSubdirTable[2][2] = {
    {"A_hfq",  "Astock"},   // 股票: [HFQ, None]
    {"etf_hfq", "etf_org"}, // ETF:  [HFQ, None]
};

// 内联函数：解析子目录（零开销，编译期可计算）
inline String resolveSubdir(bool is_etf, AdjType adj) {
    return kSubdirTable[is_etf ? 1 : 0][static_cast<int>(adj)];
}

// === 核心 CSV 数据加载 ===

/// 实际 CSV 解析函数（从已打开的文件中读取）
static Map<String, Vector<double>> parseCsvFile(
    std::ifstream& file,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    Vector<String>* out_dates)
{
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
    String line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        String tokens[7];
        int col = 0;
        while (col < 7 && std::getline(iss, tokens[col], ',')) ++col;
        if (col < 6) continue;

        time_t t = FromStr(tokens[0], "%Y-%m-%d");
        // 也支持带时间的格式 "YYYY-MM-DD HH:MM:SS"
        if (t < 0) t = FromStr(tokens[0], "%Y-%m-%d %H:%M:%S");
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

/// 从指定路径加载数据
static Map<String, Vector<double>> loadCsvDataFromPath(
    const String& data_path,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    Vector<String>* out_dates)
{
    if (data_path.empty() || !std::filesystem::exists(data_path)) {
        return {};
    }

    std::ifstream file(data_path);
    if (!file.is_open()) {
        WARN("[loadCsvDataFromPath] Cannot open: {}", data_path);
        return {};
    }

    return parseCsvFile(file, fields, start_date, end_date, out_dates);
}

/// 根据 symbol 搜索并加载数据（无频率限制）
static Map<String, Vector<double>> loadCsvData(
    const String& symbol,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    Vector<String>* out_dates)
{
    String base_dir = "./data";

    // 规范化 symbol（统一小写）
    String normalized = symbol;
    std::transform(normalized.begin(), normalized.end(),
                   normalized.begin(), ::tolower);

    // 尝试多个可能的路径
    Vector<String> search_paths;
    if (symbol.find('.') == String::npos) {
        // 无后缀：尝试 sz/sh 前缀
        search_paths.push_back(base_dir + "/A_hfq/sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/A_hfq/sh." + symbol + ".csv");
        search_paths.push_back(base_dir + "/Astock/sz." + symbol + ".csv");
        search_paths.push_back(base_dir + "/Astock/sh." + symbol + ".csv");
    } else {
        // 有后缀：将 CODE.EXCHANGE 转换为 exchange.code 格式
        // 例如: 000001.SZ → sz.000001, 600000.SH → sh.600000
        auto dot_pos = normalized.find('.');
        String code = normalized.substr(0, dot_pos);
        String exchange = normalized.substr(dot_pos + 1);

        search_paths.push_back(base_dir + "/A_hfq/" + exchange + "." + code + ".csv");
        search_paths.push_back(base_dir + "/Astock/" + exchange + "." + code + ".csv");
        // 也尝试原始格式（兼容已有文件）
        search_paths.push_back(base_dir + "/A_hfq/" + normalized + ".csv");
        search_paths.push_back(base_dir + "/Astock/" + normalized + ".csv");
    }

    // 新增：ETF 数据路径（后复权优先，用于指标计算）
    if (is_etf_symbol(normalized)) {
        // ETF 数据按频率子目录存储，默认使用 5m 日线级数据
        String etf_code = normalized;
        auto dot_pos = etf_code.find('.');
        if (dot_pos != String::npos) {
            String code = etf_code.substr(0, dot_pos);
            String exchange = etf_code.substr(dot_pos + 1);
            String etf_formatted = code + "." + exchange;  // CODE.EXCHANGE 格式 (如 510300.SH)

            // 后复权数据（指标计算用，与回测一致）
            search_paths.push_back(base_dir + "/etf_hfq/5m/" + etf_formatted + ".csv");
            search_paths.push_back(base_dir + "/etf_hfq/1m/" + etf_formatted + ".csv");
            // 原始数据（价格撮合用）
            search_paths.push_back(base_dir + "/etf_org/5m/" + etf_formatted + ".csv");
            search_paths.push_back(base_dir + "/etf_org/1m/" + etf_formatted + ".csv");
        }
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
    String line;
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

bool IsMacroCacheExpired(const nlohmann::json& cached, bool is_fred) {
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
    // FRED 数据使用更短的缓存时间 (1天 vs 7天)
    int ttl = is_fred ? (24 * 3600) : (7 * 24 * 3600);
    return (now_t - cache_time) > ttl;
}

/** 判断指标是否为 FRED 指标 */
bool IsFredIndicator(const String& indicator) {
    static const std::set<String> fred_indicators = {
        "sofr", "treasury_yield", "treasury_spread", "fx_rate"
    };
    return fred_indicators.count(indicator) > 0;
}

bool FetchMacroFromPython(const String& country, const String& indicator, nlohmann::json& out) {
    String cmd;

    if (IsFredIndicator(indicator)) {
        // FRED 指标: 调用 fetch_fred_data.py
        String api_key = "";  // 从 config 获取需要 server 实例，这里通过环境变量传递
        const char* env_key = std::getenv("FRED_API_KEY");
        if (env_key) api_key = env_key;

        if (api_key.empty()) {
            WARN("[MacroData] FRED API Key 未配置 (环境变量 FRED_API_KEY)");
            return false;
        }

        // 映射 indicator -> series IDs
        std::map<String, std::vector<String>> fred_map = {
            {"sofr",            {"SOFR"}},
            {"treasury_yield",  {"DGS2", "DGS5", "DGS10", "DGS30"}},
            {"treasury_spread", {"T10Y2Y", "T10Y3M"}},
            {"fx_rate",         {"DEXCHUS", "DEXUSEU", "DEXJPUS", "DEXHKUS", "DEXUSUK", "DEXUSAL", "DEXUSCA"}},
        };

        auto it = fred_map.find(indicator);
        if (it == fred_map.end()) {
            WARN("[MacroData] 未知的 FRED 指标: {}", indicator);
            return false;
        }

        cmd = fmt::format("python3 tools/fetch_fred_data.py --api-key {} --series", api_key);
        for (const auto& sid : it->second) {
            cmd += fmt::format(" {}", sid);
        }
        cmd += " --json";
    } else {
        // akshare 指标: 调用 fetch_macro_data.py
        cmd = fmt::format("python3 tools/fetch_macro_data.py --indicator {} --country {} --json", indicator, country);
    }

    String output;
    bool success = RunCommand(cmd, output);
    if (!success || output.empty()) {
        WARN("[MacroData] Python脚本执行失败: {}", cmd);
        return false;
    }
    try {
        auto parsed = nlohmann::json::parse(output);

        if (IsFredIndicator(indicator)) {
            // FRED 输出格式: {"updated_at": "...", "series": [{"series_id": "...", "data": [...]}]}
            // 转换为标准格式: {"data": [{"date": "...", "value": ...}], ...}
            if (!parsed.contains("series") || !parsed["series"].is_array()) {
                WARN("[MacroData] FRED 输出格式错误");
                return false;
            }

            nlohmann::json data_array = nlohmann::json::array();
            const auto& series_arr = parsed["series"];
            bool multi = (series_arr.size() > 1);

            for (const auto& s : series_arr) {
                if (!s.contains("data")) continue;
                String sid = s.value("series_id", "");
                for (const auto& item : s["data"]) {
                    nlohmann::json entry;
                    entry["date"] = item.value("date", "");
                    entry["value"] = item.value("value", 0.0);
                    if (multi) entry["series_id"] = sid;
                    data_array.push_back(entry);
                }
            }

            out["data"] = data_array;
            out["updated_at"] = parsed.value("updated_at", "");
            return true;
        }

        // akshare 格式: {"data": [...], "name": "...", ...}
        out = parsed;
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
    if (slash == String::npos) return false;
    String country = symbol.substr(0, slash);
    String indicator = symbol.substr(slash + 1);

    nlohmann::json cached_data;
    bool cache_ok = ReadMacroCache(db_path, country, indicator, cached_data);
    bool is_fred = IsFredIndicator(indicator);
    bool cache_expired = !cache_ok || IsMacroCacheExpired(cached_data, is_fred);

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
            String d = item["date"].get<String>();
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

// === 频率解析与聚合 ===

/// 解析 datetime 字符串为 time_t
static time_t parseDatetime(const String& dt) {
    // 支持 "YYYY-MM-DD" 和 "YYYY-MM-DD HH:MM:SS"
    if (dt.size() >= 19 && dt[10] == ' ') {
        return FromStr(dt, "%Y-%m-%d %H:%M:%S");
    }
    return FromStr(dt, "%Y-%m-%d");
}

/// 获取目标频率的目录名
static String getFreqDir(BarFreq freq) {
    switch (freq) {
        case BarFreq::Min1:  return "1m";
        case BarFreq::Min5:  return "5m";
        case BarFreq::Min15: return "15m";
        case BarFreq::Min30: return "30m";
        case BarFreq::Hour1: return "1h";
        case BarFreq::Hour2: return "2h";
        case BarFreq::Hour4: return "4h";
        case BarFreq::Day:   return "1d";
        case BarFreq::Week:  return "1w";
        case BarFreq::Month: return "1M";
    }
    return "1d";
}

/// 检查指定频率的数据是否存在
static bool checkFreqExists(const String& base_dir, const String& rel_path, BarFreq freq) {
    String freq_dir = getFreqDir(freq);
    String path = base_dir + "/" + freq_dir + "/" + rel_path;
    return std::filesystem::exists(path);
}

ResampledData ResampleToFrequency(
    const Map<String, Vector<double>>& source_data,
    const Vector<String>& source_dates,
    BarFreq source_freq,
    BarFreq target_freq,
    const Vector<String>& fields)
{
    ResampledData result;

    if (source_freq == target_freq || source_dates.empty()) {
        result.data = source_data;
        result.dates = source_dates;
        return result;
    }

    // 仅支持分钟 → 日线的聚合
    if (target_freq != BarFreq::Day) {
        WARN("[ResampleToFrequency] Unsupported target frequency: {}", toString(target_freq));
        result.data = source_data;
        result.dates = source_dates;
        return result;
    }

    // 按日期分组聚合
    std::map<String, std::vector<size_t>> date_groups;  // "YYYY-MM-DD" → indices
    for (size_t i = 0; i < source_dates.size(); ++i) {
        String dt = source_dates[i];
        // 提取日期部分（前 10 字符）
        String date_key = dt.size() >= 10 ? dt.substr(0, 10) : dt;
        date_groups[date_key].push_back(i);
    }

    // 字段聚合规则
    // open: 第一条, high: max, low: min, close: 最后一条, volume/turnover: sum
    for (const auto& [date_key, indices] : date_groups) {
        result.dates.push_back(date_key);

        for (const auto& field : fields) {
            auto it = source_data.find(field);
            if (it == source_data.end()) continue;

            const auto& values = it->second;
            double val = 0.0;

            if (field == "open") {
                val = values[indices.front()];
            } else if (field == "high") {
                val = -1e308;
                for (size_t idx : indices) val = std::max(val, values[idx]);
            } else if (field == "low") {
                val = 1e308;
                for (size_t idx : indices) val = std::min(val, values[idx]);
            } else if (field == "close") {
                val = values[indices.back()];
            } else if (field == "volume" || field == "turnover") {
                val = 0.0;
                for (size_t idx : indices) val += values[idx];
            } else {
                val = values[indices.back()];  // 默认取最后一条
            }

            result.data[field].push_back(val);
        }
    }

    return result;
}

Map<String, Vector<double>> LoadHistoryDataWithFreq(
    const symbol_t& symbol,
    const Vector<String>& fields,
    const String& start_date,
    const String& end_date,
    BarFreq target_freq,
    AdjType adj,
    Vector<String>* out_dates,
    FillMethod fill)
{
    // === 新方法：从 DuckDB 获取数据 ===
    try {
        // 初始化 QuoteDB（如果尚未初始化）
        // 注意：正常路径应由 Server::InitDefault() 在启动时初始化
        // 此处为回退逻辑，尝试使用 "./data/quote" 路径
        auto& quoteDB = QuoteDB::instance();
        if (!quoteDB.isInitialized()) {
            String db_dir = "./data/quote";
            if (!quoteDB.init(db_dir, "quote.db")) {
                WARN("[LoadHistoryDataWithFreq] Failed to initialize QuoteDB at {}", db_dir);
                // goto fallback_csv;
            }
        }

        // 从 symbol_t 获取标准格式的 symbol 字符串（用于查询和日志）
        String symbol_str = get_symbol(symbol);

        // 判断资产类型：使用 is_etf() 函数（内部基于代码前缀判断）
        bool is_etf_flag = is_etf(symbol);
        String asset_type = is_etf_flag ? "etf" : "stock";

        // 构建表名
        String freq_str = toString(target_freq);
        String table = QuoteDB::tableName(asset_type, freq_str);

        // 检查表是否存在
        auto tables = quoteDB.listTables();
        bool table_exists = false;
        for (const auto& t : tables) {
            if (t == table) {
                table_exists = true;
                break;
            }
        }
        
        if (!table_exists) {
            WARN("[LoadHistoryDataWithFreq] Table {} not found in DuckDB", table);
            // goto fallback_csv;
        }

        // 查询数据（使用 symbol_t 编码，内部会重新转换为字符串）
        String start_time = start_date.empty() ? "" : start_date + " 00:00:00";
        String end_time = end_date.empty() ? "" : end_date + " 23:59:59";
        auto bars = quoteDB.query(table, symbol_str, start_time, end_time, 100000);

        if (bars.empty()) {
            WARN("[LoadHistoryDataWithFreq] No data found for {} in {}", symbol_str, table);
            // goto fallback_csv;
        }

        // 构建返回结果
        Map<String, Vector<double>> result;
        Vector<String> dates;
        dates.reserve(bars.size());

        // 初始化结果字段
        for (const auto& f : fields) {
            String lower_f = f;
            std::transform(lower_f.begin(), lower_f.end(), lower_f.begin(), ::tolower);
            result[lower_f] = Vector<double>();
            result[lower_f].reserve(bars.size());
        }

        // 填充数据
        for (const auto& bar : bars) {
            dates.push_back(bar.datetime);

            for (const auto& f : fields) {
                String lower_f = f;
                std::transform(lower_f.begin(), lower_f.end(), lower_f.begin(), ::tolower);

                double val = 0.0;
                if (lower_f == "open") {
                    val = (adj == AdjType::HFQ && bar.adj_open > 0) ? bar.adj_open : bar.open;
                } else if (lower_f == "close") {
                    val = (adj == AdjType::HFQ && bar.adj_close > 0) ? bar.adj_close : bar.close;
                } else if (lower_f == "high") {
                    val = (adj == AdjType::HFQ && bar.adj_high > 0) ? bar.adj_high : bar.high;
                } else if (lower_f == "low") {
                    val = (adj == AdjType::HFQ && bar.adj_low > 0) ? bar.adj_low : bar.low;
                } else if (lower_f == "volume") {
                    val = static_cast<double>(bar.volume);
                } else if (lower_f == "turnover") {
                    val = bar.turnover;
                }

                auto it = result.find(lower_f);
                if (it != result.end()) {
                    it->second.push_back(val);
                }
            }
        }

        if (out_dates) {
            *out_dates = std::move(dates);
        }

        return result;

    } catch (const std::exception& e) {
        WARN("[LoadHistoryDataWithFreq] DuckDB query failed: {}, falling back to CSV", e.what());
    }

    return {};
}

// ═══════════════════════════════════════════════════════════
//  DataUtil — DuckDB 数据管理共享工具
// ═══════════════════════════════════════════════════════════

namespace DataUtil {

String WriteTempCsv(const std::vector<String>& csv_lines,
                         const String& name) {
    auto tmp_dir = std::filesystem::temp_directory_path() / "quasarx_test";
    std::filesystem::create_directories(tmp_dir);
    auto tmp_path = tmp_dir / (name + ".csv");

    std::ofstream ofs(tmp_path.string());
    if (!ofs.is_open()) return "";

    for (const auto& line : csv_lines) {
        ofs << line << "\n";
    }
    ofs.close();
    return tmp_path.string();
}

void CleanupTempFile(const String& path) {
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

std::pair<bool, String> CleanupDBData(
    const String& table,
    const String& symbol,
    const DBCleanupOps& ops) {

    if (!table.empty() && !symbol.empty()) {
        if (ops.delete_symbol(table, symbol)) {
            return {true, fmt::format("Deleted symbol {} from {}", symbol, table)};
        }
        return {false, fmt::format("Failed to delete symbol {} from {}", symbol, table)};
    }

    if (!table.empty()) {
        if (ops.drop_table(table)) {
            return {true, fmt::format("Table {} dropped", table)};
        }
        return {false, fmt::format("Failed to drop table {}", table)};
    }

    if (!symbol.empty()) {
        auto tables = ops.list_tables();
        int deleted = 0;
        for (const auto& t : tables) {
            if (ops.delete_symbol(t, symbol)) deleted++;
        }
        return {true, fmt::format("Deleted symbol {} from {} tables", symbol, deleted)};
    }

    return {false, "At least one param required: table or symbol"};
}

} // namespace DataUtil

// ============================================================
// BaoStock 单标的下载 + 导入
// ============================================================

namespace {

/// 行情频率字符串 → 体检子类别
DataCheckCategory quote_category(const String& freq) {
    if (freq == "daily" || freq == "1d") return DataCheckCategory::Day;
    if (freq == "5m")  return DataCheckCategory::Min5;
    if (freq == "15m") return DataCheckCategory::Min15;
    if (freq == "30m") return DataCheckCategory::Min30;
    if (freq == "60m") return DataCheckCategory::Min60;
    return DataCheckCategory::Day;
}

}  // namespace

DownloadSymbolResult DownloadAndImportSymbol(
    const String& symbol,
    const String& freq,
    const String& start,
    const String& end,
    const String& interpreter,
    const String& quote_dir,
    bool overwrite)
{
    namespace fs = std::filesystem;
    DownloadSymbolResult result;

    // 从 symbol 推断 asset_type: sh.5xxxxx / sh.58xxxx → etf, 其余 → stock
    String asset_type = "stock";
    {
        auto dot = symbol.find('.');
        if (dot != String::npos && dot <= 3) {
            String code = symbol.substr(dot + 1);
            if (!code.empty() && (code[0] == '5')) {
                asset_type = "etf";
            }
        }
    }

    // 构建下载命令
    String cmd = interpreter + " tools/download_quote_bs.py "
                    + symbol + " " + quote_dir
                    + " --freq " + freq
                    + " --asset-type " + asset_type;
    if (!start.empty()) cmd += " --start " + start;
    if (!end.empty())   cmd += " --end "   + end;

    // 执行下载
    String output;
    bool ok = RunCommand(cmd, output);
    if (!ok) {
        WARN("[DownloadAndImport] {} download failed: {}", symbol, output);
        return result;  // download_ok=false
    }
    result.download_ok = true;

    // baostock 格式 → CODE.EXCHANGE (sh.510050 → 510050.SH)
    String csv_name = symbol;
    {
        auto dot = symbol.find('.');
        if (dot != String::npos && dot <= 3) {
            String market = symbol.substr(0, dot);
            String code = symbol.substr(dot + 1);
            std::transform(market.begin(), market.end(), market.begin(), ::toupper);
            csv_name = code + "." + market;
        }
    }

    String org_path = quote_dir + "/" + asset_type + "_org/" + freq + "/" + csv_name + ".csv";
    String hfq_path = quote_dir + "/" + asset_type + "_hfq/" + freq + "/" + csv_name + ".csv";

    const bool has_org = fs::exists(org_path);
    const bool has_hfq = fs::exists(hfq_path);

    if (!has_org && !has_hfq) {
        WARN("[DownloadAndImport] {} CSV not found after download", symbol);
        return result;  // download_ok=true, import_ok=false
    }

    // 不复权(org)才是原始价的唯一来源，缺一不可。
    // org 下载失败时不能把"后复权价"写进 close 列，
    // 宁可不导入这根 bar，也不能用后复权价冒充原始价。
    if (!has_org) {
        WARN("[DownloadAndImport] {} 不复权(org)数据缺失，跳过导入以免用后复权价污染原始价: "
             "org={} hfq={}", symbol, org_path, hfq_path);
        // 清掉残留的 hfq CSV：留着会被下一次运行误当成 org 顶替原始价
        if (has_hfq) fs::remove(hfq_path);
        result.download_ok = false;
        result.import_ok = false;
        return result;
    }

    // 从 CSV 文件获取行数
    {
        String count_path = org_path;
        std::ifstream ifs(count_path);
        if (ifs.is_open()) {
            int csv_rows = 0;
            String line;
            bool header_skipped = false;
            while (std::getline(ifs, line)) {
                if (line.empty()) continue;
                if (!header_skipped) { header_skipped = true; continue; }
                csv_rows++;
            }
            result.rows = csv_rows;
        }
    }

    // 入库前体检：行数、最新日期、公告日合法性。
    // 异常记下来，凌晨补下载时按这份清单重下。
    {
        CsvSpec spec;
        spec._type = DataCheckType::Quote;
        spec._category = quote_category(freq);
        spec._symbol = toInternalSymbol(csv_name);
        spec._path = org_path;
        RecordDataIssues(CheckCsvs({spec}));
    }

    // 导入 QuoteDB（未初始化则自动初始化）
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        INFO("[DownloadAndImport] Auto-initializing QuoteDB at {}/quote.db", quote_dir);
        if (!quoteDB.init(quote_dir, "quote.db")) {
            WARN("[DownloadAndImport] QuoteDB init failed, skipping import for {}", symbol);
            return result;
        }
    }

    // org 已保证存在（上面缺 org 直接跳过），hfq 缺失时退化为 org：
    // 此时 adj_* 暂时等于原始价，随后 validateAndReconcileAdj 的
    // recalcSymbolAdjPrices 会按 dividend 表重算复权列。
    String org_p = org_path;
    String hfq_p = has_hfq ? hfq_path : org_path;
    String table = QuoteDB::tableName(asset_type, freq);
    String internal_sym = toInternalSymbol(csv_name);

    auto t0 = std::chrono::steady_clock::now();
    int imported = quoteDB.importCsv(org_p, hfq_p, table, internal_sym, overwrite);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    INFO("[DownloadAndImport] {} import done: {} rows in {} ms", internal_sym, imported, ms);

    // 清理 CSV
    fs::remove(org_p);
    fs::remove(hfq_p);

    if (imported > 0) {
        result.imported = imported;
        result.import_ok = true;
    }

    return result;
}

// ============================================================
// 标的 → QuoteDB 查询
// ============================================================

UnderlyingQuoteInfo ResolveUnderlying(const String& underlying, const String& exchange,
                                      const String& product) {
    UnderlyingQuoteInfo info;
    if (underlying.empty()) {
        INFO("[ResolveUnderlying] underlying is empty, returning empty");
        return info;
    }

    // SSE: underlying 可能是产品名 ("50ETF") 而非 ETF 代码 ("510050")
    // 通过 product 参数做映射; 如果 underlying 本身是纯数字代码则直接用
    static const std::map<String, String> SSE_PRODUCT_TO_CODE = {
        {"50ETF",     "510050"},
        {"300ETF",    "510300"},
        {"500ETF",    "510500"},
        {"STAR50ETF", "588000"},
    };

    String code = underlying;
    if (exchange == "SSE" && !product.empty()) {
        auto it = SSE_PRODUCT_TO_CODE.find(product);
        if (it != SSE_PRODUCT_TO_CODE.end()) {
            code = it->second;
        }
    }

    // 构造 baostock 格式 symbol
    String prefix = (exchange == "SSE" || exchange == "CFFEX") ? "sh." : "sz.";
    info.symbol = prefix + code;

    // 自动判断 asset_type
    symbol_t sym = to_symbol(info.symbol);
    bool etf = is_etf(sym);
    String asset_type = etf ? "etf" : "stock";
    info.table = QuoteDB::tableName(asset_type, "daily");

    INFO("[ResolveUnderlying] underlying='{}' product='{}' exchange='{}' → code='{}' symbol='{}' is_etf={} table='{}'",
         underlying, product, exchange, code, info.symbol, etf, info.table);

    // 取最新收盘价
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        WARN("[ResolveUnderlying] QuoteDB NOT initialized!");
        return info;
    }

    info.latest_close = quoteDB.getLatestClose(info.table, info.symbol);
    INFO("[ResolveUnderlying] getLatestClose({}, {}) = {}",
         info.table, info.symbol, info.latest_close);
    return info;
}


// ============================================================
//  数据完整性体检（行情 / 两融 / 基本面）
//
// 体检对象是「下载好的 CSV」而不是库里的表：下载刚落盘时数据最新，
// 还没被导入，异常能在入库前就发现并记录下来。
// ============================================================

const char* check_category_name(DataCheckCategory c) {
    switch (c) {
        case DataCheckCategory::Day:         return "daily";
        case DataCheckCategory::Min5:        return "5m";
        case DataCheckCategory::Min15:       return "15m";
        case DataCheckCategory::Min30:       return "30m";
        case DataCheckCategory::Min60:       return "60m";
        case DataCheckCategory::MarginDaily: return "margin_daily";
        case DataCheckCategory::Profit:      return "profit";
        case DataCheckCategory::Operation:   return "operation";
        case DataCheckCategory::Growth:      return "growth";
        case DataCheckCategory::Balance:     return "balance";
        case DataCheckCategory::Cashflow:    return "cashflow";
        case DataCheckCategory::Dupont:      return "dupont";
    }
    return "unknown";
}

namespace {

// 数据类型 / 原因 的可读名字
const char* check_type_name(DataCheckType t) {
    switch (t) {
        case DataCheckType::Quote:       return "quote";
        case DataCheckType::Margin:      return "margin";
        case DataCheckType::Fundamental: return "fundamental";
    }
    return "unknown";
}

const char* check_reason_name(DataCheckReason r) {
    switch (r) {
        case DataCheckReason::Missing:      return "missing";
        case DataCheckReason::Stale:        return "stale";
        case DataCheckReason::Invalid:      return "invalid";
        case DataCheckReason::Insufficient: return "insufficient";
    }
    return "unknown";
}

/// 切分一行 CSV（去掉引号）
Vector<String> split_csv_line(const String& line) {
    Vector<String> cols;
    String cur;
    bool in_quote = false;
    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (c == ',' && !in_quote) {
            cols.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    cols.push_back(cur);
    return cols;
}

/// 取一行的日期：财务类是 "YYYY-MM-DD"，行情是 "YYYY-MM-DD HH:MM:SS" 或 Unix 时间戳
String row_date(DataCheckType type, const String& raw) {
    if (raw.empty()) return "";
    if (type == DataCheckType::Quote && raw.find('-') == String::npos) {
        try {
            return ToString(static_cast<time_t>(std::stoll(raw)), "%Y-%m-%d");
        } catch (...) {
            return "";
        }
    }
    return raw.substr(0, 10);
}

/// 该类别的 CSV 有没有公告日列（财务类有，行情没有）
bool has_pub_date_column(DataCheckType type) {
    return type == DataCheckType::Fundamental || type == DataCheckType::Margin;
}

/// 一个 CSV 的体检中间结果
struct CsvInfo {
    DataCheckType     _type = DataCheckType::Quote;
    DataCheckCategory _category = DataCheckCategory::Day;
    String            _symbol;
    int               _rows = 0;
    String            _last_date;      ///< 文件里最新的日期
    String            _last_pub_date;  ///< 最新一行的公告日
};

/// 已记录的异常（下载链路统一入口，凌晨补下载时取用）
std::mutex g_issueMtx;
Vector<DataCheckIssue> g_issues;

}  // namespace

String DataCheckIssue::toLine() const {
    String line = fmt::format("{}/{} {} {}", check_type_name(_type),
                               check_category_name(_category),
                               _symbol.empty() ? "-" : _symbol, _date);
    return line + " " + check_reason_name(_reason);
}

const char* DataCheckIssue::tool() const {
    switch (_type) {
        case DataCheckType::Quote:       return "download_quote_bs.py";
        case DataCheckType::Margin:      return "fetch_margin_data.py";
        case DataCheckType::Fundamental: return "fetch_finance_data.py";
    }
    return "unknown";
}

Vector<DataCheckIssue> CheckCsvs(const Vector<CsvSpec>& specs, int lag_days) {
    Vector<DataCheckIssue> issues;
    Vector<CsvInfo> infos;
    String today = ToString(Now(), "%Y-%m-%d");

    for (const auto& spec : specs) {
        CsvInfo info;
        info._type = spec._type;
        info._category = spec._category;
        info._symbol = spec._symbol;

        DataCheckIssue issue;
        issue._type = spec._type;
        issue._category = spec._category;
        issue._symbol = spec._symbol;

        std::ifstream ifs(spec._path);
        if (!ifs.is_open()) {
            issue._reason = DataCheckReason::Missing;
            issues.push_back(issue);
            continue;
        }

        String header;
        if (!std::getline(ifs, header)) {
            issue._reason = DataCheckReason::Missing;
            issues.push_back(issue);
            continue;
        }
        // 去 BOM
        if (header.size() >= 3 && header[0] == '\xEF' && header[1] == '\xBB' &&
            header[2] == '\xBF') {
            header = header.substr(3);
        }

        int date_idx = -1;
        int pub_idx = -1;
        auto cols = split_csv_line(header);
        for (size_t i = 0; i < cols.size(); i++) {
            if (cols[i] == "statDate" || cols[i] == "datetime") date_idx = static_cast<int>(i);
            if (cols[i] == "pubDate") pub_idx = static_cast<int>(i);
        }
        if (date_idx < 0) {
            WARN("[DataCheck] {} 没有日期列，跳过", spec._path);
            continue;
        }

        // 逐行扫：统计行数、找最新日期、记下第一行非法数据
        bool invalid_reported = false;
        String line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            auto row = split_csv_line(line);
            if (date_idx >= static_cast<int>(row.size())) continue;
            String date = row_date(spec._type, row[date_idx]);
            if (date.empty()) continue;

            info._rows++;
            if (date > info._last_date) {
                info._last_date = date;
                info._last_pub_date =
                    (pub_idx >= 0 && pub_idx < static_cast<int>(row.size())) ? row[pub_idx] : "";
            }

            // 公告日缺失（节点永远看不到这条）/ 早于报告期（不可能）/ 报告期在未来
            if (has_pub_date_column(spec._type) && !invalid_reported) {
                String pub = (pub_idx >= 0 && pub_idx < static_cast<int>(row.size()))
                                 ? row[pub_idx] : "";
                if (pub.empty() || pub < date || date > today) {
                    issue._date = date;
                    issue._reason = DataCheckReason::Invalid;
                    issues.push_back(issue);
                    invalid_reported = true;
                }
            }
        }

        if (info._rows == 0) {
            issue._reason = DataCheckReason::Missing;
            issues.push_back(issue);
        } else if (info._rows < spec._min_rows) {
            issue._date = info._last_date;
            issue._reason = DataCheckReason::Insufficient;
            issues.push_back(issue);
        }

        infos.push_back(info);
    }

    // 同一类目里各标的的最新日期应当接近：明显掉队的那个 = 那次下载漏了
    Map<String, String> reference;
    for (const auto& info : infos) {
        String key = check_key(info._type, info._category);
        if (info._last_date > reference[key]) reference[key] = info._last_date;
    }
    for (const auto& info : infos) {
        const String& ref = reference[check_key(info._type, info._category)];
        if (ref.empty() || info._last_date.empty()) continue;
        if (DaysBetween(info._last_date, ref) <= lag_days) continue;

        DataCheckIssue issue;
        issue._type = info._type;
        issue._category = info._category;
        issue._symbol = info._symbol;
        issue._date = info._last_date;
        issue._reason = DataCheckReason::Stale;
        issues.push_back(issue);
    }

    return issues;
}

void RecordDataIssues(const Vector<DataCheckIssue>& issues) {
    if (issues.empty()) return;
    std::lock_guard<std::mutex> lock(g_issueMtx);
    for (const auto& issue : issues) {
        g_issues.push_back(issue);
        WARN("[DataCheck] 异常: {}  (补数据用 {})", issue.toLine(), issue.tool());
    }
}

Vector<DataCheckIssue> TakeDataIssues() {
    std::lock_guard<std::mutex> lock(g_issueMtx);
    Vector<DataCheckIssue> result;
    result.swap(g_issues);
    return result;
}

// ============================================================
//  基本面数据下载工具
// ============================================================

namespace {

// 从脚本输出里抠出 JSON manifest（参考 server.cpp 中的 ExtractJsonManifest）
bool extract_manifest(const String& output, nlohmann::json& out) {
    auto begin = output.find('{');
    auto end = output.rfind('}');
    if (begin == String::npos || end == String::npos || end <= begin) {
        return false;
    }
    try {
        out = nlohmann::json::parse(output.substr(begin, end - begin + 1));
        return true;
    } catch (const std::exception& e) {
        WARN("[extract_manifest] parse failed: {}", e.what());
        return false;
    }
}

}  // namespace

bool DownloadFundamentalCsv(
    const String& symbol,
    const String& category,
    const String& start_date,
    const String& end_date,
    const String& interpreter,
    const String& finance_dir,
    String& out_csv_path)
{
    namespace fs = std::filesystem;

    // CSV 目录用绝对路径
    String csv_dir = fs::absolute(finance_dir + "/csv").string();
    std::error_code mk_ec;
    fs::create_directories(csv_dir, mk_ec);

    // 构建命令（参考 server.cpp 中的 DownloadMarginData）
    String cmd = "cd ../tools && " + interpreter + " fetch_finance_data.py"
                 " --code " + symbol +
                 " --category " + category +
                 " --download" +
                 " --data-dir " + csv_dir;
    if (!start_date.empty()) cmd += " --start " + start_date;
    if (!end_date.empty()) cmd += " --end " + end_date;

    INFO("[DownloadFundamentalCsv] {} {} cmd: {}", symbol, category, cmd);

    // 执行下载
    String output;
    bool ok = RunCommand(cmd, output);
    if (!ok) {
        WARN("[DownloadFundamentalCsv] {} {} download failed: {}", symbol, category, output.substr(0, 200));
        return false;
    }

    // 从 manifest 提取 CSV 路径
    nlohmann::json manifest;
    if (!extract_manifest(output, manifest)) {
        WARN("[DownloadFundamentalCsv] {} {} manifest not found", symbol, category);
        return false;
    }

    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        WARN("[DownloadFundamentalCsv] {} {} manifest.files not found", symbol, category);
        return false;
    }

    // 找到对应类目的 CSV 路径
    for (const auto& file : manifest["files"]) {
        if (file.value("category", "") == category) {
            String path = file.value("path", "");
            if (!path.empty() && fs::exists(path)) {
                out_csv_path = path;
                return true;
            }
        }
    }

    return false;
}

bool DownloadMarginCsv(
    const String& date,
    const String& interpreter,
    const String& finance_dir,
    String& out_csv_path)
{
    namespace fs = std::filesystem;

    // CSV 目录用绝对路径
    String margin_dir = fs::absolute(finance_dir + "/margin").string();
    std::error_code mk_ec;
    fs::create_directories(margin_dir, mk_ec);

    // 构建命令（参考 server.cpp 中的 DownloadMarginData）
    String cmd = "cd ../tools && " + interpreter + " fetch_margin_data.py";
    if (!date.empty()) {
        cmd += " --date " + date;
    }
    cmd += " --out-dir " + margin_dir;

    INFO("[DownloadMarginCsv] cmd: {}", cmd);

    // 执行下载
    String output;
    bool ok = RunCommand(cmd, output);
    if (!ok) {
        WARN("[DownloadMarginCsv] download failed: {}", output.substr(0, 200));
        return false;
    }

    // 从 manifest 提取 CSV 路径
    nlohmann::json manifest;
    if (!extract_manifest(output, manifest)) {
        WARN("[DownloadMarginCsv] manifest not found");
        return false;
    }

    // manifest["files"] 是 CSV 文件列表
    if (manifest.contains("files") && manifest["files"].is_array() && !manifest["files"].empty()) {
        String path = manifest["files"][0].get<String>();
        if (!path.empty() && fs::exists(path)) {
            out_csv_path = path;
            return true;
        }
    }

    // 兼容老版本：manifest["file"] 单数
    if (manifest.contains("file")) {
        String path = manifest["file"].get<String>();
        if (!path.empty() && fs::exists(path)) {
            out_csv_path = path;
            return true;
        }
    }

    return false;
}

