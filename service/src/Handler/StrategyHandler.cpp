#include "Handler/StrategyHandler.h"
#include "Util/MultipartHelper.h"
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <yas/serialize.hpp>
#include "AgentSubSystem.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "json.hpp"
#include "nng/nng.h"
#include "server.h"
#include "Bridge/exchange.h"
#include "Bridge/CapitalPool.h"
#include "BrokerSubSystem.h"
#include "StrategySubSystem.h"
#include <boost/hana.hpp>
#include "Nodes/FunctionNode.h"
#include "Nodes/NeuralNetworkNode.h"
#include "Nodes/DebugNode.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/SignalNode.h"

StrategyHandler::StrategyHandler(Server* server)
: _close(true), _main(nullptr),HttpHandler(server) {
  sock.id = 0;
}

StrategyHandler::~StrategyHandler() {
  if (_main) {
    _close = true;
    _main->join();
    delete _main;
  }
}

void StrategyHandler::get(const httplib::Request& req, httplib::Response& res)
{
    // 响应结构对应 restapi.yaml 中的 StrategyListResponse schema：
    //   {
    //     "strategies": [StrategyStatus, ...],   // 已成功初始化的策略
    //     "failed":     [StrategyInitFailure, ...] // 初始化失败的策略（含失败节点信息）
    //   }
    // 保留 failed 字段供前端展示，避免节点初始化失败被服务端静默吞掉
    // （典型场景：xgboost 模型文件缺失、节点参数不合法等）
    auto strategySys = _server->GetStrategySystem();
    auto flow = strategySys->GetFlowSubsystem();
    auto broker = _server->GetBrokerSubSystem();
    auto capitalPool = broker ? broker->GetCapitalPool() : nullptr;

    nlohmann::json strategies = nlohmann::json::array();
    if (flow) {
        auto names = flow->GetFlowNames();
        for (auto& name : names) {
            nlohmann::json item;
            item["name"] = to_utf8(name.c_str());
            item["running"] = flow->IsRunning(name);
            item["epochCount"] = (int64_t)flow->GetEpochCount(name);
            item["lastHeartbeat"] = (int64_t)flow->GetLastHeartbeat(name);
            item["lastEvoke"] = (int64_t)flow->GetLastEvoke(name);

            // 补充策略资金信息
            if (capitalPool) {
                auto capitalInfo = capitalPool->get(name);
                item["availableCapital"] = capitalInfo.available;     // 可用资金额度
                item["usedCapital"] = capitalInfo.used();             // 账面资金（市值）
                item["allocatedCapital"] = capitalInfo.allocated;     // 分配总额
            } else {
                item["availableCapital"] = 0.0;
                item["usedCapital"] = 0.0;
                item["allocatedCapital"] = 0.0;
            }

            strategies.push_back(item);
        }
    }

    // 加载失败的策略列表（含原因），供前端展示
    nlohmann::json failed = nlohmann::json::array();
    if (strategySys) {
        auto failedResults = strategySys->GetFailedStrategies();
        for (auto& fr : failedResults) {
            nlohmann::json item;
            item["name"] = to_utf8(fr._name.c_str());
            item["error"] = to_utf8(fr._errorMessage.c_str());
            nlohmann::json failedNode;
            failedNode["id"] = fr._failedNodeId;
            failedNode["label"] = to_utf8(fr._failedNodeLabel.c_str());
            failedNode["type"] = to_utf8(fr._failedNodeType.c_str());
            item["failed_node"] = failedNode;
            failed.push_back(item);
        }
    }

    nlohmann::json result;
    result["strategies"] = strategies;
    result["failed"] = failed;

    res.status = 200;
    res.set_content(result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), "application/json");
}

