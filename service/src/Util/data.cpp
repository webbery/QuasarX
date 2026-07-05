#include "Util/data.h"
#include "Util/QuoteDB.h"
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
    std::string line;
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
    std::string base_dir = "./data";

    // 规范化 symbol（统一小写）
    std::string normalized = symbol;
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
        std::string code = normalized.substr(0, dot_pos);
        std::string exchange = normalized.substr(dot_pos + 1);

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
            std::string code = etf_code.substr(0, dot_pos);
            std::string exchange = etf_code.substr(dot_pos + 1);
            String etf_formatted = exchange + "." + code;

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
            std::string db_dir = "./data/quote";
            if (!quoteDB.init(db_dir)) {
                WARN("[LoadHistoryDataWithFreq] Failed to initialize QuoteDB at {}", db_dir);
                goto fallback_csv;
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
        
        for (const auto& t : tables) {
            WARN("  - {}", t);
        }
        
        if (!table_exists) {
            WARN("[LoadHistoryDataWithFreq] Table {} not found in DuckDB", table);
            goto fallback_csv;
        }

        // 查询数据（使用 symbol_t 编码，内部会重新转换为字符串）
        String start_time = start_date.empty() ? "" : start_date + " 00:00:00";
        String end_time = end_date.empty() ? "" : end_date + " 23:59:59";
        auto bars = quoteDB.query(table, symbol_str, start_time, end_time, 100000);

        if (bars.empty()) {
            WARN("[LoadHistoryDataWithFreq] No data found for {} in {}", symbol_str, table);
            goto fallback_csv;
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

        INFO("[LoadHistoryDataWithFreq] Loaded {} bars from DuckDB for {} ({})",
             bars.size(), symbol_str, table);
        return result;

    } catch (const std::exception& e) {
        WARN("[LoadHistoryDataWithFreq] DuckDB query failed: {}, falling back to CSV", e.what());
    }

fallback_csv:
    // === 原 CSV 方法（已注释，保留作为回退） ===
    /*
    const String base_dir = "./data";

    // 规范化 symbol
    String normalized = symbol;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

    // 自动判断资产类型
    const bool is_etf = is_etf_symbol(normalized);

    // 静态路径表解析子目录（零开销）
    const String subdir = resolveSubdir(is_etf, adj);

    // 构建相对路径
    String rel_path;
    if (symbol.find('.') == String::npos) {
        rel_path = "sz." + symbol + ".csv";
        if (!std::filesystem::exists(base_dir + "/" + subdir + "/" + rel_path)) {
            rel_path = "sh." + symbol + ".csv";
        }
    } else {
        auto dot_pos = normalized.find('.');
        std::string code = normalized.substr(0, dot_pos);
        std::string exchange = normalized.substr(dot_pos + 1);
        rel_path = exchange + "." + code + ".csv";
    }

    // 频率搜索优先级（从目标频率到更低频率）
    static constexpr BarFreq freq_order[] = {
        BarFreq::Min1, BarFreq::Min5, BarFreq::Min15, BarFreq::Min30,
        BarFreq::Hour1, BarFreq::Hour2, BarFreq::Hour4,
        BarFreq::Day, BarFreq::Week, BarFreq::Month
    };
    constexpr int kFreqCount = sizeof(freq_order) / sizeof(freq_order[0]);

    // 1. 优先尝试加载目标频率
    const String target_dir = getFreqDir(target_freq);
    const String target_path = base_dir + "/" + subdir + "/" + target_dir + "/" + rel_path;

    auto data = loadCsvDataFromPath(target_path, fields, start_date, end_date, out_dates);
    if (!data.empty()) {
        return data;
    }

    // 2. 遍历所有更低频率并聚合到目标频率
    for (int i = 0; i < kFreqCount; ++i) {
        const BarFreq src_freq = freq_order[i];
        if (src_freq >= target_freq) break;  // 只尝试更低频率（数值更小）

        const String src_dir = getFreqDir(src_freq);
        const String src_path = base_dir + "/" + subdir + "/" + src_dir + "/" + rel_path;

        if (!std::filesystem::exists(src_path)) continue;

        Vector<String> src_dates;
        auto src_data = loadCsvDataFromPath(src_path, fields, start_date, end_date, &src_dates);
        if (src_data.empty()) continue;

        INFO("[LoadHistoryDataWithFreq] Resampling {} → {} for {}",
             toString(src_freq), toString(target_freq), symbol);
        auto resampled = ResampleToFrequency(src_data, src_dates, src_freq, target_freq, fields);
        if (out_dates) *out_dates = std::move(resampled.dates);
        return std::move(resampled.data);
    }

    // 3. 回退：尝试顶层目录中的默认文件（无频率子目录）
    const String default_path = base_dir + "/" + subdir + "/" + rel_path;
    if (std::filesystem::exists(default_path)) {
        return loadCsvDataFromPath(default_path, fields, start_date, end_date, out_dates);
    }
    */

    return {};
}
