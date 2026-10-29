#include "Handler/StabilityHandler.h"
#include "Util/PythonRunner.h"
#include "server.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <fmt/format.h>

// 单会话槽
std::shared_ptr<StabilitySession> StabilityHandler::s_activeSession = nullptr;
std::mutex StabilityHandler::s_sessionMtx;

// 会话获取
SessionAcquireResult<StabilitySession> StabilityHandler::tryAcquireSession(StabilityAction kind) {
    std::lock_guard<std::mutex> lk(s_sessionMtx);
    if (!s_activeSession || s_activeSession->_done) {
        return {SessionAcquireResult<StabilitySession>::CanCreate, nullptr};
    }
    if (s_activeSession->_kind == kind) {
        return {SessionAcquireResult<StabilitySession>::Reused, s_activeSession};
    }
    return {SessionAcquireResult<StabilitySession>::Conflict, s_activeSession};
}

void StabilityHandler::respondConflict(httplib::Response& res, const String& sessionId, StabilityAction kind) {
    res.status = 409;
    nlohmann::json resp;
    resp["message"] = "another stability scan is running";
    resp["running_session_id"] = sessionId;
    resp["running_kind"] = stabilityActionToString(kind);
    res.set_content(resp.dump(), "application/json");
}

void StabilityHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    String action = params.value("action", "");
    if (action == "scan") {
        handleScan(params, res);
    } else if (action == "cancel") {
        handleCancel(params, res);
    } else if (action == "delete") {
        handleDelete(params, res);
    } else {
        res.status = 400;
        res.set_content(R"json({"message":"invalid action (scan|cancel|delete)"})json", "application/json");
    }
}

void StabilityHandler::get(const httplib::Request& req, httplib::Response& res) {
    String action = req.get_param_value("action");
    if (action == "events") {
        handleEvents(req, res);
    } else if (action == "status") {
        handleStatus(req, res);
    } else if (action == "result") {
        handleResult(req, res);
    } else if (action == "list") {
        handleList(req, res);
    } else {
        res.status = 400;
        res.set_content(R"json({"message":"invalid action (events|status|result|list)"})json", "application/json");
    }
}

