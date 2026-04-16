#pragma once
#include "StrategyNode.h"

/**
 * 价差计算节点 - 用于配对交易策略
 * 
 * 支持三种价差计算方式：
 * 1. simple_diff: 简单价差 = Price_A - Price_B
 * 2. log_diff: 对数价差 = ln(Price_A) - ln(Price_B)
 * 3. rolling_regression: 滚动回归价差 = Price_A - β × Price_B
 * 
 * 输出：
 * - spread.value: 价差值
 * - spread.beta: 对冲比例β（仅rolling_regression模式）
 */
class SpreadNode: public QNode {
public:
    RegistClassName(SpreadNode);
    static const nlohmann::json getParams();

    SpreadNode();
    virtual ~SpreadNode();

    virtual bool Init(const nlohmann::json& config);
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements();

private:
    // 计算简单价差
    double calculateSimpleSpread(double priceA, double priceB) const;
    
    // 计算对数价差
    double calculateLogSpread(double priceA, double priceB) const;
    
    // 滚动回归计算（更新β值）
    double calculateRollingRegression(double priceA, double priceB);

private:
    String _method;              // 计算方法
    int32_t _window;             // 滚动回归窗口大小
    double _fixedBeta;           // 固定β值（非滚动模式）
    bool _dynamicBeta;           // 是否动态计算β
    
    // 滚动回归缓冲区
    std::vector<double> _priceABuffer;  // Price_A历史数据
    std::vector<double> _priceBBuffer;  // Price_B历史数据
    size_t _count;                       // 当前数据点计数
    size_t _nextIndex;                   // 循环缓冲区索引
    
    // 输出键名
    String _spreadOutputKey;
    String _betaOutputKey;
};
