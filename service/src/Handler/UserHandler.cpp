#include "Handler/UserHandler.h"
#include "HttpHandler.h"
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
        return false;
    }
    
    // 用户名不能修改,数据及数据库位置不能修改，端口不能修改
    if (params.contains("server")) {
        params["server"]["port"] = 19107;
        params["server"]["user"] = "admin";
        params["server"]["db_path"] = DATA_PATH;
    }
    
    for (auto& item : params["exchange"]) {
        if (item["api"] == "sim") {
            item["quote"] = DATA_PATH;
        }
    }
    for (auto& item : params["broker"]) {
        item["db"] = DATA_PATH "/broker";
    }
    std::ofstream ofs;
    ofs.open("config.json");
    ofs << params;
    ofs.close();

    res.status = 200;
}