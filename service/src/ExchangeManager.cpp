#include "ExchangeManager.h"
#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/HX/HXExchange.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "Bridge/SIM/RealTimeSimulation.h"
#include "Bridge/TickFlow/TickFlowBridge.h"
#include "Bridge/SlippageModel.h"
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "Util/log.h"
#include "server.h"
#include "Util/string_algorithm.h"

ExchangeManager::ExchangeManager(Server* server)
    : _server(server), _quotePubSock{0} {
}

ExchangeManager::~ExchangeManager() {
    Shutdown();
}

// ========== 初始化 ==========

bool ExchangeManager::InitQuotePublisher() {
    if (_quotePubInited) return true;

    if (!Publish(URI_RAW_QUOTE, _quotePubSock)) {
        FATAL("Failed to create quote publisher");
        return false;
    }
    _quotePubInited = true;
    INFO("Quote publisher initialized");
    return true;
}

// ========== Exchange 生命周期管理 ==========

bool ExchangeManager::Use(const String& name) {
    auto& config = _server->GetConfig();
    auto exchangeCfg = config.GetExchangeByName(name);
    if (exchangeCfg.empty()) {
        WARN("Exchange config not found: {}", name);
        return false;
    }

    String api = exchangeCfg["api"];
    ExchangeType type = ExchangeType::EX_Unknow;

    if (api == CTP_API)              type = ExchangeType::EX_CTP;
    else if (api == STOCK_HISTORY_SIM)  type = ExchangeType::EX_STOCK_HIST_SIM;
    else if (api == ETF_HISTORY_SIM)    type = ExchangeType::EX_ETF_HIST_SIM;
    else if (api == STOCK_REAL_SIM)     type = ExchangeType::EX_STOCK_REAL_SIM;
    else if (api == HX_API)             type = ExchangeType::EX_HX;
    else if (api == TICKFLOW_QUOTE_API) type = ExchangeType::EX_TICKFLOW_QUOTE;
    else {
        WARN("Unsupported exchange api: {}", api);
        return false;
    }

    return RegisterExchange(name, type);
}

// ========== Exchange 生命周期管理 ==========