void StabilityHandler::handleScan(const nlohmann::json& params, httplib::Response& res) {
    // 参数校验
    String csvPath = params.value("csv_path", "");
    if (csvPath.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing csv_path"})", "application/json");
        return;
    }
    if (!std::filesystem::exists(csvPath)) {
        res.status = 400;
        res.set_content(R"({"message":"csv_path not found"})", "application/json");
        return;
    }

    // 幂等检查
    auto acquire = tryAcquireSession(StabilityAction::Scan);
    if (acquire.status == SessionAcquireResult<StabilitySession>::Reused) {
        nlohmann::json resp;
        resp["session_id"] = acquire.existing->_sessionId;
        resp["run_id"] = acquire.existing->_runId;
        resp["status"] = "running";
        res.set_content(resp.dump(), "application/json");
        return;
    }
    if (acquire.status == SessionAcquireResult<StabilitySession>::Conflict) {
        respondConflict(res, acquire.existing->_sessionId, acquire.existing->_kind);
        return;
    }

    // 创建会话
    auto session = std::make_shared<StabilitySession>();
    session->_sessionId = fmt::format("stab_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    session->_kind = StabilityAction::Scan;
    session->_csvPath = csvPath;

    // 生成 run_id 和输出路径
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm);
    auto seq = std::chrono::steady_clock::now().time_since_epoch().count() % 10000;
    session->_runId = fmt::format("stab_{}_{:04d}", time_buf, seq);

    String dbPath = _server->GetConfig().GetDatabasePath();
    String outDir = dbPath + "/reports/stability/" + session->_runId;
    std::filesystem::create_directories(outDir);
    session->_jsonOutPath = outDir + "/stability.json";

    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        s_activeSession = session;
    }

    nlohmann::json resp;
    resp["session_id"] = session->_sessionId;
    resp["run_id"] = session->_runId;
    resp["status"] = "started";
    res.set_content(resp.dump(), "application/json");

    // 启动后台线程
    std::thread([this, session, params]() {
        auto sendSSE = [session](const String& type, const nlohmann::json& data) {
            session->pushEvent(type, data);
        };

        try {
            sendSSE("step", {{"step", "validate"}, {"status", "start"}});

            // 构建 Python 参数
            std::vector<std::string> args = {
                "--csv", session->_csvPath,
                "--json-out", session->_jsonOutPath,
                "--run-id", session->_runId,
                "--emit-events",
            };

            // 可选参数
            if (params.contains("strategy_id")) {
                args.push_back("--strategy-id");
                args.push_back(params["strategy_id"].get<String>());
            }
            if (params.contains("ohlcv_cache")) {
                args.push_back("--ohlcv-cache");
                args.push_back(params["ohlcv_cache"].get<String>());
            }
            if (params.contains("entry")) {
                args.push_back("--entry");
                args.push_back(params["entry"].get<String>());
            }
            if (params.contains("return_horizon")) {
                args.push_back("--return-horizon");
                args.push_back(std::to_string(params["return_horizon"].get<int>()));
            }
            if (params.contains("horizons")) {
                String horizons;
                for (auto& h : params["horizons"]) {
                    if (!horizons.empty()) horizons += ",";
                    horizons += std::to_string(h.get<int>());
                }
                args.push_back("--horizons");
                args.push_back(horizons);
            }
            if (params.contains("checks")) {
                String checks;
                for (auto& c : params["checks"]) {
                    if (!checks.empty()) checks += ",";
                    checks += c.get<String>();
                }
                args.push_back("--checks");
                args.push_back(checks);
            }
            if (params.contains("features")) {
                args.push_back("--features");
                args.push_back(params["features"].get<String>());
            }
            if (params.contains("signals")) {
                args.push_back("--signals");
                args.push_back(params["signals"].get<String>());
            }
            if (params.contains("ref_mode")) {
                args.push_back("--ref-mode");
                args.push_back(params["ref_mode"].get<String>());
            }
            if (params.contains("ref_frac")) {
                args.push_back("--ref-frac");
                args.push_back(std::to_string(params["ref_frac"].get<double>()));
            }
            if (params.contains("bins")) {
                args.push_back("--bins");
                args.push_back(std::to_string(params["bins"].get<int>()));
            }
            if (params.contains("binning")) {
                args.push_back("--binning");
                args.push_back(params["binning"].get<String>());
            }
            if (params.contains("psi_warn")) {
                args.push_back("--psi-warn");
                args.push_back(std::to_string(params["psi_warn"].get<double>()));
            }
            if (params.contains("psi_alert")) {
                args.push_back("--psi-alert");
                args.push_back(std::to_string(params["psi_alert"].get<double>()));
            }
            if (params.contains("n_blocks")) {
                args.push_back("--n-blocks");
                args.push_back(std::to_string(params["n_blocks"].get<int>()));
            }
            if (params.contains("quantiles")) {
                args.push_back("--quantiles");
                args.push_back(std::to_string(params["quantiles"].get<int>()));
            }
            if (params.contains("min_names")) {
                args.push_back("--min-names");
                args.push_back(std::to_string(params["min_names"].get<int>()));
            }
            if (params.contains("min_bucket_names")) {
                args.push_back("--min-bucket-names");
                args.push_back(std::to_string(params["min_bucket_names"].get<int>()));
            }

            auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
            auto interpreter = pyEnv.resolve(params.value("py_env", ""));

            sendSSE("step", {{"step", "validate"}, {"status", "done"}});
            sendSSE("step", {{"step", "scan"}, {"status", "start"},
                            {"csv_path", session->_csvPath},
                            {"json_out", session->_jsonOutPath}});

            auto runner = std::make_shared<PythonRunner>();
            {
                std::lock_guard<std::mutex> lk(session->_mtx);
                session->_runner = runner;
            }

            INFO("[Stability] Starting scan: run_id={}, csv={}", session->_runId, session->_csvPath);
            if (!runner->start("tools/stability_scan.py", args, interpreter)) {
                sendSSE("error", {{"step", "scan"}, {"msg", "failed to start stability_scan.py"}});
                std::lock_guard<std::mutex> lk(session->_mtx);
                session->_runner = nullptr;
                session->_hasError = true;
                session->_result = {{"error", "failed to start scan script"}};
                session->_done = true;
                return;
            }

            String resultLine, stderrLines;
            bool childExited = false;
            PythonOutput out;
            while (runner->readLine(out, QS_PYTHON_READLINE_TIMEOUT_MS)) {
                if (session->_cancelled) {
                    INFO("[Stability] Session {} cancelled", session->_sessionId);
                    runner->kill();
                    break;
                }
                if (out.type == PythonOutput::DONE) {
                    childExited = true;
                    break;
                }
                if (out.type == PythonOutput::STDOUT) {
                    String lineType = jsonLineType(out.line);
                    if (lineType == "result") {
                        resultLine = out.line;
                    } else if (lineType == "error") {
                        sendSSE("warning", {{"line", out.line}});
                        if (stderrLines.size() < 1000) stderrLines += jsonLineMessage(out.line) + "\n";
                    } else {
                        try {
                            auto j = nlohmann::json::parse(out.line);
                            String t = j.value("type", "info");
                            j.erase("type");
                            sendSSE(t, j);
                        } catch (...) {
                            sendSSE("log", {{"line", out.line}});
                        }
                    }
                } else if (out.type == PythonOutput::STDERR) {
                    sendSSE("warning", {{"line", out.line}});
                    if (stderrLines.size() < 1000) stderrLines += out.line + "\n";
                }
            }

            std::lock_guard<std::mutex> lk(session->_mtx);
            session->_runner = nullptr;

            if (session->_cancelled) {
                session->_hasError = true;
                session->_result = {{"error", "cancelled by user"}};
            } else if (resultLine.empty()) {
                String msg;
                if (!childExited) {
                    msg = fmt::format("空闲超时（{}s 内无任何输出），已终止 Python 进程",
                                      QS_PYTHON_READLINE_TIMEOUT_MS / 1000);
                } else {
                    msg = stderrLines.empty() ? "stability_scan.py 未输出 result" : stderrLines.substr(0, 500);
                }
                while (!msg.empty() && msg.back() == '\n') msg.pop_back();
                session->_hasError = true;
                session->_result = {{"error", String("扫描失败: ") + msg}};
            } else {
                try {
                    session->_result = nlohmann::json::parse(resultLine);
                } catch (...) {
                    session->_hasError = true;
                    session->_result = {{"error", "扫描结果解析失败"}};
                }
            }
            session->_done = true;

        } catch (const std::exception& e) {
            FATAL("[Stability] uncaught exception: {}", e.what());
            sendSSE("error", {{"step", "scan"}, {"msg", String("internal error: ") + e.what()}});
            std::lock_guard<std::mutex> lk(session->_mtx);
            session->_runner = nullptr;
            session->_hasError = true;
            session->_result = {{"error", String("internal error: ") + e.what()}};
            session->_done = true;
        } catch (...) {
            FATAL("[Stability] unknown uncaught exception");
            sendSSE("error", {{"step", "scan"}, {"msg", "unknown internal error"}});
            std::lock_guard<std::mutex> lk(session->_mtx);
            session->_runner = nullptr;
            session->_hasError = true;
            session->_result = {{"error", "unknown internal error"}};
            session->_done = true;
        }
    }).detach();
}

