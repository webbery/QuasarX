#include "Nodes/MacroNode.h"
#include "Util/log.h"
#include "server.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

MacroInputNode::MacroInputNode(Server* server) : _server(server) {
}

const nlohmann::json MacroInputNode::getParams() {
    return nlohmann::json::parse(R"({
        "country": {"type": "select", "label": "国家", "default": "usa", "options": ["china", "usa", "global"]},
        "indicator": {"type": "select", "label": "指标", "default": "sofr", "options": ["cpi", "ppi", "gdp", "pmi", "m2", "social_financing", "unemployment", "trade", "interest_rate", "retail_sales", "industrial_production", "fixed_asset_investment", "consumer_confidence", "housing_starts", "nonfarm", "sofr", "treasury_yield", "treasury_spread", "fx_rate"]},
        "seriesId": {"type": "text", "label": "Series ID", "default": "", "description": "多 series 指标时指定具体 ID（如 DGS10、DEXCHUS），留空使用默认值"},
        "fillMethod": {"type": "select", "label": "填充方式", "default": "forward", "options": ["forward", "linear", "none"], "description": "低频数据对齐到日频时的填充方式。国内数据默认前向填充，美国数据默认 T-1 滞后"}
    })");
}

bool MacroInputNode::Init(const nlohmann::json& config) {
    if (!QNode::Init(config)) return false;

    auto params = config.value("params", nlohmann::json::object());

    // 解析参数（支持英文 key 和中文 label）
    _country = params.value("country", params.value("国家", "usa"));
    _indicator = params.value("indicator", params.value("指标", "sofr"));
    _seriesId = params.value("seriesId", params.value("Series ID", ""));
    _fillMethod = params.value("fillMethod", params.value("填充方式", "forward"));

    // 美国数据默认使用 T-1 滞后
    if (_country == "usa" && _fillMethod == "forward") {
        _fillMethod = "lag_t1";
    }

    INFO("[MacroInputNode:{}] Init: country={}, indicator={}, seriesId={}, fillMethod={}",
         _id, _country, _indicator, _seriesId, _fillMethod);

    return true;
}

void MacroInputNode::Prepare(const String& strategy, DataContext& context) {
    QNode::Prepare(strategy, context);

    if (!loadMacroData()) {
        WARN("[MacroInputNode:{}] 加载宏观数据失败: {}/{}", _id, _country, _indicator);
    }
}

bool MacroInputNode::loadMacroData() {
    auto db_path = _server->GetConfig().GetDatabasePath();
    String cache_path = db_path + "/macro/" + _country + "/" + _indicator + ".json";

    if (!std::filesystem::exists(cache_path)) {
        WARN("[MacroInputNode:{}] 缓存文件不存在: {}", _id, cache_path);
        return false;
    }

    try {
        std::ifstream ifs(cache_path);
        if (!ifs.is_open()) {
            WARN("[MacroInputNode:{}] 无法打开缓存文件: {}", _id, cache_path);
            return false;
        }

        nlohmann::json data;
        ifs >> data;
        ifs.close();

        if (!data.contains("data") || !data["data"].is_array()) {
            WARN("[MacroInputNode:{}] 缓存文件格式错误: 缺少 data 数组", _id);
            return false;
        }

        _macroData.clear();
        for (const auto& item : data["data"]) {
            if (!item.contains("date") || !item.contains("value")) continue;

            String date_str = item["date"].get<String>();
            double value = item["value"].get<double>();

            // 如果指定了 seriesId，只加载匹配的 series
            if (!_seriesId.empty()) {
                String item_series = item.value("series_id", "");
                if (item_series != _seriesId) continue;
            }

            _macroData.emplace_back(date_str, value);
        }

        // 按日期排序
        std::sort(_macroData.begin(), _macroData.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        INFO("[MacroInputNode:{}] 加载 {} 条宏观数据: {}/{}",
             _id, _macroData.size(), _country, _indicator);

        return !_macroData.empty();
    }
    catch (const std::exception& e) {
        WARN("[MacroInputNode:{}] 解析缓存文件失败: {}", _id, e.what());
        return false;
    }
}

NodeProcessResult MacroInputNode::Process(const String& strategy, DataContext& context) {
    // 获取当前时间
    const auto& times = context.GetTime();
    if (times.empty()) {
        return NodeProcessResult::Skip;
    }

    time_t current_time = times.back();

    // 将 time_t 转换为日期字符串 (YYYY-MM-DD)
    std::time_t t = current_time;
    std::tm tm = *std::localtime(&t);
    char date_buf[11];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm);
    String current_date(date_buf);

    // 根据填充方式获取值
    double value = std::numeric_limits<double>::quiet_NaN();

    if (_fillMethod == "forward") {
        value = forwardFill(current_date);
    } else if (_fillMethod == "lag_t1") {
        value = lagT1(current_date);
    } else if (_fillMethod == "linear") {
        value = linearInterp(current_date);
    } else {
        // none: 只在有数据的日期输出值
        for (const auto& [date, val] : _macroData) {
            if (date == current_date) {
                value = val;
                break;
            }
        }
    }

    // 写入 context
    String output_key = _indicator;
    if (!_seriesId.empty()) {
        output_key = _indicator + "_" + _seriesId;
    }

    if (context.exist(output_key)) {
        context.add(output_key, value);
    } else {
        context.add(output_key, Vector<double>{value});
    }

    return NodeProcessResult::Success;
}

double MacroInputNode::forwardFill(const String& targetDate) const {
    // 找到 targetDate 及之前最近的值
    for (auto it = _macroData.rbegin(); it != _macroData.rend(); ++it) {
        if (it->first <= targetDate) {
            return it->second;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double MacroInputNode::lagT1(const String& targetDate) const {
    // T-1 滞后：找到 targetDate 之前的值（不包括 targetDate 当天）
    for (auto it = _macroData.rbegin(); it != _macroData.rend(); ++it) {
        if (it->first < targetDate) {
            return it->second;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double MacroInputNode::linearInterp(const String& targetDate) const {
    // 找到 targetDate 前后的两个点
    String prev_date, next_date;
    double prev_value = 0, next_value = 0;
    bool found_prev = false, found_next = false;

    for (const auto& [date, value] : _macroData) {
        if (date <= targetDate) {
            prev_date = date;
            prev_value = value;
            found_prev = true;
        } else if (date > targetDate && !found_next) {
            next_date = date;
            next_value = value;
            found_next = true;
            break;
        }
    }

    // 如果只有前点，使用前向填充
    if (found_prev && !found_next) {
        return prev_value;
    }

    // 如果只有后点，使用后向填充
    if (!found_prev && found_next) {
        return next_value;
    }

    // 如果都没有，返回 NaN
    if (!found_prev && !found_next) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // 线性插值
    // 将日期转换为天数（简化处理）
    auto dateToDays = [](const String& date) -> int {
        int year, month, day;
        if (sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return 0;
        return year * 365 + month * 30 + day;
    };

    int target_days = dateToDays(targetDate);
    int prev_days = dateToDays(prev_date);
    int next_days = dateToDays(next_date);

    if (next_days == prev_days) {
        return prev_value;
    }

    double ratio = (double)(target_days - prev_days) / (next_days - prev_days);
    return prev_value + ratio * (next_value - prev_value);
}

Map<String, ArgType> MacroInputNode::out_elements() {
    Map<String, ArgType> elements;
    String output_key = _indicator;
    if (!_seriesId.empty()) {
        output_key = _indicator + "_" + _seriesId;
    }
    elements[output_key] = ArgType::Double_TimeSeries;
    return elements;
}
