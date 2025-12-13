#include "Handler/StrategyHandler.h"
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <yas/serialize.hpp>
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"
#include "Bridge/exchange.h"
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
    auto sys = _server->GetStrategySystem();
    nlohmann::json info = sys->GetStrategyNames();
    res.status = 200;
    res.set_content(info.dump(), "application/json");
}

void StrategyHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    int mode = params["mode"];
    if (mode == 1) { // 部署实盘
        run(params, res);
    }
    else if (mode == 2) {
        // 部署模拟盘到策略服务
        virtual_deploy(params, res);
        // 连接策略服务
    }
    else if (mode == 0) {
        train(params, res);
    } else {
        WARN("not support mode {}", mode);
    }
}

void StrategyHandler::run(const nlohmann::json& params, httplib::Response& res) {
    String strategyName = params.at("name");
    auto strategy_system = _server->GetStrategySystem();
    if (!strategy_system->HasStrategy(strategyName)) {
        res.status = 404;
        res.set_content("{message: 'strategy not found.'}", "application/json");
        return;
    }

    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");
    // request run strategy and wait reply
    res.set_chunked_content_provider("text/event-stream", [this, strategyName] (size_t /*offset*/, httplib::DataSink &sink) {
        String begin("data: Connection established\n\n");
        sink.write(begin.c_str(), begin.size());
        nng_socket recv;
        // receive result once per 5000ms 
        Subscribe("inproc://Signal." + strategyName, recv);
        // this thread exit when program exit or client connection is closed 
        constexpr std::size_t flags = yas::mem | yas::binary;
        try {
            while (!_handle->IsExit())
            {
                char* buff = NULL;
                size_t sz = 0;
                int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
                if (rv != 0) {
                    nng_free(buff, sz);
                    continue;
                }
                if (sz == 0 || sz == std::numeric_limits<size_t>::max()) {
                    nng_free(buff, sz);
                    continue;
                }
                yas::shared_buffer buf;
                buf.assign(buff, sz);
                Vector<Signal> signals;
                yas::load<flags>(buf, signals);
                String info;
                for (auto& sig: signals) {
                    info += get_symbol(sig._symbol) + "|" + std::to_string(sig._hold) + ",";
                }
                if (!info.empty()) info.pop_back();

                String reply = "data: " + info + "\n\n";
                sink.write(reply.c_str(), reply.size());
                nng_free(buff, sz);
            }
        } catch(...) {}
        nng_close(recv);
        sink.done();
        return false;
    });
}

void StrategyHandler::connect_strategy_service(const String& strategyName, httplib::DataSink& sink) {
    String begin("data: Connection established\n\n");
    sink.write(begin.c_str(), begin.size());
    nng_socket recv;
    // receive result once per 5000ms 
    Subscribe("inproc://Signal." + strategyName, recv);
    // this thread exit when program exit or client connection is closed 
    constexpr std::size_t flags = yas::mem | yas::binary;
    try {
        while (!_handle->IsExit())
        {
            char* buff = NULL;
            size_t sz = 0;
            int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
            if (rv != 0) {
                nng_free(buff, sz);
                continue;
            }
            if (sz == 0 || sz == std::numeric_limits<size_t>::max()) {
                nng_free(buff, sz);
                continue;
            }
            yas::shared_buffer buf;
            buf.assign(buff, sz);
            Vector<Signal> signals;
            yas::load<flags>(buf, signals);
            String info;
            for (auto& sig: signals) {
                info += get_symbol(sig._symbol) + "|" + std::to_string(sig._hold) + ",";
            }
            if (!info.empty()) info.pop_back();

            String reply = "data: " + info + "\n\n";
            sink.write(reply.c_str(), reply.size());
            nng_free(buff, sz);
        }
    } catch(...) {}
    nng_close(recv);
    sink.done();
}

void StrategyHandler::virtual_deploy(const nlohmann::json& param, httplib::Response& res) {

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
    auto types = hana::tuple_t<DebugNode, QuoteInputNode, SignalNode, LSTMNode, FunctionNode>;

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