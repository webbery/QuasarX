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

namespace {
    // timeHorizon 映射：统一转换为秒（用于计算 warmup epoch 数）
    static const Map<String, int> timeHorizonSeconds{
        {"6s", 6}, {"30s", 30}, {"1m", 60}, {"5m", 300}, {"1h", 3600},
        {"1d", 86400}, {"3d", 259200}, {"5d", 432000},
    };

    // 从策略配置推断预热期 epoch 数
    int InferWarmupEpochsFromConfig(const nlohmann::json& config) {
        int maxWarmup = 0;

        if (!config.contains("nodes")) return maxWarmup;

        // 1. 找到 Input 节点的 freq
        String inputFreq;
        for (auto& node : config["nodes"]) {
            String nodeType = node["data"].value("nodeType", "");
            if (nodeType == "input") {
                auto& params = node["data"]["params"];
                inputFreq = (String)params["freq"]["value"];
                break;
            }
        }

        if (inputFreq.empty() || !timeHorizonSeconds.count(inputFreq)) {
            return maxWarmup;
        }

        int freqSeconds = timeHorizonSeconds.at(inputFreq);

        // 2. 遍历 Function 节点，计算最大 warmup
        for (auto& node : config["nodes"]) {
            String nodeType = node["data"]["nodeType"];
            if (nodeType != "function") continue;

            auto& params = node["data"]["params"];
            String range = params["range"]["value"];

            if (timeHorizonSeconds.count(range)) {
                int rangeSeconds = timeHorizonSeconds.at(range);
                // 计算需要的 epoch 数（向上取整）
                int epochs = (rangeSeconds + freqSeconds - 1) / freqSeconds;
                maxWarmup = std::max(maxWarmup, epochs);
            }
        }

        return maxWarmup;
    }
}

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

            String strategy_path = entry.path().string();

            String strategyName = entry.path().filename().string();
            InstallStrategy(strategyName);
            Run(strategyName);
        }
    }
}

bool StrategySubSystem::Run(const String& strategy) {
    auto strategySys = _handle->GetStrategySystem();
    auto symbols = strategySys->GetPools(strategy);
    _agentSystem->Start(strategy, symbols);
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

int StrategySubSystem::GetWarmupEpochs(const String& strategy) const {
    auto it = _strategyWarmupEpochs.find(strategy);
    return it != _strategyWarmupEpochs.end() ? it->second : 0;
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
    _strategyWarmupEpochs.erase(name);
}

void StrategySubSystem::InitStrategy(const String& strategy, const List<QNode*>& flow) {
    _agentSystem->LoadFlow(strategy, flow);
}
 
void StrategySubSystem::InitStrategy(const String& strategyName, const nlohmann::json& script) {
    auto nodes = parse_strategy_script_v2(script, _handle);
    auto sorted_nodes = topo_sort(nodes);
    InitStrategy(strategyName, sorted_nodes);
    
    // 推断并保存预热期 epoch 数
    int warmup = InferWarmupEpochsFromConfig(script);
    _strategyWarmupEpochs[strategyName] = warmup;
    
    if (warmup > 0) {
        INFO("[StrategySubSystem] Inferred warmup for '{}': {} epochs", strategyName, warmup);
    }
}
