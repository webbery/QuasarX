#include "ExchangeManager.h"
#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/HX/HXExchange.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/StockRealSimulation.h"
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
        if (ret) {
            _activeFutureName = name;
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
        if (ret) {
            _activeStockName = name;
        }
    }
    else if (type == EX_STOCK_HIST_SIM) {
        ptr = new StockHistorySimulation(_server);
        ret = ptr->Init(exchangeCfg);
        _enableSimulation = true;
        _activeStockName = name;
        _activeFutureName = name;
    }
    else if (type == EX_STOCK_REAL_SIM) {
        ptr = new StockRealSimulation(_server);
        ret = ptr->Init(exchangeCfg);
        _activeStockName = name;
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
    ptr->GetAllStockSymbols(symbols);
    ptr->GetAllFundSymbols(symbols);
    ptr->GetAllOptionSymbols(symbols);

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
    }

    // 设置过滤条件和工作时间
    auto config = _server->GetConfig().GetExchangeByAPI(name);
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
        delete exch;
    }
    _exchanges.clear();
    _typeExchanges.clear();

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

ExchangeInterface* ExchangeManager::GetExchangeByType(ExchangeType type) const {
    if (_enableSimulation && type != ExchangeType::EX_STOCK_HIST_SIM) {
        // 模拟模式下强制返回仿真环境
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

// ========== 设置活跃交易所 ==========

void ExchangeManager::SetActiveStockExchange(const String& name) {
    if (_exchanges.count(name)) {
        _activeStockName = name;
        INFO("Active stock exchange set to: {}", name);
    }
}

void ExchangeManager::SetActiveFutureExchange(const String& name) {
    if (_exchanges.count(name)) {
        _activeFutureName = name;
        INFO("Active future exchange set to: {}", name);
    }
}

String ExchangeManager::GetActiveStockName() const {
    return _activeStockName;
}

String ExchangeManager::GetActiveFutureName() const {
    return _activeFutureName;
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
