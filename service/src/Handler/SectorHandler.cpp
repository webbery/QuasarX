#include "Handler/SectorHandler.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "json.hpp"
#include "Util/datetime.h"

SectorHandler::SectorHandler(Server* server):HttpHandler(server) {

}

void SectorHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& params = req.params;
    if (!params.contains("type")) {
        res.status = 400;
        res.set_content("{message: 'query must contain `type`'}", "application/json");
        return;
    }
    auto itr = params.find("type");
    auto type = atol(itr->second.c_str());
    nlohmann::json sectors;
    if (type == 0) {
        // TODO: 先检查本地文件是否有当天数据
        
        String output;
        if (!RunCommand("python tools/sector.py -t today", output)) {
            res.status = 400;
            res.set_content("{message: 'run python script fail'}", "application/json");
            return;
        }
        List<String> order_sectors;
        split(output, order_sectors, "\n");
        for (auto& sector_info: order_sectors) {
            Vector<String> flow;
            split(sector_info, flow, " ");
            if (flow.size() != 6)
                continue;
            nlohmann::json info, today_info;
            info["name"] = flow[0];
            today_info["date"] = ToString(Now(), "%Y-%m-%d");
            today_info["main"] = atof(flow[1].c_str());
            today_info["supbig"] = atof(flow[2].c_str());
            today_info["big"] = atof(flow[3].c_str());
            today_info["mid"] = atof(flow[4].c_str());
            today_info["small"] = atof(flow[5].c_str());
            info["value"].emplace_back(today_info);
            sectors.emplace_back(std::move(info));
        }
    }
    else {
        auto itr = params.find("name");
        String name = itr->second;
        String output;
        if (!RunCommand("python tools/sector.py -t today", output)) {
            res.status = 400;
            res.set_content("{message: 'run python script fail'}", "application/json");
            return;
        }
        List<String> order_sectors;
        split(output, order_sectors, "\n");
        for (auto& sector_info : order_sectors) {
            Vector<String> flow;
            split(sector_info, flow, " ");
            if (flow.size() != 6)
                continue;
            nlohmann::json info, today_info;
            info["name"] = name;
            today_info["date"] = flow[0];
            today_info["main"] = atof(flow[1].c_str());
            today_info["supbig"] = atof(flow[2].c_str());
            today_info["big"] = atof(flow[3].c_str());
            today_info["mid"] = atof(flow[4].c_str());
            today_info["small"] = atof(flow[5].c_str());
            info["value"].emplace_back(today_info);
            sectors.emplace_back(std::move(info));
        }
    }
    res.status = 200;
    res.set_content(sectors.dump(), "application/json");
}
