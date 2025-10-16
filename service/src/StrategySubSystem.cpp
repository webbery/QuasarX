#include "StrategySubSystem.h"
#include "FeatureSubsystem.h"
#include "AgentSubSystem.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>
#include "PortfolioSubsystem.h"

#define INIT_STRATEGY(classname) {\
    auto strategy = new classname();\
    _strategies[strategy->Name()] = strategy;\
}

AgentStrategyInfo::~AgentStrategyInfo() {
    
}

StrategySubSystem::StrategySubSystem(Server* server)
:_featureSystem(nullptr), _agentSystem(nullptr), _handle(server) {
    _featureSystem = new FeatureSubsystem(server);
    _agentSystem = new FlowSubsystem(server);
}

StrategySubSystem::~StrategySubSystem() {
    Release();
}

void StrategySubSystem::Init() {
    // load scripts
    auto cur_path = std::filesystem::current_path();
    if (!std::filesystem::exists("scripts")) {
        std::filesystem::create_directories("scripts");
    }
    for (auto& script_file: std::filesystem::directory_iterator("scripts")) {
        std::ifstream ifs(script_file.path().string());
        if (!ifs.is_open()) {
            FATAL("Load Script Fail: {}", script_file.path().string());
            continue;
        }
        
        std::stringstream buffer;  
        buffer << ifs.rdbuf();
        ifs.close();
        String script_content = buffer.str();

        AgentStrategyInfo strategy;
        auto ext = script_file.path().extension().string();
        if (ext == ".json") {
            strategy = ParseJsonScript(script_content);
        }
        if (strategy._pool.empty()) {
            continue;
        }
        auto filename = script_file.path().stem();
        strategy._name = filename.string();
        strategy._virtual = false;
        if (_virtualStrategies.count(strategy._name)) {
            strategy._virtual = true;
        }
        // AddStrategy(strategy);
        auto pfSystem = _handle->GetPortforlioSubSystem();
        auto& portfolio = pfSystem->GetPortfolio(strategy._name);
        portfolio._pools = Set<String>{strategy._pool.begin(), strategy._pool.end()};
    }
    _featureSystem->InitSecondLvlFeatures();
    _featureSystem->Start();
}

bool StrategySubSystem::Run(const String& strategy) {
    _agentSystem->Start(strategy);
    return true;
}

void StrategySubSystem::Release() {
    _strategies.clear();
}

List<String> StrategySubSystem::GetStrategyNames() {
    List<String> names{ _strategies.begin(), _strategies.end() };
    for (auto& n: _virtualStrategies) {
        names.emplace_back(n.data());
    }
    return  names;
}

void StrategySubSystem::SetupSimulation(const String& name) {
    _virtualStrategies.insert(name);
}

bool StrategySubSystem::HasStrategy(const String& name) {
    return _strategies.count(name);
}

void StrategySubSystem::RegistCollection(const String& strategy, const String& featureName, const nlohmann::json& params) {
    _featureSystem->RegistCollection(strategy, featureName, params);
}

void StrategySubSystem::ClearCollections(const String& strategy) {
    _featureSystem->ClearCollections(strategy);
}

Map<symbol_t, Map<String, List<feature_t>>> StrategySubSystem::GetCollections(const String& strategy) {
    return _featureSystem->GetCollection(strategy);
}

AgentStrategyInfo StrategySubSystem::ParseJsonScript(const String& content) {
    AgentStrategyInfo info;
    nlohmann::json script_content = nlohmann::json::parse(content);
    if (script_content.is_discarded()) {
        WARN("script parse fail.");
        return info;
    }
    return info;
    // return parse_strategy_script(script_content);
}

bool StrategySubSystem::CreateStrategy(const String& name, const nlohmann::json& params) {
    auto& features = params["feature"];
    auto& agent = params["agent"];
    _strategies.insert(name);
    return true;
}

// bool StrategySubSystem::AddStrategy(const AgentStrategyInfo& info) {
//     if (_strategies.count(info._name)) {
//         INFO("Strategy {} exist. Please check.", info._name);
//         return false;
//     }

//     if (!_agentSystem->LoadFlow(info)) {
//         //LOG("load agent `{}` fail.", info._name);
//         return false;
//     }
//     _featureSystem->LoadConfig(info);
    
//     _strategies.insert(info._name);
//     return true;
// }

void StrategySubSystem::Train(const String& name, const Vector<symbol_t>& history, DataFrequencyType freq) {
    auto data = _handle->PrepareData({history.begin(), history.end()}, freq);
}

void StrategySubSystem::DeleteStrategy(const String& name) {
    _featureSystem->ErasePipeline(name);
    _strategies.erase(name);
}

void StrategySubSystem::Init(const String& strategy, const List<QNode*>& flow) {
    _agentSystem->LoadFlow(strategy, flow);
}
