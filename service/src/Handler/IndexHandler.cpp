#include "Handler/IndexHandler.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "json.hpp"
#include "Bridge/exchange.h"
#include "nng/nng.h"
#include "server.h"
#include "std_header.h"
#include <string>

IndexHandler::IndexHandler(Server* server): HttpHandler(server),_times(0) {
}

IndexHandler::~IndexHandler() {
}

void IndexHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto exchange = _server->GetAvaliableStockExchange();
    auto indx1 = to_symbol("000001", "sh");
    auto quote = exchange->GetQuote(indx1);
    nlohmann::json jsn;
    if (quote._time != 0) {
        nlohmann::json info;
        info["code"] = "000001";
        info["price"] = quote._close;
        info["rate"] = quote._close / quote._open - 1;
        jsn.emplace_back(std::move(info));
    } else {
        String cmd = "python tools/quote_index.py " + std::to_string(_times++);
        String output;
        RunCommand(cmd, output);
        Vector<String> lines;
        split(output, lines, "\n");
        if (lines.size() == 1) {
            res.status = 400;
            return;
        }

        for (int i = 1; i < lines.size(); ++i) {
            if ( lines[i].empty())
                break;
            Vector<String> cols;
            // INFO("{}", lines[i]);
            split(lines[i], cols, " ");
            nlohmann::json info;
            info["code"] = cols[1];
            info["price"] = cols[2];
            info["rate"] = cols[3];
            jsn.emplace_back(std::move(info));
        }
    }
    res.status = 200;
    res.set_content(jsn.dump(), "application/json");
}
