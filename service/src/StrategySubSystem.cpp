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
#include "Util/string_algorithm.h"
#include "Util/DailyDecision.h"
#include "Util/DecisionDB.h"
#include "Util/datetime.h"

namespace {
    // 从策略配置推断预热期 epoch 数（支持任意 Nx 格式：20d/60d/120d/250d 等）
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

        int freqSeconds = TimeStringToSeconds(inputFreq);
        if (freqSeconds <= 0) return maxWarmup;

        // 2. 遍历 Function 节点，计算最大 warmup
        for (auto& node : config["nodes"]) {
            String nodeType = node["data"]["nodeType"];
            if (nodeType != "function") continue;

            auto& params = node["data"]["params"];
            String range = params["range"]["value"];

            int rangeSeconds = TimeStringToSeconds(range);
            if (rangeSeconds <= 0) continue;  // 解析失败/格式异常 → 跳过

            // 计算需要的 epoch 数（向上取整）
            int epochs = (rangeSeconds + freqSeconds - 1) / freqSeconds;
            maxWarmup = std::max(maxWarmup, epochs);
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

            if (InstallStrategy(strategyName)) {
                Run(strategyName);
                ++loadedCount;
            } else {
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
    _agentSystem->Release();
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

List<String> StrategySubSystem::GetDailyStrategyNames() {
    List<String> names;
    for (auto& name: _strategies) {
    }
    return names;
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
    // auto& features = params["feature"];
    // auto& agent = params["agent"];
    _strategies.insert(name);
    return true;
}

bool StrategySubSystem::InstallStrategy(const String& strategyName) {
    std::ifstream ifs;
    String path(SCRIPTS_DIR);
    ifs.open(path + "/" + strategyName);
    if (!ifs) {
        StrategyInitResult result;
        result._errorMessage = "Failed to open strategy file";
        std::lock_guard<std::mutex> lock(_failureMtx);
        _failedStrategies[strategyName] = result;
        return false;
    }

    StrategyInitResult result;
    try {
        nlohmann::json script;
        ifs >> script;

        // 版本检查：如果 version 字段不存在或低于最低兼容版本，则拒绝加载
        if (!script.contains("version") || !script["version"].is_number()) {
            WARN("[StrategySubSystem] Strategy '{}' rejected: missing 'version' field", strategyName);
            ifs.close();
            result._errorMessage = "missing 'version' field";
            std::lock_guard<std::mutex> lock(_failureMtx);
            _failedStrategies[strategyName] = result;
            return false;
        }

        int version = script["version"].get<int>();
        if (version < MIN_STRATEGY_VERSION) {
            WARN("[StrategySubSystem] Strategy '{}' rejected: version {} is below minimum required ({})",
                 strategyName, version, MIN_STRATEGY_VERSION);
            ifs.close();
            result._errorMessage = fmt::format("version {} is below minimum required ({})",
                                               version, MIN_STRATEGY_VERSION);
            std::lock_guard<std::mutex> lock(_failureMtx);
            _failedStrategies[strategyName] = result;
            return false;
        }

        ifs.close();
        result = InitStrategy(strategyName, script);
    } catch (const std::exception& e) {
        ifs.close();
        WARN("[InstallStrategy] Unexpected exception for '{}': {}", strategyName, e.what());
        result._errorMessage = fmt::format("Unexpected exception: {}", e.what());
    } catch (...) {
        ifs.close();
        WARN("[InstallStrategy] Unknown exception for '{}'", strategyName);
        result._errorMessage = "Unknown exception during strategy install";
    }

    if (!result._success) {
        std::lock_guard<std::mutex> lock(_failureMtx);
        _failedStrategies[strategyName] = result;
        return false;
    }

    return true;
}

bool StrategySubSystem::UninstallStrategy(const String& strategy) {
    if (HasStrategy(strategy)) {
        DeleteStrategy(strategy);
    }
    _agentSystem->Stop(strategy);
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
    // 清理日级执行状态，避免同一进程内重新加载策略时残留已执行标记
    {
        std::lock_guard<std::mutex> lock(_dailyMtx);
        _dailyStrategySymbols.erase(name);
        _dailyExecutedStrategies.erase(name);
    }
    // 回收策略资金
    if (_handle) {
        auto* broker = _handle->GetBrokerSubSystem();
        if (broker) {
            auto* pool = broker->GetCapitalPool();
            if (pool) {
                double reclaimed = pool->reclaim(name);
                if (reclaimed > 0) {
                    INFO("[StrategySubSystem] Reclaimed {:.0f} capital from deleted strategy '{}'", reclaimed, name);
                }
            }
        }
    }
}

void StrategySubSystem::InitStrategy(const String& strategy, const List<QNode*>& flow) {
    _agentSystem->LoadFlow(strategy, flow);
}

StrategyInitResult StrategySubSystem::InitStrategy(const String& strategyName, const nlohmann::json& script) {
    StrategyInitResult result;

    // 版本检查
    if (!script.contains("version") || !script["version"].is_number()) {
        result._errorMessage = "missing 'version' field";
        WARN("[StrategySubSystem] Strategy '{}' rejected: missing 'version' field", to_utf8(strategyName));
        return result;
    }
    int version = script["version"].get<int>();
    if (version < MIN_STRATEGY_VERSION) {
        result._errorMessage = fmt::format("version {} is below minimum required ({})", version, MIN_STRATEGY_VERSION);
        WARN("[StrategySubSystem] Strategy '{}' rejected: version {} is below minimum required ({})",
             strategyName, version, MIN_STRATEGY_VERSION);
        return result;
    }

    // 解析策略图，同时收集滑点配置和节点配置
    SlippageConfigInfo slippageConfig;
    std::map<uint32_t, nlohmann::json> nodeConfigMap;
    auto nodes = parse_strategy_script_v2(script, _handle, &slippageConfig, &nodeConfigMap);

    auto sorted_nodes = topo_sort(nodes);

    // 按拓扑顺序初始化节点（确保数据源节点先于下游节点初始化）
    for (auto* node: sorted_nodes) {
        auto itr = nodeConfigMap.find(node->id());
        if (itr != nodeConfigMap.end()) {
            if (!node->Init(itr->second)) {
                String label = itr->second.value("label", "unknown");
                String nodeType = itr->second.value("nodeType", "unknown");
                String errMsg = fmt::format("Node '{}' (id={}, type={}) initialization failed",
                                            label, node->id(), nodeType);
                WARN("[InitStrategy] {}", errMsg);
                // 清理未接管的节点，避免内存泄漏
                // TODO: 后续可考虑对已成功 Init 的节点调用清理接口
                for (auto* n : sorted_nodes) {
                    delete n;
                }
                result._errorMessage = errMsg;
                result._failedNodeLabel = label;
                result._failedNodeType = nodeType;
                result._failedNodeId = node->id();
                return result;
            }
        }
    }

    InitStrategy(strategyName, sorted_nodes);

    // 配置滑点模型（从策略解析层提取的配置）
    if (!slippageConfig.sources.empty() && slippageConfig.modelConfig.is_object()) {
        _handle->GetExchangeManager()->ConfigureSlippageModels(slippageConfig.sources, slippageConfig.modelConfig);
    }

    // 解析策略级影子模式标志
    if (script.contains("shadow") && script["shadow"] == true) {
        _agentSystem->SetShadowMode(strategyName);
        INFO("[StrategySubSystem] Strategy '{}' shadow mode enabled", to_utf8(strategyName));
    }

    // 保存策略配置资金（供 StartDaily 实盘路径使用）
    if (script.contains("capital") && script["capital"].is_number()) {
        _agentSystem->SetStrategyCapital(strategyName, script["capital"].get<double>());
    }

    // 推断并保存预热期 epoch 数
    int warmup = InferWarmupEpochsFromConfig(script);
    _strategyWarmupEpochs[strategyName] = warmup;

    _strategies.insert(strategyName);

    // 成功加载：清除之前的失败记录
    ClearStrategyFailure(strategyName);

    INFO("[StrategySubSystem] Strategy '{}'(version {}) initialized: warmup={} epochs, nodes={}",
        to_utf8(strategyName), version, warmup, sorted_nodes.size());

    result._success = true;
    return result;
}

// ── 失败策略记录访问接口 ──

List<StrategyInitResult> StrategySubSystem::GetFailedStrategies() const {
    std::lock_guard<std::mutex> lock(_failureMtx);
    List<StrategyInitResult> result;
    for (auto& kv : _failedStrategies) {
        auto item = kv.second;
        item._name = kv.first;
        result.push_back(std::move(item));
    }
    return result;
}

std::optional<String> StrategySubSystem::GetStrategyFailureReason(const String& name) const {
    std::lock_guard<std::mutex> lock(_failureMtx);
    auto it = _failedStrategies.find(name);
    if (it == _failedStrategies.end()) {
        return std::nullopt;
    }
    return std::optional<String>(it->second._errorMessage);
}

void StrategySubSystem::ClearStrategyFailure(const String& name) {
    std::lock_guard<std::mutex> lock(_failureMtx);
    _failedStrategies.erase(name);
}

// ═══════════════════════════════════════════════════════════
//  日级策略执行（收盘后依赖驱动）
// ═══════════════════════════════════════════════════════════

void StrategySubSystem::EnsureDailyReady() {
    std::lock_guard<std::mutex> lock(_dailyMtx);

    String today = ToString(Now(), "%Y-%m-%d");
    if (today != _lastDailyDate) {
        _dailyReadySymbols.clear();
        _dailyExecutedStrategies.clear();
        _dailyErrors.clear();
        _lastDailyDate = today;
        _dailyInitialized = true;
        INFO("[DailyExecution] New trading day: {}, state reset", today);
    }

    // 增量注册：扫描 _strategies 中尚未注册的策略
    INFO("[DailyExecution] EnsureDailyReady: _strategies.size()={}, _dailyStrategySymbols.size()={}",
         _strategies.size(), _dailyStrategySymbols.size());
    for (auto& name : _strategies) {
        if (_dailyStrategySymbols.count(name)) continue;  // 已注册
        if (!_agentSystem->HasManualExecuteNode(name)) continue;
        auto pools = GetPools(name);
        Set<String> symbols;
        for (auto sym : pools) {
            symbols.insert(get_symbol(sym));
        }
        if (!symbols.empty()) {
            _dailyStrategySymbols[name] = symbols;
            String symList;
            for (auto& s : symbols) { if (!symList.empty()) symList += ","; symList += s; }
            INFO("[DailyExecution] Registered strategy '{}': [{}]", name, symList);
        }
    }
}

void StrategySubSystem::InitDailyExecution() {
    std::lock_guard<std::mutex> lock(_dailyMtx);

    _dailyStrategySymbols.clear();

    // 从所有已加载策略中提取依赖标的（只注册日终策略，含 Manual ExecuteNode）
    for (auto& name : _strategies) {
        if (!_agentSystem->HasManualExecuteNode(name)) continue;
        auto pools = GetPools(name);
        Set<String> symbols;
        for (auto sym : pools) {
            symbols.insert(get_symbol(sym));
        }
        if (!symbols.empty()) {
            _dailyStrategySymbols[name] = symbols;
        }
    }

    _dailyInitialized = true;
    INFO("[DailyExecution] Initialized: {} strategies with symbols", _dailyStrategySymbols.size());
    for (auto& [strategy, symbols] : _dailyStrategySymbols) {
        String symList;
        for (auto& s : symbols) {
            if (!symList.empty()) symList += ",";
            symList += s;
        }
        INFO("[DailyExecution]   {} → [{}]", strategy, symList);
    }
}

void StrategySubSystem::ResetDaily() {
    std::lock_guard<std::mutex> lock(_dailyMtx);
    _dailyReadySymbols.clear();
    _dailyExecutedStrategies.clear();
    _dailyErrors.clear();
    INFO("[DailyExecution] Reset daily state");
}

void StrategySubSystem::MarkSymbolReady(const String& symbol, const String& simDate) {
    std::unique_lock<std::mutex> lock(_dailyMtx);

    if (!_dailyInitialized) {
        WARN("[DailyExecution] MarkSymbolReady: not initialized, ignoring {}", symbol);
        return;
    }

    _dailyReadySymbols.insert(symbol);
    // INFO("[DailyExecution] Symbol ready: {} ({}/{} ready symbols)",
    //      symbol, _dailyReadySymbols.size(), _dailyStrategySymbols.size());

    // 诊断：打印所有已注册策略及其 symbol 列表
    // for (auto& [strat, syms] : _dailyStrategySymbols) {
    //     String symList;
    //     for (auto& s : syms) { if (!symList.empty()) symList += ","; symList += s; }
    //     bool executed = _dailyExecutedStrategies.count(strat) > 0;
    //     INFO("[DailyExecution]   registered strategy '{}': symbols=[{}], already_executed={}", strat, symList, executed);
    // }

    Vector<std::pair<String, String>> strategiesToRun;

    // 检查是否有策略的所有依赖已就绪
    for (auto& [strategy, symbols] : _dailyStrategySymbols) {
        if (_dailyExecutedStrategies.count(strategy)) continue;

        bool allReady = true;
        for (auto& sym : symbols) {
            if (!_dailyReadySymbols.count(sym)) {
                allReady = false;
                INFO("[DailyExecution]   strategy '{}': symbol '{}' not ready yet", strategy, sym);
                break;
            }
        }

        if (allReady) {
            INFO("[DailyExecution] All symbols ready for strategy '{}', executing", strategy);
            _dailyExecutedStrategies.insert(strategy);

            // 收集需要执行的策略，在锁外异步启动（StartDaily 是重量级同步操作）
            strategiesToRun.push_back({strategy, simDate});
        }
    }

    if (!strategiesToRun.empty()) {
        lock.unlock();  // 先释放锁，再 detach 线程
        for (auto& [strat, date] : strategiesToRun) {
            std::thread([this, strat, date]() {
                ExecuteDailyStrategy(strat, date);
            }).detach();
        }
    }
}

void StrategySubSystem::ExecuteDailyStrategy(const String& strategy, const String& simDate) {
    INFO("[DailyExecution] Executing strategy: {}", strategy);

    auto pools = GetPools(strategy);
    String dataDir = _handle->GetConfig().GetDatabasePath();
    String today = simDate.empty() ? ToString(Now(), "%Y-%m-%d") : simDate;

    // 异步执行，回调中保存决策
    _agentSystem->StartDaily(strategy, pools,
        [this, strategy, dataDir, today](nlohmann::json decisions) {
            INFO("[DailyExecution] Callback for '{}': status={}, keys={}",
                 strategy, decisions.value("status", "unknown"),
                 decisions.dump().size() > 200 ? decisions.dump().substr(0, 200) : decisions.dump());

            DailyDecisionJson::Report report;
            report.strategy = strategy;
            report.executed_at = ToString(Now(), "%Y-%m-%d %H:%M:%S");
            report.status = decisions.value("status", "unknown");

            // 解析决策
            if (decisions.contains("decisions")) {
                for (auto& d : decisions["decisions"]) {
                    DailyDecisionJson::Decision decision;
                    decision.symbol = d.value("symbol", "");
                    decision.action = DailyDecisionJson::parseAction(d.value("action", "HOLD"));
                    decision.quantity = d.value("quantity", 0);
                    decision.target_price = d.value("price", 0.0);
                    report.decisions.push_back(decision);
                }
            }

            DailyDecisionJson::saveReport(dataDir, today, report);
            INFO("[DailyExecution] Strategy {} completed: {} decisions saved",
                 strategy, report.decisions.size());

            // 记录持仓快照到 DuckDB
            time_t todayTs = FromStr(today, "%Y-%m-%d");
            recordDailyPositions(strategy, report.decisions, todayTs);

            // 回收日终执行分配的临时资金（createBacktestContext 分配的 CapitalPool 额度）
            if (_handle) {
                auto* broker = _handle->GetBrokerSubSystem();
                if (broker) {
                    auto* pool = broker->GetCapitalPool();
                    if (pool) {
                        double reclaimed = pool->reclaim(strategy);
                        if (reclaimed > 0) {
                            INFO("[DailyExecution] Reclaimed {:.0f} capital from strategy '{}'", reclaimed, strategy);
                        }
                    }
                }
            }

            // 收集错误 + 检查是否全部完成，统一发送通知
            std::lock_guard<std::mutex> lock(_dailyMtx);
            if (report.status == "error") {
                String errMsg = strategy + ": " + decisions.value("error", "unknown error");
                _dailyErrors.push_back(errMsg);
            }

            if (_dailyExecutedStrategies.size() >= _dailyStrategySymbols.size() && !_dailyErrors.empty()) {
                String body = "[日终策略执行异常]\n\n";
                for (auto& e : _dailyErrors) {
                    body += "  - " + e + "\n";
                }
                body += "\n请检查行情数据是否已正确导入。";
                if (_handle) _handle->SendEmail(body);
                _dailyErrors.clear();
            }
        });
}

void StrategySubSystem::ForceExecuteAllDaily() {
    std::lock_guard<std::mutex> lock(_dailyMtx);

    if (!_dailyInitialized) return;

    for (auto& [strategy, symbols] : _dailyStrategySymbols) {
        if (_dailyExecutedStrategies.count(strategy)) continue;

        WARN("[DailyExecution] Force executing strategy '{}' (not all symbols ready)", strategy);
        _dailyExecutedStrategies.insert(strategy);

        // StartDaily 内部异步执行
        ExecuteDailyStrategy(strategy);
    }
}

nlohmann::json StrategySubSystem::GetDailyStatus() const {
    std::lock_guard<std::mutex> lock(_dailyMtx);

    nlohmann::json status;
    status["initialized"] = _dailyInitialized;
    status["ready_symbols"] = nlohmann::json::array();
    status["executed_strategies"] = nlohmann::json::array();
    status["pending_strategies"] = nlohmann::json::array();

    for (auto& sym : _dailyReadySymbols) {
        status["ready_symbols"].push_back(sym);
    }
    for (auto& s : _dailyExecutedStrategies) {
        status["executed_strategies"].push_back(s);
    }
    for (auto& [strategy, symbols] : _dailyStrategySymbols) {
        if (!_dailyExecutedStrategies.count(strategy)) {
            nlohmann::json pending;
            pending["strategy"] = strategy;
            pending["required_symbols"] = nlohmann::json::array();
            pending["ready_count"] = 0;
            for (auto& sym : symbols) {
                pending["required_symbols"].push_back(sym);
                if (_dailyReadySymbols.count(sym)) {
                    pending["ready_count"] = pending["ready_count"].get<int>() + 1;
                }
            }
            pending["total_count"] = static_cast<int>(symbols.size());
            status["pending_strategies"].push_back(pending);
        }
    }

    return status;
}

void StrategySubSystem::recordDailyPositions(
    const String& strategy,
    const std::vector<DailyDecisionJson::Decision>& decisions,
    time_t date) {

    auto& positions = _strategyPositions[strategy];

    for (const auto& d : decisions) {
        symbol_t sym = to_symbol(d.symbol);

        // 更新持仓
        if (d.action == DailyDecisionJson::Action::BUY) {
            positions[sym] += d.quantity;
        } else if (d.action == DailyDecisionJson::Action::SELL ||
                   d.action == DailyDecisionJson::Action::CLOSE) {
            positions[sym] = 0;
        }
        // HOLD: 持仓不变

        // 写入 DuckDB 快照
        DailyPositionRecord rec;
        rec.strategy = strategy;
        rec.symbol = sym;
        rec.date = date;
        rec.position = positions[sym];
        rec.close_price = d.target_price;
        DecisionDB::instance().insertDailyPosition(rec);
    }

    INFO("[DailyExecution] Recorded {} position(s) for strategy '{}' on {}",
         decisions.size(), strategy, ToString(date, "%Y-%m-%d"));
}
