#include "Handler/OrderHandler.h"
#include "Bridge/exchange.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include <string>
#include <tuple>
#include "BrokerSubSystem.h"

namespace {
    nlohmann::json TradeInfo2Json(const TradeInfo& info, size_t& buy_count) {
        nlohmann::json result;
        for (auto& item : info._reports) {
            nlohmann::json report;
            report["time"] = item._time;
            report["price"] = item._price;
            report["quantity"] = item._quantity;
            buy_count += item._quantity;

            result["reports"].emplace_back(std::move(report));
        }
        return result;
    }
}
OrderHandler::OrderHandler(Server* server)
  :HttpHandler(server) {

}

OrderHandler::~OrderHandler() {
  CancelAllOrder();
}

bool OrderHandler::Order(const std::string& symbol, double price, int number) {
  // auto activate = _handle->GetTradeExchange();
  // struct Order order;
  // order.buy_or_shell = true;
  // strcpy(order.symbol, symbol.c_str());
  // order.price = price;
  // order.number = number;
  // if (!activate->AddOrder(order)) {
  //   return false;
  // }
  // _handle->AddStock();
  return true;
}

void OrderHandler::QueryOrders() {
  // auto activate = _handle->GetTradeExchange();
  // auto orders = activate->GetOrders();
  // for (auto ptr: orders._order) {
  //   _orders[ptr->symbol].insert(ptr->_oid);
  // }
}

void OrderHandler::QueryOrder(const std::string& symbol) {

}

bool OrderHandler::CancelOrder(order_id id) {
  // auto activate = _handle->GetTradeExchange();
  // return activate->CancelOrder(id);
  return true;
}

void OrderHandler::CancelAllOrder() {

}

OrderBuyHandler::OrderBuyHandler(Server* server)
    :HttpHandler(server)
{

}

OrderBuyHandler::~OrderBuyHandler()
{

}

void OrderBuyHandler::post(const httplib::Request& req, httplib::Response& res)
{
    auto params = nlohmann::json::parse(req.body);
    auto symbol = GetSymbol(params);
    int quantity = params["quantity"];
    List<double> prices = params["price"];

    auto broker = _server->GetBrokerSubSystem();

    Order order;
    order._side = 0;
    order._number = quantity;
    order._time = Now();
    order._type = GetOrderType(params);
    auto itr = prices.begin();
    for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
      order._order[i]._price = *itr;
      ++itr;
    }
    TradeInfo info;
    broker->Buy(symbol, order, info);
    
    size_t buy_count = 0;
    nlohmann::json result = TradeInfo2Json(info, buy_count);

    if (buy_count == quantity) {
        result["status"] = 0;
    }
    else if (buy_count > 0) {
        result["status"] = 1;
    }
    else {
        result["status"] = 2;
    }
    res.set_content(result.dump(), "application/json");
    res.status = 200;
}

OrderType GetOrderType(nlohmann::json& params)
{
    int type = params["type"];
    switch (type) {
    case 0:
        return OrderType::Market;
    case 1:
        return OrderType::Limit;
    default:
        return OrderType::Market;
    }
}

OrderSellHandler::OrderSellHandler(Server* server)
    :HttpHandler(server)
{

}

void OrderSellHandler::post(const httplib::Request& req, httplib::Response& res)
{
    auto params = nlohmann::json::parse(req.body);
    auto symbol = GetSymbol(params);
    int quantity = params["quantity"];
    List<double> prices = params["price"];

    Order order;
    order._side = 1;
    order._number = quantity;
    order._time = Now();
    order._type = GetOrderType(params);
    auto itr = prices.begin();
    for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
        order._order[i]._price = *itr;
        ++itr;
    }
    auto info = Sell(symbol, order);

    size_t buy_count = 0;
    auto result = TradeInfo2Json(info, buy_count);
    result["status"] = 0;
    res.set_content(result.dump(), "application/json");
    res.status = 200;
}

TradeInfo OrderSellHandler::Sell(symbol_t symbol, const Order& order)
{
    auto broker = _server->GetBrokerSubSystem();
    TradeInfo trades;
    broker->Sell(symbol, order, trades);
    return trades;
}

OrderCancelHandler::OrderCancelHandler(Server* server)
    :HttpHandler(server)
{

}

void OrderCancelHandler::post(const httplib::Request& req, httplib::Response& res)
{

}
