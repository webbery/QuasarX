#pragma once
#include "StrategyNode.h"
#include "RiskContext.h"

class FormulaParser;
class ATR;
class MA;
class R2;

/**
 * @brief 风控保护节点
 *
 * 合并止损、止盈、追踪止损、时间止损等多种保护逻辑。
 * 直接从 Server/Exchange 获取当前持仓，检查是否触发任一保护器，
 * 触发后写入 RiskContext 短路信号，后续节点检查后跳过。
 *
 * Phase 0 新增 4 类止损（ATR/MA/R2/MAE）：
 *   - 内部计算 ATR/MA/R2，无需上游 FunctionNode
 *   - 全局参数（所有标的共享 period/multiplier）
 *   - per-symbol 计算器实例（每个标的独立状态）
 *
 * 配置参数：
 *   stop_loss:    { "enabled": bool, "percent": double }
 *   take_profit:  { "enabled": bool, "percent": double }
 *   trailing_stop:{ "enabled": bool, "percent": double }
 *   time_stop:    { "enabled": bool, "max_bars": int }
 *   formula_stop: { "enabled": bool, "expression": string }
 *   atr_stop_loss: { "enabled": bool, "period": int, "multiplier": double }
 *   ma_stop_loss:  { "enabled": bool, "period": int }
 *   r2_stop_loss:  { "enabled": bool, "period": int, "threshold": double }
 *   mae_stop_loss: { "enabled": bool, "percent": double }
 */
class ProtectionNode : public QNode {
public:
    struct ProtectionEvent {
        int bar_index = 0;
        time_t datetime = 0;
        symbol_t symbol;
        RiskTriggerType type = RiskTriggerType::None;
        double entry_price = 0.0;
        double current_price = 0.0;
    };

    RegistClassName(ProtectionNode);
    static const nlohmann::json getParams();

    ProtectionNode(Server* server);
    ~ProtectionNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements();

    const Vector<ProtectionEvent>& GetProtectionEvents() const { return _events; }

private:
    struct Guard {
        bool enabled = false;
        double percent = 0.0;     // SL / TP / TS / MAE 用
        int max_bars = 0;         // TimeStop 用
        // Phase 0 新增字段
        int period = 20;          // ATR/MA/R2 窗口周期
        double multiplier = 2.0;  // ATR 倍数
        double threshold = 0.5;   // R² 阈值
    };

    Server* _server;
    Guard _sl;      // 止损
    Guard _tp;      // 止盈
    Guard _ts;      // 追踪止损
    Guard _time;    // 时间止损
    Guard _formula; // 公式止损
    // Phase 0: 4 类新止损
    Guard _atr_sl;  // ATR 自适应止损
    Guard _ma_sl;   // MA 跌破止损
    Guard _r2_sl;   // R² 趋势消失止损
    Guard _mae_sl;  // MAE 最大不利偏移止损

    // 记录每个标的入场信息（节点自己维护）
    struct EntryInfo {
        double avg_price = 0.0;       // 入场均价
        double highest_price = 0.0;   // 持仓期间最高价
        double lowest_price = 0.0;    // 持仓期间最低价（MAE 用）
        int    entry_bar = 0;         // 入场 Bar 索引
    };
    Map<symbol_t, EntryInfo> _entry_info;

    // 风控触发事件记录（供回测 summary 和复盘使用）
    Vector<ProtectionEvent> _events;

    // 公式止损
    FormulaParser* _formulaParser = nullptr;
    Vector<symbol_t> _formulaSymbols;
    Set<String> _formulaVariants;

    // Phase 0: 内部计算器（per-symbol，始终内部计算）
    Map<symbol_t, std::unique_ptr<ATR>> _atr_calc;
    Map<symbol_t, std::unique_ptr<MA>>  _ma_calc;
    Map<symbol_t, std::unique_ptr<R2>>  _r2_calc;

    // 从 Server 同步持仓，更新 _entry_info
    void syncPositions(const String& strategy, DataContext& context);
};
