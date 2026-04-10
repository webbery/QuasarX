#include "Handler/OrderHandler.h"
#include "Bridge/exchange.h"
#include "DataGroup.h"
#include "HttpHandler.h"
#include "Util/system.h"
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

    nlohmann::json TransactionToJson(const Transaction& trans) {
        nlohmann::json result;

        // 订单信息
        nlohmann::json order_json;
        order_json["id"] = trans._order._id;
        order_json["symbol"] = get_symbol(trans._order._symbol);
        order_json["volume"] = trans._order._volume;
        order_json["type"] = static_cast<int>(trans._order._type);
        order_json["side"] = static_cast<int>(trans._order._side);
        order_json["time"] = trans._order._time;
        order_json["status"] = static_cast<int>(trans._order._status);

        // 订单价格明细
        order_json["prices"] = trans._order._price;

        result["order"] = order_json;

        // 成交信息
        nlohmann::json trades_json = nlohmann::json::array();
        for (const auto& report : trans._deal._reports) {
            nlohmann::json trade_json;
            trade_json["time"] = report._time;
            trade_json["price"] = report._price;
            trade_json["quantity"] = report._quantity;
            trade_json["amount"] = report._trade_amount;
            trade_json["status"] = static_cast<int>(report._status);
            trade_json["sys_id"] = report._sysID;
            trades_json.push_back(trade_json);
        }
        result["trades"] = trades_json;

        // 计算统计信息
        double total_amount = 0;
        int total_quantity = 0;
        for (const auto& report : trans._deal._reports) {
            total_amount += report._trade_amount;
            total_quantity += report._quantity;
        }
        result["total_amount"] = total_amount;
        result["total_quantity"] = total_quantity;
        result["average_price"] = total_quantity > 0 ? total_amount / total_quantity : 0;

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
    double prices = params["price"];
    auto lambda_sendResult = [symbol, this](const TradeReport& report) {
        auto sock = Server::GetSocket();
        auto info = to_sse_string(symbol, report);
        nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
    };
    Order order;
    order._volume = quantity;
    order._time = Now();
    if (params.contains("timeType")) {
        order._validTime = (OrderTimeValid)params["timeType"];
    }
    else {
        order._validTime = OrderTimeValid::Today;
    }
    order._type = GetOrderType(params);
    order._price = prices;
    if (kind == 1) {
        order._flag = (int)params["open"];
        order._hedge = (OptionHedge)params["hedge"];
    }
    int perf = 1;
#ifdef _DEBUG
    if (params.contains("perf")) {
        // 循环请求,测试性能
        perf = params["perf"];
    }
#endif
    nlohmann::json result;
    auto broker = _server->GetBrokerSubSystem();
    if (direct == 0) {
        order._side = 0;
        for (int i = 0; i < perf; ++i) {
            auto id = broker->Buy(0, "_custom_", symbol, order, lambda_sendResult);
            if (id._error) {
                return ProcessError(id._error, res);
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
            auto id = broker->Sell(0, "_custom_", symbol, order, lambda_sendResult);

            if (id._error) {
                return ProcessError(id._error, res);
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
        ProcessError(ERROR_REQUIR_TYPE, res);
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
        List<String> sysIDs = data["sysID"];
        List<Pair<order_id, symbol_t>> oids;
        for (auto& sysID : sysIDs) {
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
            oids.emplace_back(std::move(Pair{id, symbol}));
            
        }
        for (auto& item: oids) {
            auto& id = item.first;
            auto& symbol = item.second;
            broker->CancelOrder(id, symbol, [this, symbol](const TradeReport& report) {
                auto sock = Server::GetSocket();
                auto info = to_sse_string(symbol, report);
                nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
                });
        }
    }
    res.status = 200;
    res.set_content("{}", "application/json");
}

void OrderHandler::put(const httplib::Request& req, httplib::Response& res)
{
    nlohmann::json data = nlohmann::json::parse(req.body);
    int type = data["type"];
    int operation = data["operation"];
    if (type == 0) {
        auto exchange = _server->GetAvaliableStockExchange();
        if (operation == 0) {
            // 暂停报单
            exchange->EnableInsertOrder(false);
        }
        else if (operation == 1) {
            // 恢复报单
            exchange->EnableInsertOrder(true);
        }
        else if (operation == 2) {
            // 一键撤单
        }
    }
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

HistoryTradeHandler::HistoryTradeHandler(Server* server)
:HttpHandler(server) {

}

HistoryTradeHandler::~HistoryTradeHandler() {

}

void HistoryTradeHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        nlohmann::json response;

        // 获取查询参数
        std::string symbol_str = req.get_param_value("symbol");
        std::string strategy = req.get_param_value("strategy");
        std::string start_time = req.get_param_value("start");
        std::string end_time = req.get_param_value("end");
        std::string page_str = req.get_param_value("page");
        std::string page_size_str = req.get_param_value("page_size");

        auto* server = _server;
        if (!server) {
            res.status = 500;
            res.set_content(R"({"error": "Server not available"})", "application/json");
            return;
        }

        auto* broker = server->GetBrokerSubSystem();
        if (!broker) {
            res.status = 500;
            res.set_content(R"({"error": "Broker subsystem not available"})", "application/json");
            return;
        }

        // 解析页码
        size_t page = 1;
        size_t page_size = 50;
        try {
            page = std::stoul(page_str);
            page_size = std::stoul(page_size_str);
            page_size = std::min(page_size, static_cast<size_t>(1000)); // 限制最大页大小
        } catch (...) {
            // 使用默认值
        }

        // 解析时间戳
        time_t start_t = 0, end_t = 0;
        try {
            if (!start_time.empty()) start_t = std::stoll(start_time);
            if (!end_time.empty()) end_t = std::stoll(end_time);
        } catch (...) {
            // 时间解析失败，忽略时间过滤
        }

        // 根据参数选择查询方式
        if (!symbol_str.empty()) {
            // 查询指定标的的交易记录
            symbol_t symbol = to_symbol(symbol_str);
            auto query_result = broker->QueryTrades(symbol, strategy, start_t, end_t,
                                                   (page - 1) * page_size, page_size);

            nlohmann::json trades_json = nlohmann::json::array();
            for (const auto& trade : query_result.trades) {
                trades_json.push_back(TransactionToJson(trade));
            }

            response["symbol"] = symbol_str;
            response["total_count"] = query_result.totalCount;
            response["page"] = page;
            response["page_size"] = page_size;
            response["total_pages"] = (query_result.totalCount + page_size - 1) / page_size;
            response["trades"] = trades_json;

        } else {
            // 查询所有交易记录
            auto query_result = broker->QueryTrades(null_symbol(), strategy, start_t, end_t,
                                                   (page - 1) * page_size, page_size);

            nlohmann::json trades_json = nlohmann::json::array();
            for (const auto& trade : query_result.trades) {
                trades_json.push_back(TransactionToJson(trade));
            }

            response["total_count"] = query_result.totalCount;
            response["page"] = page;
            response["page_size"] = page_size;
            response["total_pages"] = (query_result.totalCount + page_size - 1) / page_size;
            response["trades"] = trades_json;
        }

        res.set_content(response.dump(), "application/json");

    } catch (const std::exception& e) {
        res.status = 500;
        nlohmann::json error = {{"error", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
}
