#pragma once
#include "StrategyNode.h"

class FormulaParser;

class FormulaNode : public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(FormulaNode);

    FormulaNode(Server* server);
    ~FormulaNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;

private:
    Server* _server;
    FormulaParser* _parser;
    String _expression;
    Vector<symbol_t> _symbols;
    String _label;
};
