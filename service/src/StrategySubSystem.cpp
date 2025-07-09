#include "StrategySubSystem.h"
#include "FeatureSubsystem.h"
#include "AgentSubSystem.h"
#include "Util/Strategy.h"
#include "Util/log.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>

#define INIT_STRATEGY(classname) {\
    auto strategy = new classname();\
    _strategies[strategy->Name()] = strategy;\
}

StrategySubSystem::StrategySubSystem(Server* server)
:_featureSystem(nullptr), _agentSystem(nullptr), _handle(server) {
    _featureSystem = new FeatureSubsystem(server);
    _agentSystem = new AgentSubsystem(server);
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

        AddStrategy(strategy);
    }
    _featureSystem->Start();
    _agentSystem->Start();
}

void StrategySubSystem::Release() {
    _strategies.clear();
}

List<String> StrategySubSystem::GetStrategyNames() {
    //List<String> names;
    //for (auto& item: _strategies) {
    //    names.push_back(item.first);
    //}
    return  { _strategies.begin(), _strategies.end() };
}

bool StrategySubSystem::HasStrategy(const String& name) {
    return _strategies.count(name);
}

AgentStrategyInfo StrategySubSystem::ParseJsonScript(const String& content) {
    AgentStrategyInfo info;
    nlohmann::json script_content = nlohmann::json::parse(content);
    if (script_content.is_discarded()) {
        WARN("script parse fail.");
        return info;
    }
    return parse_strategy_script(script_content);
}

bool StrategySubSystem::CreateStrategy(const String& name, const nlohmann::json& params) {
    auto& features = params["feature"];
    auto& agent = params["agent"];
    _strategies.insert(name);
    return true;
}

bool StrategySubSystem::AddStrategy(const AgentStrategyInfo& info) {
    if (_strategies.count(info._name)) {
        INFO("Strategy {} exist. Please check.", info._name);
        return false;
    }

    _strategies.insert(info._name);
    _featureSystem->LoadConfig(info);
    _agentSystem->LoadConfig(info);
    return true;
}

void StrategySubSystem::Train(const String& name, const Vector<symbol_t>& history, DataFrequencyType freq) {
    auto data = _handle->PrepareData({history.begin(), history.end()}, freq);
}
