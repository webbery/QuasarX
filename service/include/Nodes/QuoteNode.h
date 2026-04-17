#pragma once
#include "StrategyNode.h"
#include "Util/system.h"

class Server;
class QuoteInputNode : public QNode {
public:
    RegistClassName(QuoteInputNode);
    static const nlohmann::json getParams();

    QuoteInputNode(Server* server);

    bool Init(const nlohmann::json& config);

    /**
    * @brief 处理行情数据，实现多标的时间对齐
    *
    * 实盘模式：QuoteInfo 已由引擎通过 KBarBuilder 写入 context，
    *          本节点仅负责转发到 context 的 vector 中。
    *
    * 回测模式：
    * 1. 从 StockHistorySimulation 收集所有 symbol 当前 bar 的 quote，找出最小时间戳 min_t
    * 2. 检查所有 symbol 是否与 min_t 对齐
    * 3. 根据 missingHandle 模式处理：
    *    - Skip 模式：如果时间不对齐，跳过整个 epoch（不写入任何数据）
    *    - Linear 模式：对齐的 symbol 直接写入，不对齐的线性插值
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
private:
    bool Init();

    /**
     * @brief 从 QuoteInfo 中提取指定属性值（open/close/high/low/volume）
     */
    double getProp(const QuoteInfo& quote, const String& property) const;

    /**
     * @brief 将单个 symbol 的行情数据写入 context
     */
    void writeQuote(DataContext& context, const QuoteInfo& quote);

    /**
     * @brief 线性插值：用上一个 bar 和当前 bar 插值到 targetTime 并写入 context
     * @param nextQuote 下一个 bar 的 quote（时间晚于 targetTime）
     * @param targetTime 目标时间戳
     */
    void interpolateAndWrite(DataContext& context, const symbol_t& symbol,
                             const QuoteInfo& nextQuote, time_t targetTime);

    /**
     * @brief 前向填充：用上一个已知 bar 的值填充到 targetTime 并写入 context
     * @param symbol 标的符号
     * @param targetTime 目标时间戳
     */
    void forwardFillAndWrite(DataContext& context, const symbol_t& symbol, time_t targetTime);

    /**
     * @brief 后向填充：用下一个已知 bar 的值填充到 targetTime 并写入 context
     * @param nextQuote 下一个 bar 的 quote
     * @param symbol 标的符号
     * @param targetTime 目标时间戳
     */
    void backwardFillAndWrite(DataContext& context, const QuoteInfo& nextQuote,
                              const symbol_t& symbol, time_t targetTime);

    void addQuoteProperty(DataContext& context, const String& key, double val);
private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
    MissingHandleType _missingHandle = MissingHandleType::Skip;
    Map<symbol_t, QuoteInfo> _curQuotes;   // 当前轮次各 symbol 的 quote
    Map<symbol_t, QuoteInfo> _lastQuotes;  // 每个 symbol 上一次写入的 quote
};
