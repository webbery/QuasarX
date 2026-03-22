#pragma once
#include "StrategyNode.h"
#include "Function/Function.h"

class Server;
/**
 * @brief 双均线趋势跟踪策略节点
 * - 金叉买入：短期均线上穿长期均线
 * - 死叉卖出：短期均线下穿长期均线
 */
class TestNode: public QNode {
public:
    RegistClassName(TestNode);
    /**
     * @brief 获取节点的可用配置参数信息
     */
    static const nlohmann::json getParams();

    TestNode(Server* server);
    ~TestNode();

    virtual bool Init(const nlohmann::json& config);
    virtual bool Process(const String& strategy, DataContext& context);
    virtual Map<String, ArgType> out_elements();

private:
    // 双均线参数
    int _shortPeriod = 5;   // 短期均线周期
    int _longPeriod = 15;   // 长期均线周期

    // 符号池
    Vector<String> _codes;

    // 输入数据键集合（从上游节点的输出收集）
    Set<String> _input_keys;

    // 内部均线计算器状态（每个标的一个）
    struct MaState {
        Vector<double> shortMa;  // 短期均线序列
        Vector<double> longMa;    // 长期均线序列
        Vector<double> signals;   // 信号序列：1=买入, -1=卖出, 0=持有
        // 均线计算buffer
        Vector<double> shortBuffer;
        Vector<double> longBuffer;
        double shortSum = 0.0;
        double longSum = 0.0;
    };
    std::unordered_map<String, MaState> _maStates;

    // 计算单条均线的当前值
    double calcMa(double value, Vector<double>& buffer, double& sum, int period);
};
