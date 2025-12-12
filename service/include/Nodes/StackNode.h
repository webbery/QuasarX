#pragma once
#include "StrategyNode.h"

// 输入要素按水平合并或者堆叠合并
class StackNode: public QNode {
public:
    RegistClassName(StackNode);
    static const nlohmann::json getParams();
    
    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);

private:
    void HStack(DataContext& context);
    void Stack(DataContext& context);

private:
    bool _hstack;
    short _window = 0;
    List<String> _orders;
    String _outname;
};