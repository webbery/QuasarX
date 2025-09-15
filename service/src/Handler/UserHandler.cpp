#include "Handler/UserHandler.h"
#include "HttpHandler.h"
#include "json.hpp"
#include "server.h"
#include "jwt-cpp/jwt.h"
#include "jwt-cpp/traits/nlohmann-json/traits.h"
#include <string>
#include "Util/string_algorithm.h"

using traits = jwt::traits::nlohmann_json;

UserLoginHandler::UserLoginHandler(Server* server): HttpHandler(server) {

}

void UserLoginHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    auto& config = _server->GetConfig();
    String user = params["name"];
    String pwd = params["pwd"];
    nlohmann::json data;
    if (config.AuthenticateUser(user, pwd)) {
        try {
            auto token = jwt::create<traits>()
                    .set_issuer(config.GetIssuer())
                    .set_type("JWT")
                    .set_subject(user)
                    .set_id("QuasarX")
                    .set_issued_now()
                    .set_expires_in(std::chrono::seconds{36000})
                    .set_payload_claim("user", user)
                    .sign(jwt::algorithm::rs256("",
                        config.GetPrivateKey(), "", ""));
            data["tk"] = token;
            switch (_server->GetRunningMode()) {
                case RuningType::Backtest: data["mode"] = "Backtest"; break;
                case RuningType::Real: data["mode"] = "Real"; break;
                default: data["mode"] = "Simulation"; break;
            }
            res.status = 200;
            res.set_content(data.dump(), "application/json");
            return;
        }
        catch (...) {
            res.status = 400;
            data["tk"] = "";
            data["mode"] = "";
            data["message"] = "login fail.";
            res.set_content(data.dump(), "application/json");
            return;
        }
    }
    
    res.status = 200;
    data["tk"] = "";
    data["mode"] = "";
    data["message"] = "login fail.";
    res.set_content(data.dump(), "application/json");
}

ServerStatusHandler::ServerStatusHandler(Server* server): HttpHandler(server) {

}

void ServerStatusHandler::post(const httplib::Request& req, httplib::Response& res)  {

}

void ServerStatusHandler::get(const httplib::Request& req, httplib::Response& res)
{
    nlohmann::json status;
    // quote status

    String result;
#ifdef __linux__
    if (!RunCommand("vmstat 1 1", result)) {
        res.status = 400;
        return;
    }
    std::istringstream iss(result);
    String line;
    int lineNum = 0;
    while (std::getline(iss, line)) {
        lineNum++;
        if (lineNum <= 2)
            continue;

        break;
    }
    Vector<String> info;
    split(line, info, " ");
    auto id = std::stoi(info[14]); // idle
    // CPU
    status["cpu"] = (100.0 - id) / 100.0;
    // memory
    status["mem"] = getMemoryInfo();
#endif
    res.status = 200;
    res.set_content(status.dump(), "application/json");
}

double ServerStatusHandler::getMemoryInfo()
{
    double memory_usage = 0;
#ifdef __linux__
    String result;
    if (!RunCommand("free", result)) {
        return -1;
    }
    std::istringstream iss(result);
    String line;
    int lineNum = 0;
    while (std::getline(iss, line)) {
        lineNum++;
        if (lineNum <= 2)
            continue;

        break;
    }
    Vector<String> info;
    split(line, info, " ");
    memory_usage = std::stol(info[2]) * 1.0/std::stol(info[1]);
#endif
    return memory_usage;
}

SystemConfigHandler::SystemConfigHandler(Server* server): HttpHandler(server) {}

