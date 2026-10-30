#pragma once
#include "Nodes/SignalNodeBase.h"
#include "Util/system.h"

class FormulaParser;
class Server;
// 构建买入/卖出信号
class SignalNode: public SignalNodeBase {
public:
    RegistClassName(SignalNode);
    static const nlohmann::json getParams();

    SignalNode(Server* server);
    ~SignalNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Set<String> getRequiredFeatures() override;

    bool ParseBuyExpression(const String& expression);
    bool ParseSellExpression(const String& expression);

    Set<symbol_t> GetPool() {
        return {_pools.begin(), _pools.end()};
    }
protected:
    String nodeTypeName() const override { return className(); }
private:
    FormulaParser* _buyParser;
    FormulaParser* _sellParser;
    String _buyExpression;     // 原始买入表达式（用于日志）
    String _sellExpression;    // 原始卖出表达式（用于日志）
};
