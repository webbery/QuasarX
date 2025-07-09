#include "Handler/StrategyHandler.h"
#include <cstring>
#include <functional>
#include <limits>
#include <yas/serialize.hpp>
#include "DataHandler.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "server.h"
#include "Bridge/exchange.h"
#include <ql/methods/montecarlo/brownianbridge.hpp>
#include "Risk/StopLoss.h"
#include "StrategySubSystem.h"

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

void StrategyHandler::doWork(const std::vector<std::string>& params) {
    if (params.empty())
        return;

    if (params[0] == "run") {
      if (sock.id != 0) {
        printf("an strategy is running.\n");
        return;
      }

      std::string strategy_name = "smc";
      if (params.size() >= 2) {
        strategy_name = params[1];
      }

      /*StrategyPlugin* strategy = nullptr;
      auto itr = _strategies.find(strategy_name);
      if (itr == _strategies.end()) {
        strategy = _handle->GetOrCreateStrategy(strategy_name);
        if (strategy == nullptr)
          return;

        _strategies[strategy_name] = strategy;
      }
      else {
        strategy = itr->second;
      }

      _main = new std::thread(&StrategyHandler::run, this, strategy);*/
    }
    else if (params[0] == "stop") {
      _close = true;
      _main->join();
      printf("strategy exit.\n");
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
