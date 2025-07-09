#include "Handler/OrderHandler.h"
#include "Bridge/exchange.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include <string>
#include <tuple>

OrderHandler::OrderHandler(Server* server)
  :HttpHandler(server) {

}

OrderHandler::~OrderHandler() {
  CancelAllOrder();
}

void OrderHandler::doWork(const std::vector<std::string>& params) {
  // if (params.empty())
  //   return;

  // if (params[0] == "cancel") {
  //   if (params.size() != 2) {
  //     printf("require 2 params: code=?\n");
  //     return;
  //   }
  //   auto [symbol] = GetParams<std::string>(params, "code");
  //   auto itr = _orders.find(symbol);
  //   if (itr == _orders.end()) {
  //     printf("no order for %s\n", symbol.c_str());
  //     return;
  //   }

  //   std::set<order_id> fail_ids;
  //   for (auto id: itr->second) {
  //     if (!CancelOrder(id)) {
  //       fail_ids.insert(id);
  //       printf("cancel order success.\n");
  //     }
  //   }
  //   if (fail_ids.empty()) {
  //     printf("cancel all order success.\n");
  //   } else {
  //     printf("cancel order fail.\n");
  //   }
  // }
  // else if (params[0] == "query") {
  //   if (params.size() == 1) {
  //     QueryOrders();
  //   } else {
  //     // auto [symbol] = GetParams<std::string>(params, "code");
  //   }
  // }
  // else {
  //   if (params.size() != 3) {
  //     printf("require 3 params: code=? price=? cnt=?\n");
  //     return;
  //   }
  //   auto [symbol, price, number] = GetParams<std::string, double, int>(params, "code", "price", "cnt");
  //   if (Order(symbol, price, number)) {
  //     printf("Order Success.\n");
  //   } else {
  //     printf("Order Fail.\n");
  //   }
  // }
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
