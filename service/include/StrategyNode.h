#pragma once
#include "DataContext.h"
#include "json.hpp"

#define RegistClassName(clsName) static String className() { return #clsName; }

class ICallable;

/**
 * @brief 节点处理结果枚举
 * Success: 正常执行完成
 * Skip: 本轮跳过（时间不对齐、超时等），不执行后续节点和风控
 * Finished: 数据已用完（仅回测模式正常结束）
 * Error: 执行错误，终止策略
 */
enum class NodeProcessResult {
    Success,
    Skip,
    Finished,
    Error
};

enum ArgType {
    // 标量类型（单个值）
    Double_Scalar = 2,
    Integer_Scalar = 3,
    Bool_Scalar = 4,

    // 时间序列类型（Vector）
    Double_TimeSeries = 5,
    Integer_TimeSeries = 6,
    Bool_TimeSeries = 7,
};

// 辅助函数：判断是否为时间序列类型
inline bool isTimeSeriesType(ArgType type) {
    return type == Double_TimeSeries || type == Integer_TimeSeries || type == Bool_TimeSeries;
}

// 辅助函数：判断是否为标量类型
inline bool isScalarType(ArgType type) {
    return type == Double_Scalar || type == Integer_Scalar || type == Bool_Scalar;
}

// 辅助函数：获取基础数据类型
inline ArgType getBaseType(ArgType type) {
    switch (type) {
        case Double_Scalar:
        case Double_TimeSeries:
            return Double_Scalar;
        case Integer_Scalar:
        case Integer_TimeSeries:
            return Integer_Scalar;
        case Bool_Scalar:
        case Bool_TimeSeries:
            return Bool_Scalar;
        default:
            return Double_Scalar;
    }
}

// 行情数据缺失时的处理方式
enum class MissingHandleType : char {
    Skip,           // 跳过（默认），缺失时不写入
    Linear,         // 线性插值，用前后 bar 插值补齐
    ForwardFill,    // 前向填充，用上一个已知值填充
    BackwardFill,   // 后向填充，用下一个已知值填充
};

class QNode {
    using Edges = MultiMap<String, QNode*>;
public:
    virtual ~QNode(){}
    virtual bool Init(const nlohmann::json& config) = 0;
    /**
     * @brief 对输入数据做处理，并返回处理结果状态
     * @return NodeProcessResult 返回处理状态：Success/Skip/Finished/Error
     */
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) = 0;
    virtual void Connect(QNode* next, const String& from, const String& to) {
        _outs.insert({from, next});
        next->_ins.insert({to, this});
    }
    void Disconnect(QNode* other) {
        for (auto it = _outs.begin(); it != _outs.end(); )
            it->second == other ? it = _outs.erase(it) : ++it;
        for (auto it = _ins.begin(); it != _ins.end(); )
            it->second == other ? it = _ins.erase(it) : ++it;
    }
    virtual void Done(const String& strategy) {}
    virtual void Prepare(const String& strategy, DataContext& context) {}
    /**
     * @brief 返回输出结果在context中的输出名
     */
    virtual Map<String, ArgType> out_elements();

    virtual void UpdateLabel(const String& label) {}

    void setID(const uint32_t name){ _id = name; }

    size_t in_degree() const { return _ins.size(); }
    size_t out_degree() const { return _outs.size(); }

    const Edges& outs() const { return _outs; }
    const Edges& ins() const { return _ins; }

    uint32_t id() { return _id; }

    // BFS 遍历所有上游节点，找到 QuoteInputNode 并提取其 symbol 集合
    Set<symbol_t> discoverUpstreamSymbols();

    // BFS 遍历所有下游节点（后继节点）
    Set<QNode*> discoverDownstreamNodes();

    /**
     * @brief 返回本节点使用的所有特征名（用于上游数据源节点动态加载）
     * 
     * 特征名格式约定：
     *   - 行情数据: "close", "open", "high", "low", "volume"
     *   - 基本面: "profit.roe_avg", "growth.yoy_ni" (category.field)
     *   - 技术指标: "ma_5", "rsi_14" (label 或 feature name)
     * 
     * 默认实现返回空集合，子类按需覆盖。
     * 返回的特征名用于上游数据源节点（如 FundamentalNode）动态推断需要加载的数据。
     */
    virtual Set<String> getRequiredFeatures() { return {}; }

protected:
    // 从连接图解析所有输入映射: dataName → contextKey
    // 遍历 _ins，匹配上游 out_elements，提取 sourceHandle 中的数据名
    Map<String, String> resolveInputConnections();

    uint32_t _id;
    // key是handle名
    Edges _outs;
    Edges _ins;
};

