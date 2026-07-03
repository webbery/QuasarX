#include "Handler/PythonRunnerHandler.h"
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

    PythonRunner runner;
    if (!runner.start(script, args, interpreter)) {
        res.status = 500;
        res.set_content(R"({"message":"failed to start script"})", "application/json");
        return;
    }

    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");

    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink& sink) {
            if (!sink.is_writable() || Server::IsExit()) {
                runner.kill();
                return false;
            }

            PythonOutput out;
            if (runner.readLine(out, 5000)) {
                if (out.type == PythonOutput::DONE) {
                    auto msg = format_sse("done",
                        {{"exit_code", std::to_string(out.exit_code)}});
                    sink.write(msg.c_str(), msg.size());
                    return false;
                }
                auto msg = format_sse(
                    out.type == PythonOutput::STDOUT ? "output" : "error",
                    {{"line", out.line}});
                sink.write(msg.c_str(), msg.size());
            }
            return true;
        });
}
