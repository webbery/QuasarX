#include "Handler/PythonRunnerHandler.h"
#include "Util/JobSession.h"
#include "Util/PythonRunner.h"
#include "Util/string_algorithm.h"
#include "server.h"

void PythonRunnerHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    auto script = params.value("script", "");
    if (script.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing 'script'"})", "application/json");
        return;
    }

    // 解析 Python 环境
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(params.value("env", ""));

    // 脚本参数
    std::vector<std::string> args;
    if (params.contains("args") && params["args"].is_array()) {
        for (auto& a : params["args"])
            args.push_back(a.get<std::string>());
    }

    auto runner = std::make_shared<PythonRunner>();
    if (!runner->start(script, args, interpreter)) {
        res.status = 500;
        res.set_content(R"({"message":"failed to start script"})", "application/json");
        return;
    }

    ServePythonRunner(res, runner, 5000);
}
