#pragma once
#include "StrategyNode.h"
#include "std_header.h"

/**
 * 基本面数据节点
 *
 * 从 FinanceDB 读取标的基本面数据（盈利/成长/偿债/营运/现金流/杜邦），
 * 输出 per-symbol 基本面指标到 DataContext。
 *
 * 支持多类别：根据下游节点实际使用的特征动态加载所需类别。
 *
 * 参数：
 *   categories  — 逗号分隔的类别列表（如 "profit,growth"），空表示自动推断
 *   fields      — 逗号分隔的字段名（如 "roe_avg,np_margin"），空表示该类别全部字段
 *   lookback    — 读取历史期数（默认 8 = 2 年季报）
 *   alignMode   — 时间对齐模式：latest（前向填充）/ interpolate（线性插值）
 *
 * 输出（带 symbol 前缀）：
 *   {symbol}.{category}.{field}
 *   例如：sz.000001.profit.roe_avg, sz.000001.growth.yoy_ni
 *
 * 时间对齐：
 *   - latest: 每个 bar 使用最新已发布的财报数据（用 pub_date 判断，避免未来信息）
 *   - interpolate: 在两个财报点之间线性插值
 *
 * Process 始终返回 Success，有新财报时写新值，否则前向填充。
 */
class FundamentalNode : public QNode {
public:
    RegistClassName(FundamentalNode);
    static const nlohmann::json getParams();

    FundamentalNode(Server* server);
    ~FundamentalNode() override = default;

    bool Init(const nlohmann::json& config) override;
    NodeProcessResult Process(const String& strategy, DataContext& context) override;
    Map<String, ArgType> out_elements() override;

    enum class AlignMode{
        Latest,
        Interplot,
    };
private:
    Server* _server;

    // 配置参数
    Set<String> _categories;           // 要加载的类别集合
    Vector<String> _fields;            // 具体字段列表（空=全部）
    int _lookback = 8;
    AlignMode _alignMode = AlignMode::Latest;

    // 从上游解析的 symbol 集合
    Set<symbol_t> _symbols;

    // 解析后的输出结构：category -> [field1, field2, ...]
    Map<String, Vector<String>> _resolvedOutputs;

    // 缓存的基本面数据：symbol -> category -> [row1, row2, ...]
    // 按 stat_date 排序
    struct FinanceData {
        Vector<time_t> statDates;      // 报告期
        Vector<time_t> pubDates;       // 发布日期
        Map<String, Vector<double>> values;  // field -> values
        // 派生字段（非 DB 列，由节点按报告期算出来），与 values 同长度同顺序
        Map<String, Vector<double>> derivedValues;

        // 区间树索引：按 pub_date 排序，用于 O(log n) 查找
        // key = pub_date, value = 该期数据在 statDates/values 中的索引
        std::map<time_t, int> pubDateIndex;
    };
    Map<symbol_t, Map<String, FinanceData>> _cachedData;

    // 每个 symbol 当前已输出的最新数据（用于前向填充）
    Map<symbol_t, Map<String, double>> _latestValues;

    // 辅助方法
    void resolveSymbols(const nlohmann::json& config);
    void resolveOutputFields();
    void loadFinanceData();
    void writeOutput(DataContext& context, const String& symbol,
                     const Map<String, double>& values);
    void writeForwardFill(DataContext& context, const String& symbol);

    // 派生字段：不是 DB 里的列，由节点按报告期算出来
    //   profit.revenue_ttm —— 营收 TTM
    //     年报（12-31）：直接取全年营收
    //     半年报（06-30）：今年 H1 + 去年全年 − 去年 H1
    //     其它报告期：沿用上一期有效值（等价 Python 侧 asof_vals 的前向填充）
    static bool isDerivedField(const String& category, const String& field);

    // 获取指定日期之前最新已发布的数据
    bool getLatestPublishedData(const symbol_t& symbol, const String& category,
                                 time_t currentDate, Map<String, double>& outValues);
};
