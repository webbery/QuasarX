#pragma once
#include "StrategyNode.h"
#include "Nodes/QuoteFillStrategy.h"
#include "Util/system.h"
#include "KBarBuilder.h"
#include "server.h"

class Server;
class QuoteInputNode : public QNode {
public:
    RegistClassName(QuoteInputNode);
    static const nlohmann::json getParams();

    QuoteInputNode(Server* server);

    bool Init(const nlohmann::json& config) override;

    virtual void Prepare(const String& strategy, DataContext& context) override;
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

    Map<String, ArgType> out_elements() override;

    /**
     * @brief 获取数据源类型（"股票"/"ETF"）
     */
    const Set<contract_type>& GetSource() const { return _sources; }

    /**
     * @brief 获取当前节点已注册的 symbol 列表
     */
    const Set<symbol_t>& GetSymbols() const { return _symbols; }

    /**
     * @brief 获取频率设置
     */
    DataFrequencyType GetFreq() const { return _loadFreq; }

    /**
     * @brief 获取视图频率
     *
     * 与 GetFreq() 的区别：GetFreq 是交易所加载的原始数据频率（周频视图下为日线），
     * GetViewFreq 是本节点对外输出的 bar 节奏。周频视图的序列增长慢于日频，
     * 因此下游挂在它上面的窗口指标自动按周计算，无需任何频率换算。
     */
    DataFrequencyType GetViewFreq() const { return _viewFreq; }

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

    /**
     * @brief 把当前 bar 喂给降频聚合器，有定稿低频 bar 时写入对应 key
     * 非降频视图时为空操作
     */
    void feedView(DataContext& context, const Map<symbol_t, QuoteInfo>& quotes);

    /**
     * @brief 把定稿低频 bar 快照写入 context（key 带频率后缀）
     */
    void writeView(DataContext& context, const Map<symbol_t, QuoteInfo>& snapshot);

private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
    MissingHandleType _missingHandle = MissingHandleType::Skip;
    std::unique_ptr<IQuoteFillStrategy> _fillStrategy;  // 填充策略
    Map<symbol_t, QuoteInfo> _curQuotes;   // 当前轮次各 symbol 的 quote
    Map<symbol_t, QuoteInfo> _lastQuotes;  // 每个 symbol 上一次写入的 quote
    Set<contract_type> _sources;                // 数据源类型
    DataFrequencyType _loadFreq = DataFrequencyType::Day;  // 交易所加载的原始数据频率
    DataFrequencyType _viewFreq = DataFrequencyType::Day;  // 对外输出的 bar 节奏
    // 降频聚合器（仅当视图频率低于加载频率时非空）
    std::shared_ptr<KBarBuilder> _kbarBuilder;
    // 上一个定稿快照（用于前向填充）
    Map<symbol_t, QuoteInfo> _lastViewSnapshot;
};
