#pragma once
#include "StrategyNode.h"

class Server;
class QuoteInputNode : public QNode {
public:
    QuoteInputNode(Server* server);

    virtual feature_t Process(const DataFeatures& org, const feature_t& input);

    void AddSymbol(symbol_t symbol) { _symbols.insert(symbol); }

    void EraseSymbol(symbol_t symbol) { _symbols.erase(symbol); }

    void Connect(QNode* next, const String& from, const String& to);

private:
    bool Init();

private:
    Set<symbol_t> _symbols;
    Map<size_t, String> _validDatumNames;
    Server* _server;
};
