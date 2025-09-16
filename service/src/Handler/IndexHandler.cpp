#include "Handler/IndexHandler.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "json.hpp"
#include "Bridge/exchange.h"
#include "server.h"
#include "std_header.h"
#include <string>

IndexHandler::IndexHandler(Server* server): HttpHandler(server),_times(0) {}

void IndexHandler::get(const httplib::Request& req, httplib::Response& res) {
    String cmd = "python tool/quote_index.py " + std::to_string(_times++);
    String output;
    RunCommand(cmd, output);
    Vector<String> lines;
    split(output, lines, "\n");
    if (lines.size() == 1) {
        res.status = 400;
        return;
    }

    nlohmann::json jsn;
    for (int i = 1; i < lines.size(); ++i) {
        Vector<String> cols;
        split(lines[i], cols, " ");
        nlohmann::json info;
        info["code"] = cols[1];
        info["price"] = cols[2];
        info["rate"] = cols[3];
        jsn.emplace_back(std::move(info));
    }
    res.set_content(jsn.dump(), "application/json");
}
