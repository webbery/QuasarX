#pragma once
#include "StrategyNode.h"

/**
 * @brief 风控保护节点
 *
 * 合并止损、止盈、追踪止损、时间止损四种保护逻辑。
 * 直接从 Server/Exchange 获取当前持仓，检查是否触发任一保护器，
 * 触发后写入 RiskContext 短路信号，后续节点检查后跳过。
 *
 * 配置参数：
 *   stop_loss:    { "enabled": bool, "percent": double }
 *   take_profit:  { "enabled": bool, "percent": double }
 *   trailing_stop:{ "enabled": bool, "percent": double }
 *   time_stop:    { "enabled": bool, "max_bars": int }
 */
class ProtectionNode : public QNode {
public:
    RegistClassName(ProtectionNode);
    static const nlohmann::json getParams();

    ProtectionNode(Server* server);
    ~ProtectionNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements();

private:
    struct Guard {
        bool enabled = false;
        double percent = 0.0;     // SL / TP / TS 用
        int max_bars = 0;         // TimeStop 用
    };

    Server* _server;
    Guard _sl;   // 止损
    Guard _tp;   // 止盈
    Guard _ts;   // 追踪止损
    Guard _time; // 时间止损

    // 记录每个标的入场信息（节点自己维护）
    struct EntryInfo {
        double avg_price = 0.0;       // 入场均价
        double highest_price = 0.0;   // 持仓期间最高价
        int    entry_bar = 0;         // 入场 Bar 索引
    };
    Map<symbol_t, EntryInfo> _entry_info;

    // 从 Server 同步持仓，更新 _entry_info
    void syncPositions(const String& strategy, DataContext& context);
};