void SystemConfigHandler::get(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json config;
    if (std::filesystem::exists("config.json")) {
        std::ifstream ifs;
        ifs.open("config.json");
        std::string content((std::istreambuf_iterator<char>(ifs)),
            (std::istreambuf_iterator<char>()));
        config = nlohmann::json::parse(content);
        ifs.close();
    }
    else {
        nlohmann::json broker;
        broker["commission"] = { {"fee", 0.0001345}, {"min", 5.0}, {"stamp", 0.001} };
        broker["db"] = DATA_PATH "/broker";
        broker["name"] = "astock";
        broker["type"] = "stock";
        config["broker"].emplace_back(std::move(broker));
        nlohmann::json sim_exchange;
        sim_exchange["api"] = "sim";
        sim_exchange["name"] = "stock-sim";
        sim_exchange["pool"] = std::vector<String>();
        sim_exchange["quote"] = DATA_PATH;
        sim_exchange["trade"] = "";
        sim_exchange["type"] = "stock";
        sim_exchange["desc"] = "";
        config["exchange"].emplace_back(std::move(sim_exchange));
        config["risk"] = std::vector<String>();
        nlohmann::json server;
        server["addr"] = "localhost";
        server["db_path"] = DATA_PATH;
        server["default"] = {
            {"broker", "astock"},
            {"daily", "20:00"},
            {"exchange", {"stock-sim"}},
            {"freerate", 0.0175},
            {"record", {"*"}},
            {"strategy", {}},
        };
        server["jwt"] = "2025_09_jwt_update_key";
        server["notice"] = { {"email", ""}};
        server["passwd"] = "admin";
        server["user"] = "admin";
        server["port"] = 19107;
        server["smtp"] = { {"addr", ""}, {"auth", ""}, {"mail", ""} };
        server["ssl"] = "";
        config["server"] = std::move(server);
    }
    res.set_content(config.dump(), "application/json");
    res.status = 200;
}

void SystemConfigHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    std::ifstream ifs;
    ifs.open("config.json");
    if (!ifs.is_open()) {
        res.status = 400;
        return ;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
        (std::istreambuf_iterator<char>())); // 读取所有内容
    auto config = nlohmann::json::parse(content);
    int operatorType = params["type"];
    switch (operatorType) {
    case 1: // 添加server
        break;
    case 2: // 添加交易所
        if (!AddExchange(config, params["data"])) {
            res.status = 400;
            return;
        }
        break;
    case 3: // 删除server
        break;
    case 4: // 删除交易所
        if (!DeleteExchange(config, params["data"])) {
            res.status = 400;
            return;
        }
        break;
    case 5: // 修改密码
        if (!ChangePassword(config, params["data"]["org"], params["data"]["latest"])) {
            res.status = 400;
            return;
        }
        break;
    case 6: // 修改费率
        if (!ChangeCommission(config, params["data"])) {
            res.status = 400;
            return;
        }
        break;
    case 7: // 修改STMP
        if (!ChangeSMTP(config, params["data"])) {
            res.status = 400;
            return;
        }
        break;
    case 8: // 修改定时任务
        ChangeSchedule(config, params["data"]);
        break;
    default:
        break;
    }
    
    // 修改后写回去
    std::ofstream ofs;
    ofs.open("config.json");
    ofs << config;
    ofs.close();

    res.status = 200;
}

bool SystemConfigHandler::AddExchange(nlohmann::json& config, const nlohmann::json& params) {
    for (auto& item: config["exchange"])  {
        if (item["name"] == params["name"]) { // exist
            return false;
        }
    }
    nlohmann::json exchange;
    exchange["account"] = params["account"];
    exchange["passwd"] = params["passwd"];
    exchange["name"] = params["name"];
    if (params["api"] == "xtp") {
        exchange["quote"] = "119.3.103.38:6002";
        exchange["trade"] = "122.112.139.0:6104";
        exchange["type"] = "stock";
        exchange["log_path"] = "logs";
        exchange["key"] = params["key"];
    }
    else if (params["api"] == "ctp") {
        exchange["quote"] = "180.168.146.187:10211";
        exchange["trade"] = "180.168.146.187:10201";
        exchange["type"] = "future";
        exchange["appid"] = params["appid"];
        exchange["athencode"] = params["athencode"];
    } else {
        return false;
    }
    config["exchange"].emplace_back(std::move(exchange));
    return true;
}

bool SystemConfigHandler::DeleteExchange(nlohmann::json& config, const String& name) {
    
    return true;
}

bool SystemConfigHandler::ChangePassword(nlohmann::json& config, const String& org, const String& latest) {
    if (org == latest)
        return true;

    config["server"]["passwd"] = latest;
    return true;
}

bool SystemConfigHandler::ChangeCommission(nlohmann::json& config, const nlohmann::json& params) {
    return true;
}

bool SystemConfigHandler::ChangeSMTP(nlohmann::json& config, const nlohmann::json& params) {
    config["server"]["smtp"]["addr"] = params["addr"];
    config["server"]["smtp"]["auth"] = params["auth"];
    config["server"]["smtp"]["mail"] = params["mail"];
    return true;
}

bool SystemConfigHandler::ChangeSchedule(nlohmann::json& config, const nlohmann::json& params) {
    config["server"]["default"]["daily"] = params["daily"];
    return true;
}
