#pragma once
#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>
#include <thread>
#include "HttpHandler.h"
#include "json.hpp"

//class StrategyPlugin;
class Server;
class StrategyHandler: public HttpHandler {
public:
  StrategyHandler(Server* server);
  ~StrategyHandler();

  virtual void doWork(const std::vector<std::string>& params);

  virtual void get(const httplib::Request& req, httplib::Response& res);
  virtual void post(const httplib::Request& req, httplib::Response& res);

private:
  void run(const nlohmann::json& param, httplib::Response& res);

  void train(const nlohmann::json& param, httplib::Response& res);

  void virtual_deploy(const nlohmann::json& param, httplib::Response& res);

  void connect_strategy_service(const String& name, httplib::DataSink& sink);
private:
    //std::map<std::string, StrategyPlugin*> _strategies;
    Server* _handle;

    bool _close;
    nng_socket sock;

    std::thread* _main;
};