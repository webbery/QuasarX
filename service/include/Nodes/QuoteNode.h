#pragma once
#include "StrategyNode.h"
#include "Util/system.h"

class Server;
class QuoteInputNode : public QNode {
public:
    RegistClassName(QuoteInputNode);
    static const nlohmann::json getParams();

    QuoteInputNode(Server* server);

    bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    Map<String, ArgType> out_elements();
private:
    bool Init();

private:
    Set<symbol_t> _symbols;
    Map<String, Set<String>> _properties;
    Server* _server;
};
