#include "Handler/IndexHandler.h"
#include "json.hpp"
#include "Bridge/exchange.h"
#include "server.h"

IndexHandler::IndexHandler(Server* server): HttpHandler(server) {}

void IndexHandler::get(const httplib::Request& req, httplib::Response& res) {
    String id = req.get_param_value("id");
    auto exchange = _server->GetExchange(ExchangeType::EX_XTP);
    auto quote = exchange->GetQuote(to_symbol(id));
    nlohmann::json jsn;
}
