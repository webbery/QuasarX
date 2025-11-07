#include "Handler/StockHandler.h"
#include "Bridge/exchange.h"
#include "Util/Volatility.h"
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
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
    String stock_path = path + "/A_code.csv";
    io::CSVReader<2> reader(stock_path);
    reader.read_header(io::ignore_extra_column, "code", "name");
    std::string code;
    std::string name;
    nlohmann::json stocks;
    while (reader.read_row(code, name)) {
        if (name == "-")
            continue;
        auto symbol = format_symbol(code);
        Map<String, String> info;
        info["symbol"] = symbol;
        info["name"] = name;
        stocks["stocks"].emplace_back(std::move(info));
    }
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
  auto start_t = FromTick(start);
  String end = req.get_param_value("end");
  String right = req.get_param_value("right");
  auto end_t = FromTick(end);
  DataFrequencyType dft = DataFrequencyType::Day;
  if (type == "5m") {
    dft = DataFrequencyType::Min5;
  }
  else if (type == "1d") {
    dft = DataFrequencyType::Day;
  }
  StockAdjustType rightType = StockAdjustType::None;
  if (right == "1") {
    rightType = StockAdjustType::After;
  }
  auto symbol = format_symbol(id);
  auto group = _server->PrepareStockData({symbol}, dft, rightType);
  if (!group || !group->IsValid()) {
    res.status = 400;
  }
  else {
    auto size = group->Size(symbol);
    nlohmann::json result;

    for (size_t i = 0; i < size; ++i) {
      auto datetime = group->Get<time_t>(symbol, "datetime", i);
      if (datetime < start_t || datetime > end_t)
        continue;
      nlohmann::json row;
      row["datetime"] = datetime;
      using Extractor = QuoteExtractor<
        FixedString{"open"},
        FixedString{"close"},
        FixedString{"low"},
        FixedString{"high"},
        FixedString{"volume"},
        FixedString{"turnover"}
      >;
      Extractor::extract<double>(row, group, symbol, i);
      result.emplace_back(std::move(row));
    }
    res.status = 200;
    res.set_content(result.dump(), "application/json");
  }
}

StockDetailHandler::StockDetailHandler(Server* server)
  :HttpHandler(server)
{

}

void StockDetailHandler::get(const httplib::Request& req, httplib::Response& res)
{
  String symbol = req.get_param_value("id");
  auto exchange = _server->GetAvaliableStockExchange();
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
