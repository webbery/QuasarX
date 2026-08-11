#include "Handler/SRHandler.h"
#include "Util/PythonRunner.h"
#include "server.h"
#include "Util/string_algorithm.h"
#include <filesystem>

void SRHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    // ── 解析参数 ──
    String task = params.value("task", "volatility");
    String symbol = params.value("symbol", "");
    String csvPath = params.value("csv_path", "");
    int horizon = params.value("horizon", 5);
    String operators = params.value("operators", "safe");
    int niterations = params.value("niterations", 200);
    int populations = params.value("populations", 30);
    int maxsize = params.value("maxsize", 15);
    int topk = params.value("topk", 10);
    int timeout = params.value("timeout", 300);
    String features = params.value("features", "");
    String env = params.value("env", "");

    // ── 校验 ──
    if (symbol.empty() && csvPath.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"需要 'symbol' 或 'csv_path'"})", "application/json");
        return;
    }

    // 如果只给了 symbol，自动定位 CSV
    if (csvPath.empty()) {
        String dbPath = _server->GetConfig().GetDatabasePath();
        csvPath = dbPath + "/A_hfq/" + symbol + ".csv";
    }

    if (!std::filesystem::exists(csvPath)) {
        res.status = 404;
        String msg = R"({"message":"CSV 不存在: )" + csvPath + R"("})";
        res.set_content(msg.c_str(), "application/json");
        return;
    }

    // ── 构建 Python 脚本参数 ──
    std::vector<std::string> args = {
        "--csv-path", csvPath,
        "--symbol", symbol,
        "--horizon", std::to_string(horizon),
        "--operators", operators,
        "--niterations", std::to_string(niterations),
        "--populations", std::to_string(populations),
        "--maxsize", std::to_string(maxsize),
        "--topk", std::to_string(topk),
        "--timeout", std::to_string(timeout),
    };
    if (!features.empty()) {
        args.push_back("--features");
        args.push_back(features);
    }

    // ── Python 环境 ──
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env);

    INFO("[SR] 启动符号回归: task={}, symbol={}, horizon={}, csv={}", task, symbol, horizon, csvPath);

    // ── 启动 Python 脚本 ──
    PythonRunner runner;
    if (!runner.start("tools/symbolic_regression.py", args, interpreter)) {
        res.status = 500;
        res.set_content(R"({"message":"无法启动符号回归脚本"})", "application/json");
        return;
    }

    // ── SSE 流式输出 ──
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");

    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink& sink) {
            if (!sink.is_writable() || Server::IsExit()) {
                runner.kill();
                sink.done();
                return false;
            }

            PythonOutput out;
            if (runner.readLine(out, 10000)) {
                if (out.type == PythonOutput::DONE) {
                    auto msg = format_sse("done",
                        {{"exit_code", std::to_string(out.exit_code)}});
                    sink.write(msg.c_str(), msg.size());
                    sink.done();
                    return false;
                }
                String eventType = (out.type == PythonOutput::STDOUT) ? "output" : "error";
                auto msg = format_sse(eventType, {{"line", out.line}});
                sink.write(msg.c_str(), msg.size());
            }
            return true;
        });
}
