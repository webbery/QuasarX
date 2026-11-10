#pragma once
#include "StrategyNode.h"
#include "Nodes/QuoteFillStrategy.h"
#include "Util/system.h"
#include "KBarBuilder.h"
#include "server.h"

class Server;
using QuoteMap = Map<symbol_t, QuoteInfo>;
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

    /**
     * @brief 已定稿的视图 bar 数量（仅降频视图维护，按 symbol）
     *
     * 视图序列在日线网格上逐日前向填充，向量长度每个 epoch 都在增长，
     * 下游不能靠长度判断「有没有新的低频数据」。这个计数只在定稿时递增，
     * 是「窗口该往前推一根」的唯一可靠信号（FunctionNode 等下游使用）。
     */
    uint64_t GetViewVersion(symbol_t symbol) const {
        auto itr = _viewVersions.find(symbol);
        return itr == _viewVersions.end() ? 0 : itr->second;
    }

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

#ifdef _DEBUG
    /**
     * @brief 记录本 epoch 里「没有自己 bar」的标的（停牌 / 缺数据）
     *
     * 仅 Debug 构建启用：每个标的每次运行只打一条 DEBUG_INFO，避免停牌期间
     * 逐 bar 刷屏。消息里带上该标的的 bar 日期与本次对齐的参考日期，
     * 便于定位停牌区间。Release 下整段编译掉。
     */
    void warnMisaligned(time_t minTime);
#endif

    /**
     * @brief 把当前 bar 喂给降频聚合器，有定稿低频 bar 时写入对应 key
     * 非降频视图时为空操作
     */
    void feedView(DataContext& context, const QuoteMap& quotes);

    /**
     * @brief 把定稿低频 bar 快照写入 context（key 带频率后缀）
     *
     * writeNaN=true 时写入 NaN 占位（尚无定稿 bar 的 epoch）。占位保证视图列
     * 从第一个 epoch 起就与时间轴逐行对齐，否则整列会相对时间轴整体前移。
     */
    void writeView(DataContext& context, const QuoteMap& snapshot,
                   bool writeNaN = false);

private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
    MissingHandleType _missingHandle = MissingHandleType::Skip;
    std::unique_ptr<IQuoteFillStrategy> _fillStrategy;  // 填充策略
    QuoteMap _curQuotes;   // 当前轮次各 symbol 的 quote
    QuoteMap _lastQuotes;  // 每个 symbol 上一次写入的 quote
#ifdef _DEBUG
    // 本次运行里已提示过「该 bar 无行情」的标的（Prepare 复位）
    Set<symbol_t> _warnedMisaligned;
#endif
    Set<contract_type> _sources;                // 数据源类型
    DataFrequencyType _loadFreq = DataFrequencyType::Day;  // 交易所加载的原始数据频率
    DataFrequencyType _viewFreq = DataFrequencyType::Day;  // 对外输出的 bar 节奏
    // 降频聚合器（仅当视图频率低于加载频率时非空）
    std::shared_ptr<KBarBuilder> _kbarBuilder;
    // 上一个定稿快照（用于前向填充）
    Map<symbol_t, QuoteInfo> _lastViewSnapshot;
    // 每个 symbol 已定稿的视图 bar 数量（见 GetViewVersion）
    Map<symbol_t, uint64_t> _viewVersions;
};
