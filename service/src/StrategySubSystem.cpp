#include "StrategySubSystem.h"
#include "BrokerSubSystem.h"
#include "ExchangeManager.h"
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
:_agentSystem(nullptr), _handle(server) {
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
    if (_handle->GetRunningMode() != RuningType::Backtest) {
        // 非回测模式加载当前路径下的策略
        int loadedCount = 0;
        int failedCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(SCRIPTS_DIR)) {
            if (!entry.is_regular_file())
                continue;

            String strategy_path = entry.path().string();
            String strategyName = entry.path().filename().string();

            INFO("[StrategySubSystem] Loading strategy: {} from {}", strategyName, strategy_path);

            if (InstallStrategy(strategyName)) {
                INFO("[StrategySubSystem] Strategy '{}' installed successfully", strategyName);
                Run(strategyName);
                INFO("[StrategySubSystem] Strategy '{}' started", strategyName);
                ++loadedCount;
            } else {
                WARN("[StrategySubSystem] Strategy '{}' failed to install", strategyName);
                ++failedCount;
            }
        }
        INFO("[StrategySubSystem] Strategy loading complete: {} succeeded, {} failed", loadedCount, failedCount);
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

        // 版本检查：如果 version 字段不存在或低于最低兼容版本，则拒绝加载
        if (!script.contains("version") || !script["version"].is_number()) {
            WARN("[StrategySubSystem] Strategy '{}' rejected: missing 'version' field", strategyName);
            ifs.close();
            return false;
        }

        int version = script["version"].get<int>();
        if (version < MIN_STRATEGY_VERSION) {
            WARN("[StrategySubSystem] Strategy '{}' rejected: version {} is below minimum required ({})",
                 strategyName, version, MIN_STRATEGY_VERSION);
            ifs.close();
            return false;
        }

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
    _strategies.erase(name);
    _strategyWarmupEpochs.erase(name);
}

void StrategySubSystem::InitStrategy(const String& strategy, const List<QNode*>& flow) {
    _agentSystem->LoadFlow(strategy, flow);
}
 
void StrategySubSystem::InitStrategy(const String& strategyName, const nlohmann::json& script) {
    // 版本检查
    if (!script.contains("version") || !script["version"].is_number()) {
        WARN("[StrategySubSystem] Strategy '{}' rejected: missing 'version' field", strategyName);
        return;
    }
    int version = script["version"].get<int>();
    if (version < MIN_STRATEGY_VERSION) {
        WARN("[StrategySubSystem] Strategy '{}' rejected: version {} is below minimum required ({})",
             strategyName, version, MIN_STRATEGY_VERSION);
        return;
    }

    INFO("[StrategySubSystem] Initializing strategy '{}' (version {})", strategyName, version);

    // 解析策略图，同时收集滑点配置和节点配置
    SlippageConfigInfo slippageConfig;
    std::map<uint32_t, nlohmann::json> nodeConfigMap;
    auto nodes = parse_strategy_script_v2(script, _handle, &slippageConfig, &nodeConfigMap);
    INFO("[StrategySubSystem] Strategy '{}' parsed: {} nodes created", strategyName, nodes.size());

    auto sorted_nodes = topo_sort(nodes);

    // 按拓扑顺序初始化节点（确保数据源节点先于下游节点初始化）
    for (auto* node: sorted_nodes) {
        auto itr = nodeConfigMap.find(node->id());
        if (itr != nodeConfigMap.end()) {
            node->Init(itr->second);
        }
    }

    InitStrategy(strategyName, sorted_nodes);

    // 配置滑点模型（从策略解析层提取的配置）
    if (!slippageConfig.sources.empty() && slippageConfig.modelConfig.is_object()) {
        _handle->GetExchangeManager()->ConfigureSlippageModels(slippageConfig.sources, slippageConfig.modelConfig);
        INFO("[StrategySubSystem] Strategy '{}' slippage models configured", strategyName);
    }

    // 解析策略级影子模式标志
    if (script.contains("shadow") && script["shadow"] == true) {
        _agentSystem->SetShadowMode(strategyName);
        INFO("[StrategySubSystem] Strategy '{}' shadow mode enabled", strategyName);
    }

    // 推断并保存预热期 epoch 数
    int warmup = InferWarmupEpochsFromConfig(script);
    _strategyWarmupEpochs[strategyName] = warmup;

    INFO("[StrategySubSystem] Strategy '{}' initialized: warmup={} epochs, nodes={}",
         strategyName, warmup, sorted_nodes.size());
}