bool ExchangeManager::RegisterExchange(const String& name, ExchangeType type) {
    // 避免重复注册
    if (_exchanges.count(name)) {
        WARN("Exchange {} already registered", name);
        return true;
    }

    auto exchangeCfg = GetExchangeInfo(name);
    ExchangeInterface* ptr = nullptr;
    bool ret = false;

    // 根据 API 类型创建对应的 Exchange 实例
    if (type == ExchangeType::EX_CTP) {
        ptr = new CTPExchange(_server);
        ret = ptr->Init(exchangeCfg);
        if (ret && !ptr->Login()) {
            WARN("CTP login failed for {}", name);
            delete ptr;
            return false;
        }
    }
    else if (type == ExchangeType::EX_HX) {
        ptr = new HXExchange(_server);
        ret = ptr->Init(exchangeCfg);
        if (ret && !ptr->Login()) {
            WARN("HX login failed for {}", name);
            delete ptr;
            return false;
        }
    }
    else if (type == EX_STOCK_HIST_SIM) {
        ptr = new StockHistorySimulation(_server);
        ret = ptr->Init(exchangeCfg);
        _enableSimulation = true;
    }
    else if (type == EX_ETF_HIST_SIM) {
        ptr = new ETFHistorySimulation(_server);
        ret = ptr->Init(exchangeCfg);
        _enableSimulation = true;
    }
    else if (type == EX_STOCK_REAL_SIM) {
        ptr = new RealTimeSimulation(_server);
        ret = ptr->Init(exchangeCfg);
    }
    else if (type == EX_TICKFLOW_QUOTE) {
        ptr = new TickFlowBridge(_server);
        ret = ptr->Init(exchangeCfg);
    }
    else {
        WARN("Unknown exchange type: {}", (int)type);
        return false;
    }

    if (!ret || !ptr) {
        delete ptr;
        return false;
    }

    // 注册到容器
    _exchanges[name] = ptr;
    _typeExchanges[type] = ptr;

    // 同步合约符号信息到 Server
    List<SymbolInfo> symbols;
    bool stockOk = ptr->GetAllStockSymbols(symbols);
    bool fundOk = ptr->GetAllFundSymbols(symbols);
    bool optionOk = ptr->GetAllOptionSymbols(symbols);

    if (!stockOk && !fundOk && !optionOk) {
        // 所有符号加载都失败 → 注册失败
        WARN("Exchange {} registered but no symbols loaded (stock/fund/option all failed)", name);
        delete ptr;
        _exchanges.erase(name);
        _typeExchanges.erase(type);
        return false;
    }

    // 部分成功：至少有一种符号加载成功
    for (auto& sym : symbols) {
        ContractInfo info;
        info._type = sym._type;
        info._exchange = sym._exchange;
        info._name = sym._name;
        info._market = sym._market;
        info._expireDate = sym._expireDate;
        info._deliveryDate = sym._deliveryDate;
        info._strike = sym._strike;
        _server->AddSymbolToMarket(sym._code, std::move(info));
    }
    if (!symbols.empty()) {
        INFO("Loaded {} symbols from exchange {}", symbols.size(), name);
    } else {
        WARN("Exchange {} registered with 0 symbols (Bridge mode, symbols provided by config pool)", name);
    }

    // 设置过滤条件和工作时间
    auto config = _server->GetConfig().GetExchangeByName(name);
    QuoteFilter filter;
    if (config.contains("pool")) {
        for (String symbol : config["pool"]) {
            filter._symbols.insert(symbol);
        }
    }
    ptr->SetFilter(filter);

    // 设置工作时间段
    if (config.contains("utc_active")) {
        for (auto& range : config["utc_active"]) {
            String working_range = range;
            Vector<String> time_range;
            split(working_range, time_range, "-");
            if (time_range.size() < 2) break;

            Vector<String> components;
            split(time_range[0], components, ":");
            if (components.size() < 2) break;
            auto start_hour = (char)atoi(components[0].c_str());
            auto start_minute = (char)atoi(components[1].c_str());

            components.clear();
            split(time_range[1], components, ":");
            if (components.size() < 2) break;
            auto stop_hour = (char)atoi(components[0].c_str());
            auto stop_minute = (char)atoi(components[1].c_str());

            ptr->SetWorkingRange(start_hour, stop_hour, start_minute, stop_minute);
        }
    }

    INFO("Exchange {} registered successfully (type: {})", name, (int)type);
    return true;
}

void ExchangeManager::RegisterExchangePtr(const String& name, ExchangeType type, ExchangeInterface* ptr) {
    // 避免重复注册
    if (_exchanges.count(name)) {
        WARN("Exchange {} already registered in ExchangeManager", name);
        return;
    }

    _exchanges[name] = ptr;
    _typeExchanges[type] = ptr;
    // 符号加载已由 RegisterExchange() 处理，这里只注册实例
}

void ExchangeManager::UnregisterExchange(const String& name) {
    auto itr = _exchanges.find(name);
    if (itr == _exchanges.end()) return;

    ExchangeInterface* ptr = itr->second;
    ptr->Release();

    // 从类型索引中移除
    for (auto typeItr = _typeExchanges.begin(); typeItr != _typeExchanges.end(); ) {
        if (typeItr->second == ptr) {
            typeItr = _typeExchanges.erase(typeItr);
        } else {
            ++typeItr;
        }
    }

    _exchanges.erase(itr);
    INFO("Exchange {} unregistered", name);
}