void StabilityHandler::handleCancel(const nlohmann::json& params, httplib::Response& res) {
    String sessionId = params.value("session_id", "");
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing session_id"})", "application/json");
        return;
    }

    std::shared_ptr<StabilitySession> session;
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && s_activeSession->_sessionId == sessionId) {
            session = s_activeSession;
        }
    }
    if (!session) {
        res.status = 404;
        res.set_content(R"({"message":"session not found"})", "application/json");
        return;
    }

    INFO("[Stability] Cancelling session {}", sessionId);
    session->cancel();

    nlohmann::json resp;
    resp["message"] = "cancelled";
    resp["session_id"] = sessionId;
    res.set_content(resp.dump(), "application/json");
}

void StabilityHandler::handleDelete(const nlohmann::json& params, httplib::Response& res) {
    String runId = params.value("run_id", "");
    if (runId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing run_id"})", "application/json");
        return;
    }

    // 安全检查：run_id 必须以 stab_ 开头
    if (runId.rfind("stab_", 0) != 0) {
        res.status = 400;
        res.set_content(R"({"message":"invalid run_id format"})", "application/json");
        return;
    }

    String dbPath = _server->GetConfig().GetDatabasePath();
    String outDir = dbPath + "/reports/stability/" + runId;

    if (!std::filesystem::exists(outDir)) {
        res.status = 404;
        res.set_content(R"({"message":"result not found"})", "application/json");
        return;
    }

    std::error_code ec;
    std::filesystem::remove_all(outDir, ec);
    if (ec) {
        res.status = 500;
        res.set_content(R"({"message":"failed to delete result"})", "application/json");
        return;
    }

    nlohmann::json resp;
    resp["message"] = "deleted";
    resp["run_id"] = runId;
    res.set_content(resp.dump(), "application/json");
}

