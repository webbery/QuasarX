#include "Handler/ExchangeHandler.h"
#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "Bridge/SIM/StockRealSimulation.h"
#include "Bridge/HX/HXExchange.h"
#include "Bridge/TickFlow/TickFlowBridge.h"
#include "Bridge/exchange.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include "BrokerSubSystem.h"

ExchangeHandler::ExchangeHandler(Server* handle)
  :HttpHandler(handle)
{

}

ExchangeHandler::~ExchangeHandler() {
}

bool ExchangeHandler::Use(const String& name) {
  auto& config = _server->GetConfig();
  auto exchange = config.GetExchangeByName(name);
  String ex_type = exchange["api"];
  bool ret = false;
  ExchangeType et = ExchangeType::EX_Unknow;
  if (ex_type == CTP_API) {
    ret = SwitchExchange<CTPExchange>(name);
    _activeFutureName = name;
    et = ExchangeType::EX_CTP;
  }
  else if (ex_type == STOCK_HISTORY_SIM) {
    ret = SwitchExchange<StockHistorySimulation>(name);
    et = ExchangeType::EX_STOCK_HIST_SIM;
    _activeFutureName = name;
    _activeStockName = name;
    _enableSimulation = true;
  }
  else if (ex_type == ETF_HISTORY_SIM) {
    ret = SwitchExchange<ETFHistorySimulation>(name);
    et = ExchangeType::EX_ETF_HIST_SIM;
    _activeFutureName = name;
    _activeStockName = name;
    _enableSimulation = true;
  }
  else if (ex_type == STOCK_REAL_SIM) {
    ret = SwitchExchange<StockRealSimulation>(name);
    et = ExchangeType::EX_STOCK_REAL_SIM;
    _activeStockName = name;
  }
  else if (ex_type == HX_API) {
      ret = SwitchExchange<HXExchange>(name);
      _activeStockName = name;
      et = ExchangeType::EX_HX;
  }
  else if (ex_type == TICKFLOW_QUOTE_API) {
      ret = UseTickFlow(name);
      _activeStockName = name;
      et = ExchangeType::EX_TICKFLOW_QUOTE;
  }
  else {
      WARN("not support exchange {}", ex_type);
  }
  if (ret) {
    auto ptr = _exchanges[name];
    _type_excs[et] = ptr;

    // 同步 symbol 信息到 Server（所有 exchange 统一处理）
    List<SymbolInfo> symbols;
    // 获取所有类型的符号信息
    ptr->GetAllStockSymbols(symbols);
    ptr->GetAllFundSymbols(symbols);
    ptr->GetAllOptionSymbols(symbols);

    // 填充到 Server 的 _markets
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

    QuoteFilter filter;
    if (exchange.contains("pool")) {
        for (String symbol: exchange["pool"]) {
            filter._symbols.insert(symbol);
        }
    }
    if (exchange.contains("utc_active")) {
        for (auto& range: exchange["utc_active"]) {
            String working_range = range;
            Vector<String> time_range;
            split(working_range, time_range, "-");
            String& start_datetime = time_range[0];
            Vector<String> components;
            split(start_datetime, components, ":");
            if (components.size() == 0)
                break;

            auto start_hour = (char)atoi(components[0].c_str());
            auto start_minute = (char)atoi(components[1].c_str());
            components.clear();
            split(time_range[1], components, ":");
            if (components.size() == 0)
                break;
            auto stop_hour = (char)atoi(components[0].c_str());
            auto stop_minute = (char)atoi(components[1].c_str());
            ptr->SetWorkingRange(start_hour, stop_hour, start_minute, stop_minute);
        }
    }
    ptr->SetFilter(filter);
  }
  return ret;
}

bool ExchangeHandler::UseTickFlow(const String& name) {
    if (!_exchanges[name]) {
        auto ptr = new TickFlowBridge(_server);
        auto& config = _server->GetConfig();
        auto exchange = config.GetExchangeByName(name);

        // 构造 ExchangeInfo（TickFlow 特化：只需要 username + passwd）
        ExchangeInfo info;
        memset(&info, 0, sizeof(ExchangeInfo));
        if (exchange.contains("username")) {
            std::string username = exchange["username"];
            strncpy(info._username, username.c_str(), sizeof(info._username) - 1);
        }
        if (exchange.contains("passwd")) {
            std::string passwd = exchange["passwd"];
            memcpy(info._passwd, passwd.data(), sizeof(info._passwd));
        }

        if (!ptr->Init(info)) {
            WARN("TickFlowBridge init failed for {}", name);
            delete ptr;
            return false;
        }
        _exchanges[name] = ptr;
    }
    auto ptr = _exchanges[name];
    // TickFlowBridge::Login 总是返回 true
    _server->SetActiveExchange(ptr);
    return true;
}

ExchangeInterface* ExchangeHandler::GetExchangeByType(ExchangeType type) {
  if (_enableSimulation) { // 模拟场景下强制返回仿真环境
    if (type == ExchangeType::EX_ETF_HIST_SIM) {
      return _type_excs[ExchangeType::EX_ETF_HIST_SIM];
    }
    return _type_excs[ExchangeType::EX_STOCK_HIST_SIM];
  }
  return _type_excs[type];
}

double ExchangeHandler::Buy(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& deals) {
    auto broker = _server->GetBrokerSubSystem();
    broker->Buy(strategy, symbol, order, deals);
  return 0;
}

double ExchangeHandler::Sell(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& deals) {
    auto broker = _server->GetBrokerSubSystem();
    broker->Sell(strategy, symbol, order, deals);
  return 0;
}

void ExchangeHandler::post(const httplib::Request& req, httplib::Response& res) {

}
