#pragma once
#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>
#include <thread>
#include <chrono>
#include <unordered_map>
#include "HttpHandler.h"
#include "json.hpp"

//class StrategyPlugin;
class Server;
class StrategyHandler: public HttpHandler {
public:
  StrategyHandler(Server* server);
  ~StrategyHandler();

  virtual void get(const httplib::Request& req, httplib::Response& res);
  virtual void post(const httplib::Request& req, httplib::Response& res);
  virtual void del(const httplib::Request& req, httplib::Response& res);

private:
  void run(const String& name, httplib::Response& res);
  void stop(const String& name, httplib::Response& res);

  void validate(const nlohmann::json& param, httplib::Response& res);

  void train(const nlohmann::json& param, httplib::Response& res);

  void deploy(const nlohmann::json& param, httplib::Response& res, bool force = false);
  void deploy(const nlohmann::json& param, const httplib::Request& req, httplib::Response& res, bool force = false);
  void deployImpl(const nlohmann::json& param, const httplib::Request* reqPtr, httplib::Response& res, bool force = false);

  void load(const nlohmann::json& param, httplib::Response& res);

  // POST /v0/strategy {action:"batch_models", names:[...]} →
  //   对每个 strategyName 返回其所有 XGBoostNode 绑定的模型信息
  //   （production + 最新 experiments + is_latest），10s TTL 缓存
  void batchModels(const nlohmann::json& params, httplib::Response& res);

  void connect_strategy_service(const String& name, httplib::DataSink& sink);

  // 缓存项：value 为已序列化好的 JSON 字符串 + 写入时间，避免每次 deep copy
  struct ModelsCacheEntry {
      std::string _json;
      std::chrono::steady_clock::time_point _cachedAt;
  };
  // batch_models 缓存（key = strategyName），TTL 10s
  std::unordered_map<String, ModelsCacheEntry> _modelsCache;
  std::mutex _modelsCacheMtx;
  static constexpr std::chrono::seconds kModelsCacheTtl{10};
private:
    //std::map<std::string, StrategyPlugin*> _strategies;
    Server* _handle;

    bool _close;
    nng_socket sock;

    std::thread* _main;
};

class StrategyNodesHandler: public HttpHandler {
public:
  StrategyNodesHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);
};

class StrategyNodeHandler: public HttpHandler {
public:
  StrategyNodeHandler(Server* server);

  /**
   * @brief 从节点下载数据
   */
  virtual void get(const httplib::Request& req, httplib::Response& res);
  /**
   * @brief 从节点上传数据
   */
  virtual void put(const httplib::Request& req, httplib::Response& res);
};