void StrategyHandler::post(const httplib::Request& req, httplib::Response& res) {
    // multipart/form-data（含 model_* 文件 part）走单独解析路径
    bool isMultipart = req.is_multipart_form_data();
    nlohmann::json params;
    bool forceDeploy = false;
    if (isMultipart) {
        nlohmann::json scriptJson;
        String mpName;
        String errMsg;
        if (!ParseMultipartScript(req, scriptJson, mpName, errMsg)) {
            res.status = 400;
            nlohmann::json err;
            err["message"] = errMsg;
            res.set_content(err.dump(), "application/json");
            return;
        }
        params["script"] = scriptJson;
        if (!mpName.empty()) {
            params["name"] = mpName;
        }
        if (req.has_file("force")) {
            forceDeploy = req.get_file_value("force").content == "true";
        }
    } else {
        params = nlohmann::json::parse(req.body);
        forceDeploy = params.value("force", false);
    }

    // 检查是否是验证请求
    if (params.contains("action") && params["action"] == "validate") {
        validate(params, res);
        return;
    }

    // 检查是否是加载请求（只 InitStrategy，不保存文件、不 Run）
    if (params.contains("action") && params["action"] == "load") {
        load(params, res);
        return;
    }

    // 回收所有策略资金（测试隔离 / 管理接口）
    if (params.contains("action") && params["action"] == "reclaim_all") {
        auto* broker = _server->GetBrokerSubSystem();
        if (broker && broker->GetCapitalPool()) {
            broker->GetCapitalPool()->reclaimAll();
            nlohmann::json result;
            result["message"] = "all capital reclaimed";
            res.status = 200;
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content(R"({"error": "capital pool not available"})", "application/json");
        }
        return;
    }

    int mode = params.value("mode", 0);
    if (mode == 2) {// 暂停
        String name = params.value("name", "");
        stop(name, res);
    }
    else if (mode == 1) {// 运行
        String name = params.value("name", "");
        run(name, res);
    } else {
        // 部署并运行；multipart 时把 req 一起传给 deploy（用于读 model_* parts）
        INFO("[StrategyHandler] params type={}, is_object={}, keys={}",
             params.type_name(), params.is_object(), params.dump());
        if (isMultipart) deploy(params, req, res, forceDeploy);
        else deploy(params, res, forceDeploy);
    }
}

void StrategyHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    if (!params.is_object()) {
        res.status = 400;
        nlohmann::json err;
        err["message"] = "invalid param: expected JSON object";
        res.set_content(err.dump(), "application/json");
        return;
    }
    String name = params.value("name", "");
    auto strategySys = _server->GetStrategySystem();
    strategySys->Stop(name);
    strategySys->UninstallStrategy(name);
    strategySys->ReleaseStrategy(name);

    String erase_file(SCRIPTS_DIR);
    erase_file += "/" + name;
    std::filesystem::remove(erase_file);
    res.status = 200;
    nlohmann::json result;
    result["message"] = "success";
    res.set_content(result.dump(), "application/json");
}

void StrategyHandler::deploy(const nlohmann::json& param, httplib::Response& res, bool force) {
    deployImpl(param, nullptr, res, force);
}

void StrategyHandler::deploy(const nlohmann::json& param, const httplib::Request& req, httplib::Response& res, bool force) {
    deployImpl(param, &req, res, force);
}

