#pragma once
#include "StrategyNode.h"

class Server;
class QuoteInputNode : public QNode {
public:
    QuoteInputNode(Server* server);

    bool Init(DataContext& context, const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    void Connect(QNode* next, const String& from, const String& to);

private:
    bool Init();

private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
};
