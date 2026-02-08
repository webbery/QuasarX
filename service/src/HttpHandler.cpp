#include "HttpHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "Bridge/exchange.h"

symbol_t GetSymbol(const nlohmann::json& req) {
    String str = req["symbol"];
    return to_symbol(str);
}

void ProcessError(char error, httplib::Response& res) {
    nlohmann::json result;
    switch (error) {
    case ERROR_INSERT_LIMIT:
        res.status = 400;
        result["status"] = ERROR_INSERT_LIMIT;
        break;
    case ERROR_CANCEL_LIMIT:
        res.status = 400;
        result["status"] = ERROR_CANCEL_LIMIT;
        break;
    case ERROR_ORDER_INSERT:
        res.status = 400;
        result["status"] = ERROR_ORDER_INSERT;
        break;
    case ERROR_NO_SECURITY:
        res.status = 400;
        result["status"] = ERROR_NO_SECURITY;
        break;
    case ERROR_ORDER_FORBID:
        res.status = 400;
        result["status"] = ERROR_ORDER_FORBID;
        break;
    default:
        LOG("ERROR not set: {}", error);
        break;
    }
    res.set_content(result.dump(), "application/json");
}