void StrategyHandler::deployImpl(const nlohmann::json& param, const httplib::Request* reqPtr, httplib::Response& res, bool force) {
    INFO("[StrategyHandler] deployImpl: param type={}, is_object={}", param.type_name(), param.is_object());
    if (!param.is_object()) {
        WARN("[StrategyHandler] deployImpl: param is not an object, type={}", param.type_name());
        res.status = 400;
        nlohmann::json err;
        err["message"] = "invalid param: expected JSON object";
        err["type"] = param.type_name();
        res.set_content(err.dump(), "application/json");
        return;
    }
    nlohmann::json scriptJson = param.value("script", nlohmann::json());
    String scripts = scriptJson.dump();
    String name = param.value("name", "");
    INFO("[StrategyHandler] deployImpl: name='{}', script size={}, force={}", name, scripts.size(), force);

    // 检查同名策略是否正在运行
    auto strategySys = _server->GetStrategySystem();
    auto flow = strategySys->GetFlowSubsystem();
    if (flow && flow->IsRunning(name)) {
        if (!force) {
            WARN("[StrategyHandler] Strategy '{}' is running, deploy rejected (force=false)", name);
            res.status = 409;
            nlohmann::json err;
            err["message"] = "策略正在运行中，请先停止或确认重新部署";
            err["name"] = name;
            err["running"] = true;
            res.set_content(err.dump(), "application/json");
            return;
        }
        INFO("[StrategyHandler] Strategy '{}' is running, force deploy → stopping first", name);
        strategySys->Stop(name);
    }

    // 使用 std::filesystem::path 正确拼接路径（跨平台）
    std::filesystem::path scripts_dir(SCRIPTS_DIR);

    // 确保 scripts 目录存在
    try {
        if (!std::filesystem::exists(scripts_dir)) {
            std::filesystem::create_directories(scripts_dir);
            INFO("[StrategyHandler] Created scripts directory: {}", scripts_dir.string());
        }
    } catch (const std::filesystem::filesystem_error& e) {
        WARN("[StrategyHandler] Failed to create scripts directory: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Failed to create scripts directory";
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
        return;
    }

    // Windows 下需要将 UTF-8 策略名转换为 UTF-16 才能正确处理中文路径
#ifdef _WIN32
    std::wstring wname = utf8_to_utf16(name);
    if (wname.empty()) {
        WARN("[StrategyHandler] Failed to convert strategy name to UTF-16: {}", name);
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Failed to convert strategy name encoding";
        res.set_content(err.dump(), "application/json");
        return;
    }

    std::filesystem::path full_path = scripts_dir / std::filesystem::path(wname);
    std::wstring full_path_wstr = full_path.wstring();

    INFO("[StrategyHandler] Deploying strategy '{}' (UTF-16 path)", name);

    // Windows 下使用 _wfopen 打开文件（支持中文路径）
    FILE* fp = _wfopen(full_path_wstr.c_str(), L"wb, ccs=UTF-8");
    if (!fp) {
        WARN("[StrategyHandler] Failed to open script file for writing: {}", name);
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Failed to save strategy file";
        err["path"] = name;
        res.set_content(err.dump(), "application/json");
        return;
    }
    fwrite(scripts.c_str(), 1, scripts.size(), fp);
    fclose(fp);
#else
    std::filesystem::path full_path = scripts_dir / name;
    String full_path_str = full_path.string();
    INFO("[StrategyHandler] Deploying strategy '{}' to {}", name, full_path_str);

    std::ofstream ofs(full_path_str, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        WARN("[StrategyHandler] Failed to open script file for writing: {}", full_path_str);
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Failed to save strategy file";
        err["path"] = full_path_str;
        res.set_content(err.dump(), "application/json");
        return;
    }
    ofs << scripts;
    ofs.close();
#endif

    INFO("[StrategyHandler] Strategy '{}' saved successfully", name);

    // ============ 处理 multipart 模型文件 ============
    {
        String modelErrMsg;
        if (!ValidateXGBoostModelPaths(scriptJson, name, modelErrMsg)) {
            WARN("[StrategyHandler] {}", modelErrMsg);
            res.status = 400;
            nlohmann::json err;
            err["message"] = modelErrMsg;
            res.set_content(err.dump(), "application/json");
            return;
        }
        if (reqPtr) {
            String dbPath = _server->GetConfig().GetDatabasePath();
            if (!WriteMultipartModelFiles(*reqPtr, name, dbPath, modelErrMsg)) {
                WARN("[StrategyHandler] {}", modelErrMsg);
                res.status = 500;
                nlohmann::json err;
                err["message"] = modelErrMsg;
                res.set_content(err.dump(), "application/json");
                return;
            }
        }
    }

    // 运行
    StrategyInitResult initResult;
    try {
        initResult = strategySys->InitStrategy(name, scriptJson);
    } catch (const std::exception& e) {
        // 兜底：防御性 catch，应保证不触发
        WARN("[StrategyHandler] Unexpected exception in InitStrategy for '{}': {}", name, e.what());
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Strategy init failed (unexpected exception)";
        err["error"] = e.what();
        err["name"] = name;
        res.set_content(err.dump(), "application/json");
        return;
    }

    if (!initResult._success) {
        WARN("[StrategyHandler] InitStrategy failed for '{}': {}", name, initResult._errorMessage);
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Strategy init failed";
        err["error"] = initResult._errorMessage;
        err["name"] = name;
        nlohmann::json failedNode;
        failedNode["id"] = initResult._failedNodeId;
        failedNode["label"] = initResult._failedNodeLabel;
        failedNode["type"] = initResult._failedNodeType;
        err["failed_node"] = failedNode;
        res.set_content(err.dump(), "application/json");
        return;
    }

    try {
        strategySys->Run(name);
    } catch (const std::exception& e) {
        WARN("[StrategyHandler] Run failed for '{}': {}", name, e.what());
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Strategy run failed";
        err["error"] = e.what();
        err["name"] = name;
        res.set_content(err.dump(), "application/json");
        return;
    }

    bool running = false;
    if (auto flow = strategySys->GetFlowSubsystem()) {
        running = flow->IsRunning(name);
    }
    // 回测模式下策略跑完后线程已退出，IsRunning 会返回 false，需按全局运行模式判断
    if (_server->GetRunningMode() == RuningType::Backtest) {
        running = true;
    }
    res.status = 200;
    nlohmann::json result;
    result["message"] = "success";
    result["name"] = name;
    result["running"] = running;
    res.set_content(result.dump(), "application/json");
}

void StrategyHandler::load(const nlohmann::json& param, httplib::Response& res) {
    if (!param.is_object()) {
        res.status = 400;
        nlohmann::json err;
        err["message"] = "invalid param: expected JSON object";
        res.set_content(err.dump(), "application/json");
        return;
    }
    String name = param.value("name", "");
    nlohmann::json scriptJson = param.value("script", nlohmann::json());
    auto strategySys = _server->GetStrategySystem();
    StrategyInitResult initResult;
    try {
        initResult = strategySys->InitStrategy(name, scriptJson);
    } catch (const std::exception& e) {
        WARN("[StrategyHandler] Unexpected exception in InitStrategy (load) for '{}': {}", name, e.what());
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Strategy init failed (unexpected exception)";
        err["error"] = e.what();
        err["name"] = name;
        res.set_content(err.dump(), "application/json");
        return;
    }

    if (!initResult._success) {
        WARN("[StrategyHandler] Load InitStrategy failed for '{}': {}", name, initResult._errorMessage);
        res.status = 500;
        nlohmann::json err;
        err["message"] = "Strategy init failed";
        err["error"] = initResult._errorMessage;
        err["name"] = name;
        nlohmann::json failedNode;
        failedNode["id"] = initResult._failedNodeId;
        failedNode["label"] = initResult._failedNodeLabel;
        failedNode["type"] = initResult._failedNodeType;
        err["failed_node"] = failedNode;
        res.set_content(err.dump(), "application/json");
        return;
    }

    res.status = 200;
    nlohmann::json result;
    result["message"] = "loaded";
    result["name"] = name;
    res.set_content(result.dump(), "application/json");
}

void StrategyHandler::run(const String& name, httplib::Response& res) {
    auto strategySys = _server->GetStrategySystem();
    strategySys->Run(name);

    bool running = false;
    if (auto flow = strategySys->GetFlowSubsystem()) {
        running = flow->IsRunning(name);
    }
    if (_server->GetRunningMode() == RuningType::Backtest) {
        running = true;
    }
    nlohmann::json result;
    result["message"] = "success";
    result["name"] = name;
    result["running"] = running;
    res.set_content(result.dump(), "application/json");
}

void StrategyHandler::stop(const String& name, httplib::Response& res) {
    auto strategySys = _server->GetStrategySystem();
    strategySys->Stop(name);

    bool running = false;
    if (auto flow = strategySys->GetFlowSubsystem()) {
        running = flow->IsRunning(name);
    }
    if (_server->GetRunningMode() == RuningType::Backtest) {
        running = true;
    }
    nlohmann::json result;
    result["message"] = "success";
    result["name"] = name;
    result["running"] = running;
    res.set_content(result.dump(), "application/json");
}

void StrategyHandler::validate(const nlohmann::json& param, httplib::Response& res) {
    try {
        // 获取策略配置
        auto& config = param["config"];

        // 调用 Server 的验证方法
        auto [success, errorMessage] = _server->ValidateStrategyConfig(config);

        nlohmann::json result;
        if (success) {
            result["success"] = true;
            result["message"] = "Validation passed";
            res.status = 200;
        } else {
            result["success"] = false;
            result["error"] = errorMessage;
            result["message"] = "Validation failed";
            res.status = 400;
        }
        res.set_content(result.dump(), "application/json");
    } catch (const std::exception& e) {
        nlohmann::json result;
        result["success"] = false;
        result["error"] = e.what();
        result["message"] = "Validation error";
        res.status = 500;
        res.set_content(result.dump(), "application/json");
    }
}

void StrategyHandler::train(const nlohmann::json& params, httplib::Response& res) {
    String strategyName = params.at("name");
    auto& args = params.at("params");

    auto strategy_system = _server->GetStrategySystem();
    if (!strategy_system->CreateStrategy(strategyName, args)) {
      res.status = 400;
      res.set_content("{message: 'create strategy fail'}", "application/json");
      return;
    }
    String str = params["symbol"];
    Vector<String> str_codes;
    split(str, str_codes, ",");
    Vector<symbol_t> symbols;
    for (auto& code: str_codes) {
      auto symbol = to_symbol(code);
      symbols.push_back(symbol);
    }
    strategy_system->Train(strategyName, symbols, DataFrequencyType::Day);
    res.status = 200;
}

StrategyNodesHandler::StrategyNodesHandler(Server* server):HttpHandler(server) {

}

void StrategyNodesHandler::get(const httplib::Request& req, httplib::Response& res) {
    namespace hana = boost::hana;
    auto types = hana::tuple_t<DebugNode, QuoteInputNode, SignalNode, FunctionNode>;

    nlohmann::json nodeParams;
    hana::for_each(types, [&nodeParams](auto t) {
        using T = typename decltype(t)::type;
        auto cls = T::className();
        auto params = T::getParams();
        nodeParams[cls] = params;
    });
    res.status = 200;
    res.set_content(nodeParams.dump(), "application/json");
}

StrategyNodeHandler::StrategyNodeHandler(Server* server):HttpHandler(server) {

}

void StrategyNodeHandler::get(const httplib::Request& req, httplib::Response& res) {
    String strategy = req.get_param_value("strategy");
    String label = req.get_param_value("label");
    auto& cfg = _server->GetConfig();
    auto path = cfg.GetDatabasePath();
    path += "/data/debug/" + strategy + "/" + label;
    if (!std::filesystem::exists(path)) {
        res.status = 404;
        res.set_content("{message: 'data not exist'}", "application/json");
        return;
    }
    std::ifstream* file = new std::ifstream(path, std::ios::binary);
    res.set_chunked_content_provider(
        "application/octet-stream", // Content-Type
        [file, path](size_t offset, httplib::DataSink &sink) {
            char buffer[MAX_STREAM_SIZE] = {0};
            file->read(buffer, sizeof(buffer));
            std::streamsize bytes_read = file->gcount();
            if (bytes_read > 0) {
                sink.write(buffer, bytes_read);
                return true;
            }
            else {
                sink.done();
                file->close();
                delete file;
                // 不删除保证可以重复下载
                // std::filesystem::remove_all(path);
                return false;
            }
        }
    );
}

void StrategyNodeHandler::put(const httplib::Request& req, httplib::Response& res) {
    // 检查是否是multipart/form-data
    if (!req.has_file("file")) {
        res.status = 400;
        res.set_content("No file uploaded", "text/plain");
        return;
    }
    
    // 获取上传的文件
    const auto& file = req.get_file_value("file");
    
    // 打印文件信息
    std::cout << "File name: " << file.filename << std::endl;
    std::cout << "Content type: " << file.content_type << std::endl;
    std::cout << "File size: " << file.content.size() << " bytes" << std::endl;
    
    try {
        auto& cfg = _server->GetConfig();
        // 保存文件到指定目录
        auto dir = cfg.GetDatabasePath() + "/model";
        auto save_path = dir + "/" + file.filename;

        // 确保上传目录存在
        std::filesystem::create_directories(dir);
        
        // 写入文件
        std::ofstream ofs(save_path, std::ios::binary);
        if (!ofs) {
            res.status = 500;
            res.set_content("Failed to save file", "text/plain");
            return;
        }
        
        ofs.write(file.content.data(), file.content.size());
        ofs.close();
        
        // 返回成功响应
        res.set_content("{'message':'" + file.filename + " upload success'}", "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content("{message: '" + std::string(e.what()) + "'}", "text/plain");
    }
}