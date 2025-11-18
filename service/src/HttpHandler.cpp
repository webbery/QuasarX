#include "HttpHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "Bridge/exchange.h"

symbol_t GetSymbol(const nlohmann::json& req) {
    String str = req["symbol"];
    return to_symbol(str);
}

void ProcessError(char error, nlohmann::json& result, httplib::Response& res) {
    switch (error) {
    case ERROR_INSERT_LIMIT:
        res.status = 400;
        result["status"] = ERROR_INSERT_LIMIT;
    break;
    default:
    break;
    }
}