void ExchangeManager::Shutdown() {
    for (auto& [name, exch] : _exchanges) {
        exch->Release();
    }
    _exchanges.clear();
    _typeExchanges.clear();
    _exchangeRefCounts.clear();

    if (_quotePubInited) {
        nng_close(_quotePubSock);
        _quotePubInited = false;
    }
}

// ========== 查询接口 ==========

ExchangeInterface* ExchangeManager::GetExchange(const String& name) const {
    auto itr = _exchanges.find(name);
    if (itr != _exchanges.end()) {
        return itr->second;
    }
    return nullptr;
}

const Map<String, ExchangeInterface*>& ExchangeManager::GetAllExchanges() const {
    return _exchanges;
}

const Map<ExchangeType, ExchangeInterface*>& ExchangeManager::GetExchangesByType() const {
    return _typeExchanges;
}

Vector<ExchangeInterface*> ExchangeManager::GetActiveExchanges() const {
    Vector<ExchangeInterface*> result;
    for (auto& [type, exch] : _typeExchanges) {
        if (exch) {
            result.push_back(exch);
        }
    }
    return result; 
}

ExchangeInterface* ExchangeManager::GetExchangeByType(ExchangeType type) const {
    if (_enableSimulation && type != ExchangeType::EX_STOCK_HIST_SIM && type != ExchangeType::EX_ETF_HIST_SIM) {
        // 模拟模式下强制返回仿真环境（非 ETF 类型）
        auto simItr = _typeExchanges.find(ExchangeType::EX_STOCK_HIST_SIM);
        if (simItr != _typeExchanges.end()) {
            return simItr->second;
        }
    }
    auto itr = _typeExchanges.find(type);
    if (itr != _typeExchanges.end()) {
        return itr->second;
    }
    return nullptr;
}

bool ExchangeManager::EnsureExchangeByType(ExchangeType type) {
    // 已存在，直接返回
    if (_typeExchanges.count(type)) {
        return true;
    }

    // 从 config.json 查找对应 API 类型的 exchange
    String apiName;
    String configName;
    String quoteAddr = "data";  // 默认数据目录

    if (type == EX_STOCK_HIST_SIM) {
        apiName = STOCK_HISTORY_SIM;
    } else if (type == EX_ETF_HIST_SIM) {
        apiName = ETF_HISTORY_SIM;
    } else {
        WARN("EnsureExchangeByType: unsupported type {}", (int)type);
        return false;
    }

    // 遍历 config.json 中的 exchange 数组，找到匹配 api 的 exchange
    auto exchanges = _server->GetConfig().GetExchanges();
    for (auto& ex : exchanges) {
        String api = ex.value("api", "");
        if (api == apiName) {
            configName = ex.value("name", "");
            quoteAddr = ex.value("quote", "data");
            break;
        }
    }

    // 如果配置中没有，使用默认名称
    if (configName.empty()) {
        WARN("Exchange type {} (api={}) not found in config, using default name '{}'",
             (int)type, apiName, configName);
        return false;
    }

    // 构造 ExchangeInfo
    ExchangeInfo info;
    memset(&info, 0, sizeof(info));
    strncpy(info._quote_addr, quoteAddr.c_str(), sizeof(info._quote_addr) - 1);

    ExchangeInterface* ptr = nullptr;
    bool ret = false;

    if (type == EX_STOCK_HIST_SIM) {
        ptr = new StockHistorySimulation(_server);
        ret = ptr->Init(info);
        _enableSimulation = true;
    } else if (type == EX_ETF_HIST_SIM) {
        ptr = new ETFHistorySimulation(_server);
        ret = ptr->Init(info);
        _enableSimulation = true;
    }

    if (!ret || !ptr) {
        delete ptr;
        return false;
    }

    RegisterExchangePtr(configName, type, ptr);
    INFO("Auto-created exchange '{}' (type={}, api={})", configName, (int)type, apiName);
    return true;
}

