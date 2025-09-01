#include "HttpHandler.h"
#include "Util/system.h"
#include "json.hpp"

symbol_t GetSymbol(const nlohmann::json& req) {
    String str = req["symbol"];
    return to_symbol(str);
}