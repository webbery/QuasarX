#pragma once
#include "StrategyNode.h"
#include "Util/system.h"

class FormulaParser;
class Server;
// 构建买入/卖出信号
class SignalNode: public QNode {
public:
    RegistClassName(SignalNode);
    static const nlohmann::json getParams();

    SignalNode(Server* server);
    ~SignalNode();

    virtual bool Init(const nlohmann::json& config);
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements();

    bool ParseBuyExpression(const String& expression);
    bool ParseSellExpression(const String& expression);

    Set<symbol_t> GetPool() {
        return {_pools.begin(), _pools.end()};
    }
    
private:

    Server* _server;
    FormulaParser* _buyParser;
    FormulaParser* _sellParser;
    Vector<symbol_t> _pools;
    bool _allowShort = false;  // 是否允许做空
};