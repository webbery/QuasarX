#pragma once
#include "Bridge/exchange.h"
#include "HttpHandler.h"
#include <charconv>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <iostream>
#include <utility>
#include <set>

class ExchangeInterface;
class Server;
class OrderHandler : public HttpHandler {
public:
  OrderHandler(Server* server);
  ~OrderHandler();

  void doWork(const std::vector<std::string>& params);
private:
  bool Order(const std::string& symbol, double price, int number);
  bool Trade();
  bool CancelOrder(order_id id);
  void CancelAllOrder();

  void QueryOrders();
  
  void QueryOrder(const std::string& symbol);

private:
  Server* _handle;

  std::map<std::string, std::set<order_id>> _orders;
};