ExchangeInterface* ExchangeManager::GetActiveStockExchange() const {
    if (_activeStockName.empty()) return nullptr;
    auto itr = _exchanges.find(_activeStockName);
    return (itr != _exchanges.end()) ? itr->second : nullptr;
}

ExchangeInterface* ExchangeManager::GetActiveFutureExchange() const {
    if (_activeFutureName.empty()) return nullptr;
    auto itr = _exchanges.find(_activeFutureName);
    return (itr != _exchanges.end()) ? itr->second : nullptr;
}

// ========== 交易相关（持仓/订单） ==========

bool ExchangeManager::GetTradingPosition(AccountPosition& outPosition) const {
    bool found = false;
    for (auto& [name, exch] : _exchanges) {
        if (!exch) continue;
        // 通过 _typeExchanges 反查 Exchange 类型
        ExchangeType type = ExchangeType::EX_Unknow;
        for (auto& [t, e] : _typeExchanges) {
            if (e == exch) { type = t; break; }
        }
        // 排除历史回测和纯行情 Bridge，只取真实交易 Exchange
        if (type == ExchangeType::EX_STOCK_HIST_SIM ||
            type == ExchangeType::EX_ETF_HIST_SIM ||
            type == ExchangeType::EX_TICKFLOW_QUOTE) {
            continue;
        }
        AccountPosition ap;
        if (exch->GetPosition(ap) && !ap._positions.empty()) {
            outPosition._positions.insert(outPosition._positions.end(),
                                          ap._positions.begin(), ap._positions.end());
            found = true;
        }
    }
    return found;
}

bool ExchangeManager::GetTradingOrders(SecurityType secType, OrderList& outOrders) const {
    bool found = false;
    for (auto& [name, exch] : _exchanges) {
        if (!exch) continue;
        ExchangeType type = ExchangeType::EX_Unknow;
        for (auto& [t, e] : _typeExchanges) {
            if (e == exch) { type = t; break; }
        }
        if (type == ExchangeType::EX_STOCK_HIST_SIM ||
            type == ExchangeType::EX_ETF_HIST_SIM ||
            type == ExchangeType::EX_TICKFLOW_QUOTE) {
            continue;
        }
        OrderList ol;
        if (exch->GetOrders(secType, ol) && !ol.empty()) {
            outOrders.insert(outOrders.end(), ol.begin(), ol.end());
            found = true;
        }
    }
    return found;
}

// ========== 行情发布 ==========

void ExchangeManager::PublishQuote(const void* data, size_t size) {
    if (!_quotePubInited) return;

    std::lock_guard<std::mutex> lock(_quotePubMtx);
    if (0 != nng_send(_quotePubSock, const_cast<void*>(data), size, 0)) {
        WARN("Failed to publish quote");
    }
}

// ========== 交易路由 ==========

double ExchangeManager::Buy(const String& strategy, symbol_t symbol,
                            const Order& order, TradeInfo& deals) {
    auto broker = _server->GetBrokerSubSystem();
    if (!broker) {
        WARN("BrokerSubSystem not initialized");
        return 0;
    }
    broker->Buy(strategy, symbol, order, deals);
    return 0;
}

double ExchangeManager::Sell(const String& strategy, symbol_t symbol,
                             const Order& order, TradeInfo& deals) {
    auto broker = _server->GetBrokerSubSystem();
    if (!broker) {
        WARN("BrokerSubSystem not initialized");
        return 0;
    }
    broker->Sell(strategy, symbol, order, deals);
    return 0;
}

// ========== 定时任务 ==========

void ExchangeManager::UpdateQuoteQueryStatus(time_t curr) {
    for (auto& [name, exch] : _exchanges) {
        // 跳过模拟回测
        if (strcmp(exch->Name(), STOCK_HISTORY_SIM) == 0) {
            continue;
        }
        if (exch->IsWorking(curr)) {
            if (exch->IsLogin()) {
                exch->QueryQuotes();
            } else {
                exch->Login();
            }
        } else {
            exch->StopQuery();
        }
    }
}

