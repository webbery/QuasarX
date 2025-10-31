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

OrderType GetOrderType(nlohmann::json& params);

class OrderHandler : public HttpHandler {
public:
  OrderHandler(Server* server);
  ~OrderHandler();

    virtual void post(const httplib::Request& req, httplib::Response& res);
    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void del(const httplib::Request& req, httplib::Response& res);
private:
  bool BuyOrder(const std::string& symbol, double price, int number);
  TradeInfo SellORder(symbol_t symbol, const Order& order);
  bool Trade();
  bool CancelOrder(order_id id);
  void CancelAllOrder();

  void QueryOrders();
  
  void QueryOrder(const std::string& symbol);

private:
  Server* _handle;

  std::map<std::string, std::set<order_id>> _orders;
  Map<std::thread::id, nng_socket> _sockets;
};

class OrderSellHandler : public HttpHandler {
public:
    OrderSellHandler(Server* server);
    ~OrderSellHandler(){}

    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    int64_t SellAsyn();
    TradeInfo Sell(symbol_t symbol, const Order& order);
};

class OrderCancelHandler : public HttpHandler {
public:
    OrderCancelHandler(Server* server);
    ~OrderCancelHandler(){}

    virtual void post(const httplib::Request& req, httplib::Response& res);

};