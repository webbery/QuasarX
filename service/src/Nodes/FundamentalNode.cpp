#include "Nodes/FundamentalNode.h"
#include "Util/FinanceDB.h"
#include "Util/log.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include <cmath>
#include <algorithm>
#include <limits>

FundamentalNode::FundamentalNode(Server* server) : _server(server) {}

const nlohmann::json FundamentalNode::getParams() {
    return nlohmann::json::parse(R"({
        "categories": {"type": "text", "label": "数据类别", "default": "", "description": "逗号分隔的类别列表（profit/growth/balance/operation/cashflow/dupont），空=自动推断"},
        "fields": {"type": "text", "label": "字段", "default": "", "description": "逗号分隔的字段名，空=该类别全部字段"},
        "lookback": {"type": "number", "label": "历史期数", "default": 8, "description": "读取历史财报期数（默认 8 = 2 年季报）"},
        "alignMode": {"type": "select", "label": "时间对齐", "default": "latest", "options": ["latest", "interpolate"], "description": "latest=前向填充，interpolate=线性插值"}
    })");
}

bool FundamentalNode::Init(const nlohmann::json& config) {
    auto params = config.value("params", nlohmann::json::object());

    // 解析参数
    String categoriesStr = params.value("categories", params.value("数据类别", ""));
    String fieldsStr = params.value("fields", params.value("字段", ""));
    _lookback = params.value("lookback", params.value("历史期数", 8));
    String mode = params.value("alignMode", params.value("时间对齐", "latest"));
    _alignMode = AlignMode::Latest;

    // 解析类别列表
    if (!categoriesStr.empty()) {
        Vector<String> cats;
        split(categoriesStr, cats, ",");
        for (auto& cat : cats) {
            if (!cat.empty()) {
                _categories.insert(cat);
            }
        }
    }

    // 解析字段列表
    if (!fieldsStr.empty()) {
        split(fieldsStr, _fields, ",");
        // 移除空字符串
        _fields.erase(std::remove_if(_fields.begin(), _fields.end(),
                     [](const String& s) { return s.empty(); }), _fields.end());
    }

    // 从上游连接发现 symbols
    _symbols = discoverUpstreamSymbols();
    if (_symbols.empty()) {
        WARN("[FundamentalNode:{}] 未发现上游 symbol", _id);
        return false;
    }

    DEBUG_INFO("[FundamentalNode:{}] Init: symbols={}, categories={}, fields={}, lookback={}, alignMode={}",
         _id, _symbols.size(), categoriesStr.empty() ? "(auto)" : categoriesStr,
         fieldsStr.empty() ? "(all)" : fieldsStr, _lookback, (int)_alignMode);

    // 解析输出字段
    resolveOutputFields();

    // 加载基本面数据
    loadFinanceData();

    return true;
}

void FundamentalNode::resolveOutputFields() {
    auto& financeDB = FinanceDB::instance();
    _resolvedOutputs.clear();

    // 1. 发现所有后继节点
    auto downstreamNodes = discoverDownstreamNodes();
    INFO("[FundamentalNode:{}] 发现 {} 个后继节点", _id, downstreamNodes.size());

    // 2. 收集所有后继节点需要的特征
    Set<String> requiredFeatures;
    for (auto* node : downstreamNodes) {
        auto features = node->getRequiredFeatures();
        requiredFeatures.insert(features.begin(), features.end());
    }
    INFO("[FundamentalNode:{}] 后继节点需要 {} 个特征", _id, requiredFeatures.size());

    // 3. 从特征名中提取基本面相关的 category.field
    //    基本面特征格式: "category.field" (如 "profit.roe_avg")
    //    如果用户显式指定了 categories/fields，则使用用户指定；否则从下游推断
    if (_categories.empty() && _fields.empty() && !requiredFeatures.empty()) {
        // 动态推断模式：从下游节点需要的特征中提取
        for (const auto& feature : requiredFeatures) {
            auto dotPos = feature.find('.');
            if (dotPos != String::npos) {
                String category = feature.substr(0, dotPos);
                String field = feature.substr(dotPos + 1);

                // 检查是否是有效的基本面类别
                if (financeDB.isValidCategory(category)) {
                    const auto& fieldDefs = financeDB.categoryFields(category);
                    // 验证字段是否存在
                    bool fieldExists = false;
                    for (const auto& [csvCol, dbCol] : fieldDefs) {
                        if (dbCol == field) {
                            fieldExists = true;
                            break;
                        }
                    }
                    if (fieldExists) {
                        _resolvedOutputs[category].push_back(field);
                        INFO("[FundamentalNode:{}] 动态推断需要: {}.{}", _id, category, field);
                    } else {
                        WARN("[FundamentalNode:{}] 字段 {}.{} 不存在", _id, category, field);
                    }
                }
            }
        }
    } else {
        // 用户显式指定模式：使用配置的 categories 和 fields
        if (_categories.empty()) {
            _categories = {"profit", "growth", "balance", "operation", "cashflow", "dupont"};
        }

        for (const auto& category : _categories) {
            if (!financeDB.isValidCategory(category)) {
                WARN("[FundamentalNode:{}] 未知类别: {}", _id, category);
                continue;
            }

            const auto& fieldDefs = financeDB.categoryFields(category);
            Vector<String> fields;

            if (_fields.empty()) {
                // 使用该类别全部字段
                for (const auto& [csvCol, dbCol] : fieldDefs) {
                    fields.push_back(dbCol);
                }
            } else {
                // 只使用指定的字段（需要检查是否存在）
                Set<String> validFields;
                for (const auto& [csvCol, dbCol] : fieldDefs) {
                    validFields.insert(dbCol);
                }
                for (const auto& f : _fields) {
                    if (validFields.count(f) > 0) {
                        fields.push_back(f);
                    } else {
                        WARN("[FundamentalNode:{}] 字段 {} 在类别 {} 中不存在", _id, f, category);
                    }
                }
            }

            if (!fields.empty()) {
                _resolvedOutputs[category] = fields;
            }
        }
    }

    INFO("[FundamentalNode:{}] 解析输出: {} 个类别, {} 个字段",
         _id, _resolvedOutputs.size(),
         std::accumulate(_resolvedOutputs.begin(), _resolvedOutputs.end(), 0,
                         [](int acc, const auto& p) { return acc + p.second.size(); }));
}