// ========== 私有方法 ==========

ExchangeInfo ExchangeManager::GetExchangeInfo(const String& name) const {
    return _server->GetExchangeInfo(name);
}

bool ExchangeManager::IsSimulationEnabled() const {
    return _enableSimulation;
}

// ========== 多回测引擎协调 ==========

Vector<ExchangeInterface*> ExchangeManager::GetExchangesByTypes(const Vector<ExchangeType>& types) const {
    Vector<ExchangeInterface*> result;
    for (auto type : types) {
        auto itr = _typeExchanges.find(type);
        if (itr != _typeExchanges.end()) {
            result.push_back(itr->second);
        }
    }
    return result;
}

ExchangeInterface* ExchangeManager::ResolveExchange(const symbol_t& symbol) const {
    // 基金类标的优先走 ETFExchange
    if (is_fund(symbol) || is_etf(symbol)) {
        auto itr = _typeExchanges.find(ExchangeType::EX_ETF_HIST_SIM);
        if (itr != _typeExchanges.end()) {
            return itr->second;
        }
        // 没有 ETFExchange 时回退到 StockExchange
        itr = _typeExchanges.find(ExchangeType::EX_STOCK_HIST_SIM);
        if (itr != _typeExchanges.end()) {
            return itr->second;
        }
    }
    // 股票类走 StockExchange
    auto itr = _typeExchanges.find(ExchangeType::EX_STOCK_HIST_SIM);
    if (itr != _typeExchanges.end()) {
        return itr->second;
    }
    // 最后回退到 EX_STOCK_HIST_SIM
    itr = _typeExchanges.find(ExchangeType::EX_STOCK_HIST_SIM);
    if (itr != _typeExchanges.end()) {
        return itr->second;
    }
    return nullptr;
}

// ========== 策略级生命周期 ==========

bool ExchangeManager::StartExchange(const String& name) {
    auto* exch = GetExchange(name);
    if (!exch) {
        WARN("Exchange {} not registered", name);
        return false;
    }

    int& refCount = _exchangeRefCounts[name];

    if (refCount > 0) {
        refCount++;
        INFO("Exchange {} already running, ref count -> {}", name, refCount);
        return true;
    }

    if (!exch->Login(AccountType::MAIN)) {
        WARN("Exchange {} login failed", name);
        return false;
    }

    refCount = 1;
    INFO("Exchange {} started (ref count = 1)", name);
    return true;
}

bool ExchangeManager::StopExchange(const String& name) {
    auto itr = _exchangeRefCounts.find(name);
    if (itr == _exchangeRefCounts.end() || itr->second <= 0) {
        return true;  // 未运行或已停止
    }

    auto* exch = GetExchange(name);
    if (!exch) return false;

    itr->second--;

    if (itr->second > 0) {
        INFO("Exchange {} still in use, ref count -> {}", name, itr->second);
        return true;
    }

    // 引用计数归零，真正停止
    exch->StopQuery();
    exch->Logout(AccountType::MAIN);
    _exchangeRefCounts.erase(itr);
    INFO("Exchange {} stopped (ref count = 0)", name);
    return true;
}

Set<String> ExchangeManager::ResolveExchangeNames(const Set<String>& requiredSources) {
    Set<String> names;

    for (auto& [exName, exch] : _exchanges) {
        bool needStart = false;
        String apiName = exch->Name();

        for (auto& source : requiredSources) {
            if (source == "股票") {
                // 根据运行模式决定：回测用历史数据，实盘用真实数据
                if (apiName == STOCK_HISTORY_SIM || apiName == HX_API ||
                    apiName == STOCK_REAL_SIM) {
                    needStart = true;
                }
            }
            else if (source == "ETF") {
                if (apiName == ETF_HISTORY_SIM || apiName == STOCK_REAL_SIM) {
                    needStart = true;
                }
            }
        }

        if (needStart) {
            names.insert(exName);
        }
    }

    return names;
}

