#include "Handler/ExchangeHandler.h"
#include "Bridge/XTP/XTPExchange.h"
#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/SIM/SIMExchange.h"
#include "Bridge/exchange.h"
#include "server.h"
#include "Util/string_algorithm.h"

ExchangeHandler::ExchangeHandler(Server* handle)
  :HttpHandler(handle)
{

}

ExchangeHandler::~ExchangeHandler() {
  for (auto& item: _exchanges) {
    if (item.second)
      item.second->Release();
  }
}

void ExchangeHandler::doWork(const std::vector<std::string>& params)
{
  if (params[0] == "use") {
    if (params.size() != 2) {
      printf("wrong params.\n");
      return;
    }
    Use(params[1]);
  }
}

bool ExchangeHandler::Use(const String& name) {
  auto& config = _server->GetConfig();
  auto exchange = config.GetExchangeByName(name);
  String ex_type = exchange["api"];
  bool ret = false;
  ExchangeType et = ExchangeType::EX_Unknow;
  if (ex_type == XTP_API) {
    ret = SwitchExchange<XTPExchange>(name);
    et = ExchangeType::EX_XTP;
  }
  else if (ex_type == CTP_API) {
    ret = SwitchExchange<CTPExchange>(name);
    et = ExchangeType::EX_CTP;
  }
  else if (ex_type == "sim") {
    ret = SwitchExchange<StockSimulation>(name);
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
  strcpy(handle._trade_addr, trade_info[0].c_str());
  if (exchange.contains("account")) {
    std::string username = exchange["account"];
    strcpy(handle._username, username.c_str());
  }
  if (exchange.contains("passwd")) {
    std::string passwd = exchange["passwd"];
    strcpy(handle._passwd, passwd.c_str());
  }
  if (trade_info.size() > 1) {
    handle._trade_port = atoi(trade_info[1].c_str());
  }
  if (quote_info.size() > 1) {
    handle._quote_port = atoi(quote_info[1].c_str());
  }
  return handle;
}

void ExchangeHandler::post(const httplib::Request& req, httplib::Response& res) {

}
