#include "Handler/UserHandler.h"
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
    if (config.AuthenticateUser(user, pwd)) {
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
        nlohmann::json data;
        data["tk"] = token;
        switch (_server->GetRunningMode()) {
            case RuningType::Backtest: data["mode"] = "Backtest"; break;
            case RuningType::Real: data["mode"] = "Real"; break;
            default: data["mode"] = "Simulation"; break;
        }
        res.status = 200;
        res.set_content(data.dump(), "application/json");
    } else {
        res.status = 200;
        res.set_content("{status: -1, message: 'login fail.'}", "application/json");
    }
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
