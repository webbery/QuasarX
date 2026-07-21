#pragma once
#include "StrategyNode.h"
#include "std_header.h"

/**
 * 包络突破状态机节点
 *
 * 4 态状态机（含 hysteresis）：
 *   1 = ABOVE_UPPER    突破上轨
 *   2 = FALLBACK_UPPER 回落至上轨内
 *   3 = BELOW_LOWER    突破下轨
 *   4 = FALLBACK_LOWER 回落下轨内
 *   0 = NONE           无信息（warmup）
 *
 * 输入（3 个 handle）：
 *   handle "value" → 当前价格（通常来自 QuoteInput.close）
 *   handle "upper" → 上轨（通常来自 FormulaNode）
 *   handle "lower" → 下轨（通常来自 FormulaNode）
 *
 * 输出：
 *   {symbol}.{label}           — 突破状态 (0-4)
 *   {symbol}.{label}_duration  — 当前状态持续 bar 数
 */
class BreakoutNode : public QNode {
public:
    RegistClassName(BreakoutNode);
    BreakoutNode(Server* server);

    static const nlohmann::json getParams();

    bool Init(const nlohmann::json& config) override;
    NodeProcessResult Process(const String& strategy, DataContext& context) override;
    Map<String, ArgType> out_elements() override;

private:
    struct SymbolState {
        int state = 0;
        int duration = 0;
    };

    Server* _server;
    String _label;
    Map<String, SymbolState> _states;

    // handle → context key 映射
    String _valueKey;  // close 价格的 context key
    String _upperKey;  // 上轨 context key
    String _lowerKey;  // 下轨 context key

    Map<String, ArgType> _outputs;
};
