#pragma once
#include "StrategyNode.h"

class Server;
class QuoteInputNode : public QNode {
public:
    QuoteInputNode(Server* server);

    virtual bool Process(const String& strategy, DataContext& context, const DataFeatures& org);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    void Connect(QNode* next, const String& from, const String& to);

private:
    bool Init();

private:
    Set<symbol_t> _symbols;
    Map<String, String> _validDatumNames;
    Server* _server;
};