bool ExchangeManager::StartRequiredExchanges(const Set<String>& requiredSources) {
    auto names = ResolveExchangeNames(requiredSources);
    bool anySuccess = false;

    for (auto& name : names) {
        if (StartExchange(name)) {
            anySuccess = true;
        }
    }
    return anySuccess;
}

void ExchangeManager::StopRequiredExchanges(const Set<String>& requiredSources) {
    auto names = ResolveExchangeNames(requiredSources);
    for (auto& name : names) {
        StopExchange(name);
    }
}

void ExchangeManager::StopAllExchanges() {
    // 复制名称列表，避免在迭代中修改集合
    Vector<String> runningNames;
    for (auto& [name, count] : _exchangeRefCounts) {
        if (count > 0) {
            runningNames.push_back(name);
        }
    }

    for (auto& name : runningNames) {
        StopExchange(name);
    }
}

// 保留旧方法以兼容（内部委托给新方法）
bool ExchangeManager::LoginExchanges(const Set<String>& requiredSources) {
    return StartRequiredExchanges(requiredSources);
}

void ExchangeManager::LogoutExchanges() {
    StopAllExchanges();
}

bool ExchangeManager::StepAllHistoryExchanges(run_id_t runId) {
    bool anyMoreData = false;

    for (auto& [name, exch] : _exchanges) {
        auto* base = dynamic_cast<HistorySimulationBase*>(exch);
        if (!base) continue;

        auto* ctx = base->getBacktestContext(runId);
        if (!ctx || ctx->isFinished()) continue;

        if (base->stepForward(ctx)) {
            anyMoreData = true;
        }
    }
    return anyMoreData;
}

run_id_t ExchangeManager::CreateMultiContext(const String& strategy,
                                              const Set<symbol_t>& symbols,
                                              double initialCapital) {
    // 按标的类型分组
    Set<symbol_t> stockSymbols, etfSymbols;
    for (auto sym : symbols) {
        if (is_fund(sym) || is_etf(sym)) {
            etfSymbols.insert(sym);
        } else {
            stockSymbols.insert(sym);
        }
    }

    // 主 Exchange 分配 runId
    run_id_t mainRunId = 0;

    // 创建股票回测上下文
    if (!stockSymbols.empty()) {
        auto itr = _typeExchanges.find(ExchangeType::EX_STOCK_HIST_SIM);
        if (itr != _typeExchanges.end()) {
            auto* stockExch = dynamic_cast<HistorySimulationBase*>(itr->second);
            if (stockExch) {
                mainRunId = stockExch->createBacktestContext(strategy, stockSymbols, initialCapital);
            }
        }
    }

    // 创建 ETF 回测上下文
    if (!etfSymbols.empty()) {
        auto itr = _typeExchanges.find(ExchangeType::EX_ETF_HIST_SIM);
        if (itr != _typeExchanges.end()) {
            auto* etfExch = dynamic_cast<HistorySimulationBase*>(itr->second);
            if (etfExch) {
                // ETF Exchange 独立创建上下文（使用自己的 runId 计数器）
                run_id_t etfRunId = etfExch->createBacktestContext(strategy, etfSymbols, initialCapital);
                if (mainRunId == 0) {
                    // 如果没有股票标的，以 ETF 的 runId 为主 runId
                    mainRunId = etfRunId;
                }
            }
        }
    }

    return mainRunId;
}

double ExchangeManager::GetTotalAvailableFunds(run_id_t runId) const {
    double total = 0.0;
    bool found = false;

    for (auto& [name, exch] : _exchanges) {
        auto* base = dynamic_cast<HistorySimulationBase*>(exch);
        if (!base) continue;

        auto* ctx = base->getBacktestContext(runId);
        if (ctx) {
            total += ctx->getAvailableFunds();
            found = true;
        }
    }
    return found ? total : BACKTEST_INITIAL_CAPITAL;
}

