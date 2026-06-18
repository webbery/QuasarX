#pragma once
#include "StrategyNode.h"
#include "Nodes/QuoteFillStrategy.h"
#include "Util/system.h"
#include "server.h"

class Server;
class QuoteInputNode : public QNode {
public:
    RegistClassName(QuoteInputNode);
    static const nlohmann::json getParams();

    QuoteInputNode(Server* server);

    bool Init(const nlohmann::json& config);

    virtual void Prepare(const String& strategy, DataContext& context);
    /**
    * @brief 处理行情数据，实现多标的时间对齐
    *
    * 实盘模式：QuoteInfo 已由引擎通过 KBarBuilder 写入 context，
    *          本节点仅负责转发到 context 的 vector 中。
    *
    * 回测模式：
    * 1. 从 HistorySimulationBase 收集所有 symbol 当前 bar 的 quote，找出最小时间戳 min_t
    * 2. 检查所有 symbol 是否与 min_t 对齐
    * 3. 根据填充策略处理：
    *    - Skip：不对齐时跳过整个 epoch（不写入任何数据）
    *    - ForwardFill：不对齐的 symbol 用上一个已知值填充
    *    - Linear：不对齐的 symbol 用前后 bar 线性插值
    *    - BackwardFill：不对齐的 symbol 用下一个已知值填充
    * 4. 检测是否有任何 symbol 已到达末尾（time==0），返回 Finished
    *
    * @return NodeProcessResult::Success 成功
    *         NodeProcessResult::Skip 时间不对齐，跳过本轮
    *         NodeProcessResult::Finished 数据已用完
    */
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    Map<String, ArgType> out_elements();

    /**
     * @brief 获取数据源类型（"股票"/"ETF"）
     */
    const String& GetSource() const { return _source; }

    /**
     * @brief 获取频率设置
     */
    DataFrequencyType GetFreq() const { return _freq; }
private:
    /**
     * @brief 从 QuoteInfo 中提取指定属性值（open/close/high/low/volume）
     */
    double getProp(const QuoteInfo& quote, const String& property) const;

    /**
     * @brief 将单个 symbol 的行情数据写入 context
     */
    void writeQuote(DataContext& context, const QuoteInfo& quote);

    void addQuoteProperty(DataContext& context, const String& key, double val);
private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
    MissingHandleType _missingHandle = MissingHandleType::Skip;
    std::unique_ptr<IQuoteFillStrategy> _fillStrategy;  // 填充策略
    Map<symbol_t, QuoteInfo> _curQuotes;   // 当前轮次各 symbol 的 quote
    Map<symbol_t, QuoteInfo> _lastQuotes;  // 每个 symbol 上一次写入的 quote
    String _source = "股票";                // 数据源类型
    DataFrequencyType _freq = DataFrequencyType::Day;  // 频率设置
};
