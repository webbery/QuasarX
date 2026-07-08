#pragma once
#include "StrategyNode.h"

/**
 * 多因子合成节点
 *
 * 将多个因子加权合成为综合得分 {symbol}.composite_score
 *
 * 合成方法：
 *   - equal:  等权平均（跳过 NaN 后归一化）
 *   - custom: 用户自定义权重（按输入顺序对应，自动归一化）
 *
 * 输入：多个 {symbol}.factorX 时间序列（来自 FunctionNode / FormulaNode）
 * 输出：{symbol}.composite_score 时间序列
 */

class FactorCombineNode : public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(FactorCombineNode);

    FactorCombineNode(Server* server);
    ~FactorCombineNode() = default;

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;

private:
    Server* _server;
    String _label;
    String _method;              // "equal" | "custom"
    Vector<double> _weights;     // custom 模式的原始权重

    // 按输入顺序的 factor key 列表
    Vector<String> _factorKeys;

    // 按 symbol 分组：{symbol -> [factorKey indices into _factorKeys]}
    Map<String, Vector<size_t>> _symbolFactorIndices;

    // 输出
    Map<String, ArgType> _outputs;
};
