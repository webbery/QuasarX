#pragma once
#include "DataContext.h"
#include "json.hpp"
#include "onnxruntime/onnxruntime_cxx_api.h"

#define RegistClassName(clsName) static String className() { return #clsName; }

class ICallable;

enum ArgType {
    Integer,
    Double,
};

class QNode {
    using Edges = MultiMap<String, QNode*>;
public:
    virtual ~QNode(){}
    virtual bool Init(const nlohmann::json& config) = 0;
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual bool Process(const String& strategy, DataContext& context) = 0;
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