#include "Handler/PositionHandler.h"
#include "server.h"

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
    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
