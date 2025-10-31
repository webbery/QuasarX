#include "Handler/OrderHandler.h"
#include "Bridge/exchange.h"
#include "DataGroup.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include <cstdint>
#include <string>
#include <thread>
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

OrderHandler::OrderHandler(Server* server)
  :HttpHandler(server) {

}

OrderHandler::~OrderHandler() {
  CancelAllOrder();
}

void OrderHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    int direct = params["direct"];
    auto symbol = GetSymbol(params);
    int quantity = params["quantity"];
    List<double> prices = params["price"];
    auto lambda_sendResult = [symbol, this](const TradeReport& report) {
        auto tid = std::this_thread::get_id();
        if (_sockets.count(tid) == 0) {
            nng_socket sock;
            Publish(URI_SERVER_EVENT, sock);
            _sockets[tid] = sock;
        }
        auto sock = _sockets[tid];
        auto info = to_sse_string(symbol, report);
        nng_send(sock, info.data(), info.size(), 0);
    };
    if (direct == 0) {
        auto broker = _server->GetBrokerSubSystem();

        Order order;
        order._side = 0;
        order._volume = quantity;
        order._time = Now();
        order._type = GetOrderType(params);
        auto itr = prices.begin();
        for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
            order._order[i]._price = *itr;
            ++itr;
        }

        auto id = broker->Buy("", symbol, order, lambda_sendResult);
        
        nlohmann::json result;
        result["id"] = id;
        res.set_content(result.dump(), "application/json");
    }
    else if (direct == 1) {
        Order order;
        order._side = true;
        order._volume = quantity;
        order._time = Now();
        order._type = GetOrderType(params);
        auto itr = prices.begin();
        for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
            order._order[i]._price = *itr;
            ++itr;
        }
        auto broker = _server->GetBrokerSubSystem();
        TradeInfo trades;
        auto id = broker->Sell("", symbol, order, lambda_sendResult);
        
        nlohmann::json result;
        result["id"] = id;
        res.set_content(result.dump(), "application/json");
    }
    
    res.status = 200;
}

void OrderHandler::get(const httplib::Request& req, httplib::Response& res) {
    // 获取所有订单状态
    nlohmann::json result;
    if (req.params.size() == 0) {
        auto broker = _server->GetBrokerSubSystem();
        OrderList ol;
        if (!broker->QueryOrders(ol)) {
            res.status = 500;
        }
        else {
            for (auto& item : ol) {
                auto order = order2json(item);
                result.emplace_back(std::move(order));
            }
            res.status = 200;
        }
    }
    else {
        auto itr = req.params.find("id");
        if (itr == req.params.end()) {
            res.status = 400;
            res.set_content("{message: 'query must be `id`'}", "application/json");
            return;
        }
        String symbol = itr->second;

        res.status = 200;
    }
    res.set_content(result.dump(), "application/json");
}

void OrderHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto broker = _server->GetBrokerSubSystem();
    auto lambda_sendReport = [this] (const TradeReport& report) {
        auto tid = std::this_thread::get_id();
        if (_sockets.count(tid) == 0) {
            nng_socket sock;
            Publish(URI_SERVER_EVENT, sock);
            _sockets[tid] = sock;
        }
        // auto sock = _sockets[tid];
        // auto info = to_sse_string(symbol, report);
        // nng_send(sock, info.data(), info.size(), 0);
    };
    if (req.params.size() == 0) { // 全部
        OrderList ol;
        if (!broker->QueryOrders(ol)) {
            res.status = 500;
        }
        else {
            for (auto& item: ol) {
                broker->CancelOrder({item._id}, lambda_sendReport);
            }
        }
    } else {
        auto itr = req.params.find("id");
        if (itr == req.params.end()) {
            res.status = 400;
            res.set_content("{message: 'query must be `id`'}", "application/json");
            return;
        }
        uint64_t id = atol(itr->second.c_str());
        broker->CancelOrder({id}, lambda_sendReport);
    }
    res.status = 200;
    res.set_content("{}", "application/json");
}

bool OrderHandler::BuyOrder(const std::string& symbol, double price, int number) {
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

TradeInfo OrderHandler::SellORder(symbol_t symbol, const Order& order)
{
    auto broker = _server->GetBrokerSubSystem();
    TradeInfo trades;
    broker->Sell("", symbol, order, trades);
    return trades;
}