void StabilityHandler::handleEvents(const httplib::Request& req, httplib::Response& res) {
    String sessionId = req.get_param_value("session_id");
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing session_id"})", "application/json");
        return;
    }

    std::shared_ptr<StabilitySession> session;
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && s_activeSession->_sessionId == sessionId) {
            session = s_activeSession;
        }
    }
    if (!session) {
        res.status = 404;
        res.set_content(R"({"message":"session not found"})", "application/json");
        return;
    }

    ServeJobSession(res, session);
}

void StabilityHandler::handleStatus(const httplib::Request& req, httplib::Response& res) {
    String sessionId = req.get_param_value("session_id");
    if (sessionId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing session_id"})", "application/json");
        return;
    }

    std::shared_ptr<StabilitySession> session;
    {
        std::lock_guard<std::mutex> lk(s_sessionMtx);
        if (s_activeSession && s_activeSession->_sessionId == sessionId) {
            session = s_activeSession;
        }
    }
    if (!session) {
        res.status = 404;
        res.set_content(R"({"message":"session not found"})", "application/json");
        return;
    }

    res.set_content(session->snapshot().dump(), "application/json");
}

void StabilityHandler::handleResult(const httplib::Request& req, httplib::Response& res) {
    String runId = req.get_param_value("run_id");
    if (runId.empty()) {
        res.status = 400;
        res.set_content(R"({"message":"missing run_id"})", "application/json");
        return;
    }

    // 安全检查
    if (runId.rfind("stab_", 0) != 0) {
        res.status = 400;
        res.set_content(R"({"message":"invalid run_id format"})", "application/json");
        return;
    }

    String dbPath = _server->GetConfig().GetDatabasePath();
    String jsonPath = dbPath + "/reports/stability/" + runId + "/stability.json";

    if (!std::filesystem::exists(jsonPath)) {
        res.status = 404;
        res.set_content(R"({"message":"result not found"})", "application/json");
        return;
    }

    std::ifstream ifs(jsonPath);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    res.set_content(content, "application/json");
}

void StabilityHandler::handleList(const httplib::Request& req, httplib::Response& res) {
    String dbPath = _server->GetConfig().GetDatabasePath();
    String baseDir = dbPath + "/reports/stability";

    if (!std::filesystem::exists(baseDir)) {
        nlohmann::json resp = nlohmann::json::array();
        res.set_content(resp.dump(), "application/json");
        return;
    }

    nlohmann::json items = nlohmann::json::array();
    for (auto& entry : std::filesystem::directory_iterator(baseDir)) {
        if (!entry.is_directory()) continue;
        String runId = entry.path().filename().string();
        if (runId.rfind("stab_", 0) != 0) continue;

        String jsonPath = entry.path().string() + "/stability.json";
        if (!std::filesystem::exists(jsonPath)) continue;

        try {
            std::ifstream ifs(jsonPath);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            auto json = nlohmann::json::parse(content);

            nlohmann::json item;
            item["run_id"] = runId;
            item["created_at"] = json.value("created_at", "");
            item["strategy_id"] = json.value("strategy_id", "");
            item["csv_path"] = json.value("csv_path", "");
            item["checks"] = json.value("checks", nlohmann::json::array());
            item["summary"] = json.value("summary", nlohmann::json::object());
            items.push_back(item);
        } catch (...) {
            // 跳过解析失败的条目
        }
    }

    // 按 created_at 倒序
    std::sort(items.begin(), items.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("created_at", "") > b.value("created_at", "");
    });

    res.set_content(items.dump(), "application/json");
}
