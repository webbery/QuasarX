#pragma once
#include "StrategyNode.h"
#include "DataGroup.h"

class DebugNode: public QNode {
public:
    RegistClassName(DebugNode);
    DebugNode(Server* server);
    
    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    virtual void Done(const String& strategy);

private:
    void SaveCSV(const DataFrame& df);
private:
    Server* _server;
    String _suffix;
    String _label;
    List<String> _inNames;
    DataContext* _context;
};