void FundamentalNode::loadFinanceData() {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        String db_path = _server->GetConfig().GetDatabasePath();
        if (!financeDB.init(db_path + "/finance", "finance.db")) {
            WARN("[FundamentalNode:{}] 无法初始化 FinanceDB", _id);
            return;
        }
    }

    _cachedData.clear();

    for (const auto& symbol : _symbols) {
        String symbolStr = get_symbol(symbol);

        for (const auto& [category, fields] : _resolvedOutputs) {
            // 查询该 symbol 和 category 的数据
            auto result = financeDB.query(category, symbolStr, "", "", _lookback * 10);

            if (result.contains("error")) {
                WARN("[FundamentalNode:{}] 查询失败: {} {} - {}",
                     _id, symbolStr, category, result["error"].get<String>());
                continue;
            }

            if (!result.contains("data") || result["data"].empty()) {
                continue;
            }

            FinanceData data;
            const auto& dataArray = result["data"];

            for (const auto& row : dataArray) {
                // 解析日期
                String statDateStr = row.value("stat_date", "");
                String pubDateStr = row.value("pub_date", "");

                time_t statDate = FromStr(statDateStr);
                time_t pubDate = FromStr(pubDateStr);

                if (statDate == 0) continue;

                data.statDates.push_back(statDate);
                data.pubDates.push_back(pubDate);

                // 解析字段值
                for (const auto& field : fields) {
                    if (!data.values.count(field)) {
                        data.values[field] = Vector<double>();
                    }

                    if (row.contains(field) && !row[field].is_null()) {
                        data.values[field].push_back(row[field].get<double>());
                    } else {
                        data.values[field].push_back(std::numeric_limits<double>::quiet_NaN());
                    }
                }
            }

            if (!data.statDates.empty()) {
                // 构建区间树索引：按 pub_date 排序，用于 O(log n) 查找
                for (int i = 0; i < static_cast<int>(data.pubDates.size()); ++i) {
                    data.pubDateIndex[data.pubDates[i]] = i;
                }
                _cachedData[symbol][category] = std::move(data);
            }
        }
    }

    INFO("[FundamentalNode:{}] 加载缓存数据: {} 个标的", _id, _cachedData.size());
}

NodeProcessResult FundamentalNode::Process(const String& strategy, DataContext& context) {
    const auto& times = context.GetTime();
    if (times.empty()) {
        return NodeProcessResult::Skip;
    }

    time_t currentTime = times.back();

    for (const auto& symbol : _symbols) {
        String symbolStr = get_symbol(symbol);
        Map<String, double> currentValues;

        // 获取当前日期之前最新已发布的数据
        for (const auto& [category, data] : _cachedData[symbol]) {
            if (data.statDates.empty() || data.pubDateIndex.empty()) continue;

            // O(log n) 查找：找 pub_date <= currentTime 的最新记录
            // upper_bound 返回第一个 > currentTime 的迭代器，前一个就是 <= currentTime 的最大值
            auto it = data.pubDateIndex.upper_bound(currentTime);
            int latestIdx = -1;

            if (it != data.pubDateIndex.begin()) {
                --it;
                latestIdx = it->second;
            }

            if (latestIdx >= 0) {
                // 提取该期的字段值
                for (const auto& field : _resolvedOutputs[category]) {
                    String key = symbolStr + "." + category + "." + field;
                    double value = std::numeric_limits<double>::quiet_NaN();

                    auto fieldIt = data.values.find(field);
                    if (fieldIt != data.values.end() &&
                        latestIdx < static_cast<int>(fieldIt->second.size())) {
                        value = fieldIt->second[latestIdx];
                    }

                    // 写入 context
                    if (context.exist(key)) {
                        context.add(key, value);
                    } else {
                        context.set(key, Vector<double>{value});
                    }

                    currentValues[key] = value;
                }
            } else {
                // 没有已发布的数据，写 NaN
                for (const auto& field : _resolvedOutputs[category]) {
                    String key = symbolStr + "." + category + "." + field;
                    double nanValue = std::numeric_limits<double>::quiet_NaN();

                    if (context.exist(key)) {
                        context.add(key, nanValue);
                    } else {
                        context.set(key, Vector<double>{nanValue});
                    }

                    currentValues[key] = nanValue;
                }
            }
        }

        // 更新最新值缓存（用于前向填充）
        _latestValues[symbol] = currentValues;
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> FundamentalNode::out_elements() {
    Map<String, ArgType> elements;

    for (const auto& symbol : _symbols) {
        String symbolStr = get_symbol(symbol);

        for (const auto& [category, fields] : _resolvedOutputs) {
            for (const auto& field : fields) {
                String key = symbolStr + "." + category + "." + field;
                elements[key] = ArgType::Double_TimeSeries;
            }
        }
    }

    return elements;
}
