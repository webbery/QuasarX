#pragma once

#include "StrategyNode.h"
#include "RiskContext.h"
#include "Guard/Guard.h"
#include "Guard/GuardFactory.h"

class FormulaParser;

/**
 * @brief 风控保护节点（Guard 架构重构版）
 *
 * 设计改进：
 * 1. 使用 Guard 抽象类，按配置动态创建止损策略
 * 2. 支持在 ExecuteNode 之后检查当天买入的标的
 * 3. Guard 可独立测试和扩展
 *
 * 执行流程：
 * - Process(): 在决策 bar 检查现有持仓
 * - postExecuteCheck(): 在 ExecuteNode 执行后检查新买入的标的
 */
class ProtectionNode : public QNode {
public:
    struct ProtectionEvent {
        int _bar_index = 0;
        time_t _datetime = 0;
        symbol_t _symbol;
        RiskTriggerType _type = RiskTriggerType::None;
        double _entry_price = 0.0;    // 入场成本（原始价）
        double _current_price = 0.0;  // 触发时当前价（原始价）
        int64_t _quantity = 0;
    };

    RegistClassName(ProtectionNode);
    static const nlohmann::json getParams();

    ProtectionNode(Server* server);
    ~ProtectionNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements();

    const Vector<ProtectionEvent>& GetProtectionEvents() const { return _events; }
    void ClearProtectionEvents() { _events.clear(); }

    /**
     * @brief 在 ExecuteNode 执行后检查新买入的标的
     * 
     * 解决问题：ProtectionNode 在 ExecuteNode 之前执行，
     * 无法检查当天买入的标的。此方法在 ExecuteNode 之后调用，
     * 检查新买入的持仓是否触发止损。
     */
    void postExecuteCheck(const String& strategy, DataContext& context);

    // 获取已触发的标的（供 ExecuteNode 判断是否需要紧急平仓）
    const Set<symbol_t>& GetTriggeredSymbols() const { return _pending_close; }

private:
    // 从 Server 同步持仓，更新 _entry_info
    void syncPositions(const String& strategy, DataContext& context);

    // 检查所有 Guard
    RiskTriggerType checkAllGuards(symbol_t symbol, DataContext& context, const EntryInfo& info);

    // 更新 Guard 状态
    void updateAllGuards(symbol_t symbol, DataContext& context, EntryInfo& info);

    // 处理触发事件
    void handleTrigger(symbol_t symbol, RiskTriggerType type, const String& strategy, 
                       DataContext& context, const EntryInfo& info);

    // 获取持仓数量
    int64_t get_position_qty(const String& strategy, DataContext& context, symbol_t symbol);
    
    // 获取持仓成本
    double get_position_cost(const String& strategy, DataContext& context, symbol_t symbol);

private:
    Server* _server;
    Vector<std::unique_ptr<Guard>> _guards;  // Guard 实例列表

    // 持仓入场信息
    Map<symbol_t, EntryInfo> _entry_info;

    // 风控触发事件
    Vector<ProtectionEvent> _events;

    // 已触发但尚未平仓的标的
    Set<symbol_t> _pending_close;

    // 公式止损（特殊处理）
    FormulaParser* _formulaParser = nullptr;
    Vector<symbol_t> _formulaSymbols;
    Set<String> _formulaVariants;
    bool _formulaEnabled = false;
    String _formulaExpr;
};