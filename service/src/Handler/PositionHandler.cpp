#include "Handler/PositionHandler.h"
#include "server.h"
#include "Util/string_algorithm.h"

PositionHandler::PositionHandler(Server* server):HttpHandler(server) {

}

void PositionHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto exchange = _server->GetAvaliableStockExchange();
    AccountPosition positions;
    nlohmann::json result;
    if (!exchange->GetPosition(positions)) {
        res.status = 500;
        return ;
    }
    for (auto& item: positions._positions) {
        nlohmann::json pos;
        pos["id"] = get_symbol(item._symbol);
        pos["name"] = to_utf8(item._name);
        pos["price"] = item._price;
        pos["curPrice"] = item._curPrice;
        pos["quantity"] = item._holds;
        pos["valid_quantity"] = item._validHolds;
        result.emplace_back(std::move(pos));
    }
    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