void ExchangeManager::ConfigureETFExchange() {
    auto itr = _typeExchanges.find(ExchangeType::EX_ETF_HIST_SIM);
    if (itr == _typeExchanges.end()) return;

    auto* etfExch = dynamic_cast<ETFHistorySimulation*>(itr->second);
    if (!etfExch) return;

    Set<String> t0Codes, t1Codes;
    const auto& rawConfig = _server->GetConfig().GetRawConfig();
    if (rawConfig.contains("etf")) {
        if (rawConfig["etf"].contains("t0")) {
            for (auto& c : rawConfig["etf"]["t0"]) t0Codes.insert(c.get<String>());
        }
        if (rawConfig["etf"].contains("t1")) {
            for (auto& c : rawConfig["etf"]["t1"]) t1Codes.insert(c.get<String>());
        }
    }
    etfExch->SetEtfCodes(t0Codes, t1Codes);
    INFO("[ETF] Loaded T0 codes: {}, T1 codes: {}", t0Codes.size(), t1Codes.size());
}

double ExchangeManager::GetProgress(const String& strategy) const {
    // 聚合所有 HistorySimulation Exchange 的进度
    // 多标的场景（股票+ETF）下，取最慢的进度作为整体进度
    double minProgress = 1.0;
    bool foundAny = false;

    for (const auto& [name, exch] : _exchanges) {
        auto* base = dynamic_cast<HistorySimulationBase*>(exch);
        if (base) {
            // 检查该 Exchange 是否有该策略的上下文
            if (base->HasBacktestContext(strategy)) {
                double p = base->Progress(strategy);
                foundAny = true;
                if (p < minProgress) {
                    minProgress = p;
                }
            }
        }
    }

    return foundAny ? minProgress : 0.0;
}

void ExchangeManager::SetBacktestTimeRange(time_t start, time_t end) {
    for (auto& [name, exch] : _exchanges) {
        auto* base = dynamic_cast<HistorySimulationBase*>(exch);
        if (base) {
            base->SetBacktestTimeRange(start, end);
        }
    }
}

void ExchangeManager::ConfigureSlippageModels(const Set<String>& sources, const nlohmann::json& slippageConfig) {
    auto model = SlippageFactory::create(slippageConfig);

    for (const auto& source : sources) {
        ExchangeType targetType = ExchangeType::EX_Unknow;
        if (source == "股票") {
            targetType = ExchangeType::EX_STOCK_HIST_SIM;
        } else if (source == "ETF") {
            targetType = ExchangeType::EX_ETF_HIST_SIM;
        } else if (source == "期货") {
            // 期货回测引擎（如果有的话）
            continue;
        } else {
            WARN("[Slippage] Unknown source type: {}", source);
            continue;
        }

        auto itr = _typeExchanges.find(targetType);
        if (itr != _typeExchanges.end()) {
            auto* histSim = dynamic_cast<HistorySimulationBase*>(itr->second);
            if (histSim) {
                histSim->SetSlippageModel(SlippageFactory::create(slippageConfig));
                INFO("[Slippage] Configured slippage model for source '{}'", source);
            } else {
                WARN("[Slippage] Exchange for source '{}' is not a HistorySimulationBase", source);
            }
        } else {
            WARN("[Slippage] No exchange registered for source '{}'", source);
        }
    }
}

order_id ExchangeManager::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order)
{
    // 根据 symbol 类型路由到对应的 Exchange
    auto* targetExch = ResolveExchange(symbol);
    if (targetExch) {
        return targetExch->AddOrder(run_id, symbol, order);
    }

    // 回退：遍历所有 Exchange
    for (auto& [name, exc] : _exchanges) {
        return exc->AddOrder(run_id, symbol, order);
    }
    return order_id();
}
