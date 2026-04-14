#pragma once
#include "DataContext.h"
#include "json.hpp"
// #include "onnxruntime/onnxruntime_cxx_api.h"

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
    // 已弃用类型（用于向后兼容过渡）
    Integer_Deprecated = 0,
    Double_Deprecated = 1,

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
        case Double_Deprecated:
            return Double_Scalar;
        case Integer_Scalar:
        case Integer_TimeSeries:
        case Integer_Deprecated:
            return Integer_Scalar;
        case Bool_Scalar:
        case Bool_TimeSeries:
            return Bool_Scalar;
        default:
            return Double_Scalar;
    }
}

// 辅助函数：将旧类型迁移到新类型（默认假设为时间序列）
inline ArgType migrateLegacyType(ArgType type) {
    switch (type) {
        case Double_Deprecated:
            return Double_TimeSeries;
        case Integer_Deprecated:
            return Integer_TimeSeries;
        default:
            return type;
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
protected:
    uint32_t _id;
    // key是handle名
    Edges _outs;
    Edges _ins;
};

#if 0
class ArtificialIntelligenceNode: public QNode {
public:

protected:
    String ConvertServerModelPath(const String& uploadPath);

    std::vector<std::vector<int64_t>> InitInput();

    std::vector<std::vector<int64_t>> InitOutput();
protected:
    Vector<const char*> _modelInputs;
    Vector<const char*> _modelOutputs;

    Ort::Session* _session = nullptr;
    static Ort::Env _env;
};
#endif