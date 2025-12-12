#pragma once
#include "StrategyNode.h"
#include "onnxruntime/onnxruntime_cxx_api.h"

class ArtificialIntelligenceNode: public QNode {
public:

protected:
    Vector<const char*> _modelInputs;
    Vector<const char*> _modelOutputs;

    static Ort::Env _env;
};

class LSTMNode: public ArtificialIntelligenceNode {
public:
    RegistClassName(LSTMNode);
    
    ~LSTMNode();

    virtual bool Init(const nlohmann::json& config);

    virtual bool Process(const String& strategy, DataContext& context);

    static const nlohmann::json getParams();
private:
    std::vector<std::vector<int64_t>> InitInput();

    std::vector<std::vector<int64_t>> InitOutput();
    
private:
    Ort::Session* _session = nullptr;

    char _predictWindow = 1;
    char _inputWindow = 4;
    List<String> _inputNames;

};