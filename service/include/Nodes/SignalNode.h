#pragma once
#include "StrategyNode.h"
#include "Util/system.h"

class FormulaParser;
class Server;
// 构建买入/卖出信号
class SignalNode: public QNode {
public:
    SignalNode(Server* server);
    ~SignalNode();

    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

    bool ParseBuyExpression(const String& expression);
    bool ParseSellExpression(const String& expression);

private:


private:
    Server* _server;
    FormulaParser* _buyParser;
    FormulaParser* _sellParser;
    Vector<symbol_t> _pools;
};