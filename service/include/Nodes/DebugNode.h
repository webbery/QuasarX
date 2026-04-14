#pragma once
#include "StrategyNode.h"
#include "std_header.h"

class DebugNode: public QNode {
public:
    RegistClassName(DebugNode);
    DebugNode(Server* server);
    
    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config);

    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;

    virtual void Done(const String& strategy);

private:
    void SaveCSV(const DataFrame& df, const String& dir);
private:
    Server* _server;
    String _suffix;
    String _label;
    Set<String> _inNames;
    DataContext* _context;
};
