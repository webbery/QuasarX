#include "Handler/SectorQuoteHandler.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "json.hpp"
#include "Util/datetime.h"

SectorQuoteHandler::SectorQuoteHandler(Server* server): HttpHandler(server) {

}

void SectorQuoteHandler::get(const httplib::Request& req, httplib::Response& res) {
    String output;
    if (!RunCommand("python tools/sector.py -t quote", output)) {
        res.status = 400;
        res.set_content("{message: 'run python script fail'}", "application/json");
        return;
    }

    List<String> lines;
    split(output, lines, "\n");

    nlohmann::json sectors = nlohmann::json::array();
    for (auto& line : lines) {
        if (line.empty()) continue;

        Vector<String> fields;
        split(line, fields, " ");
        // 排名 板块名称 上涨家数 下跌家数 涨跌额 涨跌幅
        if (fields.size() < 6) continue;

        nlohmann::json item;
        item["rank"] = atoi(fields[0].c_str());
        item["name"] = fields[1];
        item["up_count"] = atoi(fields[2].c_str());
        item["down_count"] = atoi(fields[3].c_str());
        item["change_amount"] = atof(fields[4].c_str());
        item["change_pct"] = atof(fields[5].c_str());
        sectors.emplace_back(std::move(item));
    }

    nlohmann::json result;
    result["status"] = "success";
    result["data"] = sectors;
    result["date"] = ToString(Now(), "%Y-%m-%d");

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
