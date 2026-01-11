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
    int kind = params["kind"];
    auto symbol = GetSymbol(params);
    int quantity = params["quantity"];
    List<double> prices = params["prices"];
    auto lambda_sendResult = [symbol, this](const TradeReport& report) {
        auto sock = Server::GetSocket();
        auto info = to_sse_string(symbol, report);
        nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
    };
    Order order;
    order._volume = quantity;
    order._time = Now();
    order._validTime = (OrderTimeValid)params["timeType"];
    order._type = GetOrderType(params);
    auto itr = prices.begin();
    for (int i = 0; i < MAX_ORDER_SIZE; ++i) {
        if (itr != prices.end()) {
            order._order[i]._price = *itr;
            ++itr;
        }
        else {
            order._order[i]._price = 0;
        }
    }
    if (kind == 1) {
        order._flag = (int)params["open"];
        order._hedge = (OptionHedge)params["hedge"];
    }
    int perf = 1;
    if (params.contains("perf")) {
        // 循环请求,测试性能
        perf = params["perf"];
    }
    nlohmann::json result;
    auto broker = _server->GetBrokerSubSystem();
    if (direct == 0) {
        order._side = 0;
        for (int i = 0; i < perf; ++i) {
            auto id = broker->Buy("_custom_", symbol, order, lambda_sendResult);
            nlohmann::json result;
            if (id._error) {
                ProcessError(id._error, result, res);
            }
            else {
                res.status = 200;
                result["id"] = id._id;
                result["sysID"] = id._sysID;
            }
        }
    }
    else if (direct == 1) {
        order._side = true;
        for (int i = 0; i < perf; ++i) {
            auto id = broker->Sell("_custom_", symbol, order, lambda_sendResult);

            if (id._error) {
                ProcessError(id._error, result, res);
            }
            else {
                res.status = 200;
                result["id"] = id._id;
                result["sysID"] = id._sysID;
            }
        }
    }
    res.set_content(result.dump(), "application/json");
}

void OrderHandler::get(const httplib::Request& req, httplib::Response& res) {
    // 获取所有订单状态
    nlohmann::json result;
    auto tp_itr = req.params.find("type");
    if (tp_itr == req.params.end()) {
        res.status = 400;
        res.set_content("{message: 'not set type of stock/option/future'}", "application/json");
        return;
    }
    auto type = (SecurityType)atoi(tp_itr->second.c_str());
    auto itr = req.params.find("id");
    if (itr == req.params.end()) {
        auto broker = _server->GetBrokerSubSystem();
        OrderList ol;
        if (!broker->QueryOrders(type,ol)) {
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
    nlohmann::json data = nlohmann::json::parse(req.body);
    if (!data.contains("type")) {
        res.status = 400;
        res.set_content("{message: 'query must contain `type`'}", "application/json");
        return;
    }

    auto type = (SecurityType)data["type"];
    auto broker = _server->GetBrokerSubSystem();
    if (req.body.empty()) { // 全部
        OrderList ol;
        if (!broker->QueryOrders(type,ol)) {
            res.status = 500;
        }
        else {
            for (auto& item: ol) {
                auto symbol = item._symbol;
                order_id id;
                strcpy(id._sysID, item._sysID.c_str());
                id._id = item._id;
                broker->CancelOrder(id, symbol, [symbol] (const TradeReport& report) {
                    auto sock = Server::GetSocket();
                    auto info = to_sse_string(symbol, report);
                    nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
                });
                if (id._error == ERROR_CANCEL_LIMIT) {
                    res.status = 500;
                    res.set_content("{message: 'cancel order out of limit'}", "application/json");
                    return;
                }
            }
        }
    } else {
        if (!data.contains("sysID")) {
            res.status = 400;
            res.set_content("{message: 'query must contain `sysID`'}", "application/json");
            return;
        }
        String sysID = data["sysID"];
        Order order;
        if (!broker->QueryOrder(sysID, order)) {
            res.status = 400;
            res.set_content("{message: 'order not exist}", "application/json");
            return;
        }
        auto symbol = order._symbol;
        order_id id;
        // id._id = order._id;
        strcpy(id._sysID, sysID.c_str());
        broker->CancelOrder(id, symbol, [this, symbol] (const TradeReport& report) {
            auto sock = Server::GetSocket();
            auto info = to_sse_string(symbol, report);
            nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
        });
    }
    res.status = 200;
    res.set_content("{}", "application/json");
}

void OrderHandler::put(const httplib::Request& req, httplib::Response& res)
{

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
