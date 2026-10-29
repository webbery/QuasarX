#pragma once
#include "StrategyNode.h"
#include "Util/system.h"

class Server;

/**
 * 信号节点基类。
 *
 * 统一处理各信号节点共有的部分：
 *   1. 标的发现：从上游 BFS 得到信号作用的标的池
 *   2. 账户持仓读取：回测走 HistorySimulationBase，实盘走 StrategyCapital
 *   3. 做空门控：allowShort 开关 + margin_stock 可做空名单
 *   4. signal 写入：{symbol}.signal = +1 买入 / -1 卖出 / 0 保持（含 warmup 预填）
 *
 * 派生类只负责"算出每个标的该做什么"。
 */
class SignalNodeBase : public QNode {
public:
    SignalNodeBase(Server* server) : _server(server) {}
    virtual ~SignalNodeBase() = default;

    /// 通用初始化：allowShort + 上游标的发现 + 可做空名单
    virtual bool Init(const nlohmann::json& config) override;

    /// 输出要素：{symbol}.signal
    virtual Map<String, ArgType> out_elements() override;

protected:
    /// 读账户真实持仓（正=多，负=空）；无持仓的标的不出现在返回的 map 里
    Map<symbol_t, int64_t> loadPositions(const String& strategy) const;

    /// 做空门控：无持仓的 SELL 降级为 HOLD。
    /// allowShort=false 时一律降级；allowShort=true 时只有 margin_stock
    /// 名单内的标的保留（名单外的降级为 HOLD）。
    void applyShortableFilter(List<Pair<symbol_t, TradeAction>>& actions,
                               const Map<symbol_t, int64_t>& held) const;

    /// 已持仓的标的不再追加买入（BUY → HOLD）
    void applyNoAddOnPosition(List<Pair<symbol_t, TradeAction>>& actions,
                               const Map<symbol_t, int64_t>& held) const;

    /// 把每个标的的最终动作写进 {symbol}.signal（每根 bar 一条，含 warmup 预填）
    void writeSignals(DataContext& context,
                      const Map<symbol_t, TradeAction>& decisions) const;

    /// 是否允许对该标的开空仓
    bool canOpenShort(symbol_t symbol) const {
        return _allowShort && _shortableSymbols.count(symbol) > 0;
    }

    /// 节点类型名（仅用于日志）
    virtual String nodeTypeName() const { return "SignalNodeBase"; }

    /// 加载 margin_stock 名单，筛出标的池里可做空的标的
    void ensureMarginStockData();

protected:
    Server* _server;
    Vector<symbol_t> _pools;
    bool _allowShort = false;
    Set<symbol_t> _shortableSymbols;
};
