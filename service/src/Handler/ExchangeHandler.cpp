#include "Handler/ExchangeHandler.h"
#include "Bridge/XTP/XTPExchange.h"
#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/SIM/SIMExchange.h"
#include "Bridge/HX/HXExchange.h"
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
  if (ex_type == XTP_API) {
    ret = SwitchExchange<XTPExchange>(name);
    _activeStockName = name;
    et = ExchangeType::EX_XTP;
  }
  else if (ex_type == CTP_API) {
    ret = SwitchExchange<CTPExchange>(name);
    _activeFutureName = name;
    et = ExchangeType::EX_CTP;
  }
  else if (ex_type == "sim") {
    ret = SwitchExchange<StockSimulation>(name);
    et = ExchangeType::EX_SIM;
    _activeFutureName = name;
    _activeStockName = name;
    _enableSimulation = true;
  }
  else if (ex_type == HX_API) {
      ret = SwitchExchange<HXExchange>(name);
      _activeStockName = name;
      et = ExchangeType::EX_HX;
  }
  else {
      WARN("not support exchange {}", ex_type);
  }
  if (ret) {
    auto ptr = _exchanges[name];
    _type_excs[et] = ptr;
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
      // filter._range.first = 
    }
    ptr->SetFilter(filter);
  }
  return ret;
}

ExchangeInterface* ExchangeHandler::GetExchangeByType(ExchangeType type) {
  if (_enableSimulation) { // 模拟场景下强制返回仿真环境
    return _type_excs[ExchangeType::EX_SIM];
  }
  return _type_excs[type];
}

ExchangeInfo ExchangeHandler::GetExchangeInfo(const char* name)
{
  auto& config = _server->GetConfig();
  auto exchange = config.GetExchangeByName(name);

  std::string ex_type = exchange["api"];
  std::string quote_addr = exchange["quote"];
  std::string trade_addr = exchange["trade"];
  ExchangeInfo handle;
  strcpy(handle._local_addr, config.GetHost().c_str());
  std::vector<std::string> trade_info;
  split(trade_addr, trade_info, ":");
  std::vector<std::string> quote_info;
  split(quote_addr, quote_info, ":");
  strcpy(handle._quote_addr, quote_info[0].c_str());
  strcpy(handle._default_addr, trade_info[0].c_str());
  if (exchange.contains("account")) {
    std::string username = exchange["account"];
    strcpy(handle._username, username.c_str());
  }
  if (exchange.contains("passwd")) {
    std::string passwd = exchange["passwd"];
    strcpy(handle._passwd, passwd.c_str());
  }
  if (exchange.contains("option")) {
      std::string option_addr = exchange["option"];
      std::vector<std::string> option_info;
      split(option_addr, option_info, ":");
      strcpy(handle._option_addr, option_info[0].c_str());
      handle._option_port = atoi(option_info[1].c_str());
  }
  if (trade_info.size() > 1) {
    handle._stock_port = atoi(trade_info[1].c_str());
  }
  if (quote_info.size() > 1) {
    handle._quote_port = atoi(quote_info[1].c_str());
  }
  auto accounts = config.GetStockAccounts();
  assert(accounts.size() > 0);
  auto account = accounts.front().first;
  auto accpwd = accounts.front().second;
  strcpy(handle._account, account.c_str());
  strcpy(handle._accpwd, accpwd.c_str());
  handle._localPort = config.GetPort();
  return handle;
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
