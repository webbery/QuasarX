#include "Handler/StockHandler.h"
#include "Bridge/exchange.h"
#include "ExchangeManager.h"
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include "Util/data.h"
#include "Util/system.h"
#include "server.h"
#include "Util/finance.h"
#include <tuple>
#include <vector>
#include "json.hpp"
#include "csv.h"

using namespace std;

StockHandler::StockHandler(Server* server):HttpHandler(server) {

}

void StockHandler::checkHelp() {
  std::list<std::pair<std::string, std::string>> help{
    {"sort", "default or [type=?(sp/r) top=?]"},
  };
  for (auto& args: help) {
    printf("  %s\t%s\n", args.first.c_str(), args.second.c_str());
  }
}

void StockHandler::get(const httplib::Request& req, httplib::Response& res)
{
    auto path = _server->GetConfig().GetDatabasePath();
    String stock_path = path + "/symbol_market.csv";
    nlohmann::json stocks;
    if (std::filesystem::exists(stock_path)) {
        try {
            io::CSVReader<2> reader(stock_path);
            reader.read_header(io::ignore_extra_column, "代码", "name");
            std::string code;
            std::string name;
            while (reader.read_row(code, name)) {
                if (name == "-")
                    continue;
                auto symbol = format_symbol(code);
                Map<String, String> info;
                info["symbol"] = symbol;
                info["name"] = name;
                stocks["stocks"].emplace_back(std::move(info));
            }
        }
        catch (std::exception& e) {
            INFO("read symbol_market.csv error: {}", e.what());
            throw std::runtime_error(e.what());
        }
    }
    res.status = 200;
    stocks["status"] = "success";
    res.set_content(stocks.dump(), "application/json");
}


StockHistoryHandler::StockHistoryHandler(Server* server)
  :HttpHandler(server)
{

}

void StockHistoryHandler::get(const httplib::Request& req, httplib::Response& res)
{
  String id = req.get_param_value("id");
  String type = req.get_param_value("type");
  String start = req.get_param_value("start");
  String end = req.get_param_value("end");
  String right = req.get_param_value("right");

  // 支持两种格式：日期字符串 "YYYY-MM-DD" 或 Unix 时间戳
  String start_date, end_date;
  if (start.empty() || start.find('-') != String::npos) {
    start_date = start;  // 已经是日期字符串或空
  } else {
    start_date = ToString(FromTick(start), "%Y-%m-%d");
  }
  if (end.empty() || end.find('-') != String::npos) {
    end_date = end;
  } else {
    end_date = ToString(FromTick(end), "%Y-%m-%d");
  }

  // 转换为 LoadHistoryDataWithFreq 参数
  symbol_t sym = to_symbol(toInternalSymbol(id));
  BarFreq freq = type.empty() ? BarFreq::Day : parseBarFreq(type);
  AdjType adj = (right == "1") ? AdjType::HFQ : AdjType::None;

  Vector<String> dates;
  auto data = LoadHistoryDataWithFreq(sym,
      {"open", "close", "high", "low", "volume", "turnover"},
      start_date, end_date, freq, adj, &dates);

  if (dates.empty()) {
    res.status = 400;
    return;
  }

  nlohmann::json result = nlohmann::json::array();
  for (size_t i = 0; i < dates.size(); ++i) {
    nlohmann::json row;
    row["datetime"] = FromStr(dates[i]);
    row["open"]    = (i < data["open"].size())    ? data["open"][i]    : 0.0;
    row["close"]   = (i < data["close"].size())   ? data["close"][i]   : 0.0;
    row["high"]    = (i < data["high"].size())     ? data["high"][i]    : 0.0;
    row["low"]     = (i < data["low"].size())      ? data["low"][i]     : 0.0;
    row["volume"]  = (i < data["volume"].size())   ? data["volume"][i]  : 0.0;
    row["turnover"] = (i < data["turnover"].size()) ? data["turnover"][i] : 0.0;
    result.emplace_back(std::move(row));
  }

  res.status = 200;
  res.set_content(result.dump(), "application/json");
}

StockDetailHandler::StockDetailHandler(Server* server)
  :HttpHandler(server)
{

}

void StockDetailHandler::get(const httplib::Request& req, httplib::Response& res)
{
  String symbol = req.get_param_value("id");
  auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
  auto quote = exchange->GetQuote(to_symbol(symbol));
  nlohmann::json jsn;
  jsn["upper"] = quote._upper;
  jsn["lower"] = quote._lower;
  jsn["price"] = quote._close;
  jsn["volume"] = quote._volume;
  jsn["turnover"] = quote._turnover;
  res.status = 200;
  res.set_content(jsn.dump(), "application/json");
}

StockPrivilege::StockPrivilege(Server* server):HttpHandler(server)
{

}

void StockPrivilege::get(const httplib::Request& req, httplib::Response& res)
{
    String id = req.get_param_value("id");
    if (Server::GetExchange(id) == ExchangeName::MT_Unknow) {
        ProcessError(ERROR_NO_SECURITY, res);
        return;
    }
    auto symbol = to_symbol(id);
    auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    nlohmann::json jsn;
    auto result = exchange->HasPermission(symbol);
    if (result.has_value()) {
        if (result.value()) {
            jsn["forbid"] = false;
        }
        else {
            jsn["forbid"] = true;
            jsn["message"] = "query privilege fail";
        }
    }
    else {
        jsn["forbid"] = true;
        jsn["message"] = result.error();
    }
    res.status = 200;
    res.set_content(jsn.dump(), "application/json");
}

StockParams::StockParams(Server* server):HttpHandler(server)
{

}

void StockParams::get(const httplib::Request& req, httplib::Response& res)
{
    auto& config = _server->GetConfig();
    auto& limits = config.GetStockLimits();
    res.status = 200;
    nlohmann::json result;
    auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    result["order_limit"] = exchange->GetStockLimitation(2);
    result["daily_limit"] = exchange->GetStockLimitation(1);
    result["cancel_limit"] = exchange->GetStockLimitation(3);
    res.set_content(result.dump(), "application/json");
}

void StockParams::put(const httplib::Request& req, httplib::Response& res)
{
    nlohmann::json data = nlohmann::json::parse(req.body);
    int ol = data["order_limit"];
    int dl = data["daily_limit"];
    int cl = data["cancel_limit"];
    auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    if (!exchange->SetStockLimitation(1, dl)) {
        ProcessError(ERROR_SET_ORDER_LIMIT, res);
        return;
    }
    if (!exchange->SetStockLimitation(2, ol)) {
        ProcessError(ERROR_SET_DAILY_LIMIT, res);
        return;
    }
    if (!exchange->SetStockLimitation(3, cl)) {
        ProcessError(ERROR_SET_CANCEL_LIMIT, res);
        return;
    }
    res.status = 200;
    res.set_content("{'message':'success'}", "application/json");
}
