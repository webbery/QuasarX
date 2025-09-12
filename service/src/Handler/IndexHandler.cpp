#include "Handler/IndexHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "Bridge/exchange.h"
#include "server.h"
#include <string>

IndexHandler::IndexHandler(Server* server): HttpHandler(server),_times(0) {}

void IndexHandler::get(const httplib::Request& req, httplib::Response& res) {
    String cmd = "python tool/quote_index.py " + std::to_string(_times++);
    String output;
    RunCommand(cmd, output);
    nlohmann::json jsn;
    jsn[""] = "";
    res.set_content(jsn.dump(), "application/json");
}
