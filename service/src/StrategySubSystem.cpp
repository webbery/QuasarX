#include "StrategySubSystem.h"
#include "BrokerSubSystem.h"
#include "FeatureSubsystem.h"
#include "AgentSubSystem.h"
#include "Strategy.h"
#include "json.hpp"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <variant>
#include "PortfolioSubsystem.h"

#define INIT_STRATEGY(classname) {\
    auto strategy = new classname();\
    _strategies[strategy->Name()] = strategy;\
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
    if (!std::filesystem::exists(SCRIPTS_DIR)) {
        std::filesystem::create_directories(SCRIPTS_DIR);
    }
    _featureSystem->InitSecondLvlFeatures();
    _featureSystem->Start();
    if (_handle->GetRunningMode() != RuningType::Backtest) {
        // 非回测模式加载当前路径下的策略
        for (const auto& entry : std::filesystem::directory_iterator(SCRIPTS_DIR)) {
            if (!entry.is_regular_file())
                continue;

            String strategy_path = entry.path();

            String strategyName = entry.path().filename();
            InstallStrategy(strategyName);
            Run(strategyName);
        }
    }
}

bool StrategySubSystem::Run(const String& strategy) {
    _agentSystem->Start(strategy);
    return true;
}

Set<symbol_t> StrategySubSystem::GetPools(const String& strategy) {
    return _agentSystem->GetPools(strategy);
}

void StrategySubSystem::Release() {
    if (_handle->GetRunningMode() != RuningType::Backtest) {
        for (auto& strategyName: _strategies) {
            UninstallStrategy(strategyName);
        }
    }
    _strategies.clear();
}

void StrategySubSystem::ReleaseStrategy(const String& strategy) {
    _agentSystem->ClearFlow(strategy);
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

Map<StatisticIndicator, std::variant<float, List<float>>>  StrategySubSystem::GetIndicators(const String& strategy) {
    return _agentSystem->GetCollection(strategy);
}

// AgentStrategyInfo StrategySubSystem::ParseJsonScript(const String& content) {
//     AgentStrategyInfo info;
//     nlohmann::json script_content = nlohmann::json::parse(content);
//     if (script_content.is_discarded()) {
//         WARN("script parse fail.");
//         return info;
//     }
//     return info;
// }

bool StrategySubSystem::CreateStrategy(const String& name, const nlohmann::json& params) {
    auto& features = params["feature"];
    auto& agent = params["agent"];
    _strategies.insert(name);
    return true;
}

bool StrategySubSystem::InstallStrategy(const String& strategyName) {
    std::ifstream ifs;
    String path(SCRIPTS_DIR);
    ifs.open(path + "/" + strategyName);
    if (!ifs)
        return false;

    try {
        nlohmann::json script;
        ifs >> script;
        InitStrategy(strategyName, script);
    } catch (const nlohmann::json::parse_error& e) {
        ifs.close();
        return false;
    }
    ifs.close();
    return true;
}

bool StrategySubSystem::UninstallStrategy(const String& strategy) {
    if (HasStrategy(strategy)) {
        DeleteStrategy(strategy);
    }
    return true;
}

void StrategySubSystem::Stop(const String& strategy) {
    _agentSystem->Stop(strategy);
}

void StrategySubSystem::Train(const String& name, const Vector<symbol_t>& history, DataFrequencyType freq) {
    // auto data = _handle->PrepareData({history.begin(), history.end()}, freq);
}

void StrategySubSystem::DeleteStrategy(const String& name) {
    _featureSystem->ErasePipeline(name);
    _strategies.erase(name);
}

void StrategySubSystem::InitStrategy(const String& strategy, const List<QNode*>& flow) {
    _agentSystem->LoadFlow(strategy, flow);
}
 
void StrategySubSystem::InitStrategy(const String& strategyName, const nlohmann::json& script) {
    auto nodes = parse_strategy_script_v2(script, _handle);
    auto sorted_nodes = topo_sort(nodes);
    InitStrategy(strategyName, sorted_nodes);
}
