#pragma once
#include <mutex>
#include <thread>
#include "HttpHandler.h"
#include "nng/nng.h"
#include "Util/system.h"

class IStopLoss;
class Server;

enum class StopLossType: char {
  Percentage = 0,
  Fix,
  Move,
  ATR,
  SAR,
  Key,
  Step,
  Time,
};

class StopLossHandler: public HttpHandler {
public:
  StopLossHandler(Server* handle);
  ~StopLossHandler();

  virtual void doWork(const std::vector<std::string>& params);

  virtual void get(const httplib::Request& req, httplib::Response& res);
  virtual void post(const httplib::Request& req, httplib::Response& res);
  virtual void del(const httplib::Request& req, httplib::Response& res);

  IStopLoss* Switch(const String& name);

private:
  void run();

  bool SendEmail(bool buy_sell, const List<symbol_t>& symbols);

  IStopLoss* GetOrGenerate(StopLossType type);

private:
  Map<StopLossType, IStopLoss*> _stloss_map;
  std::thread* _main;
  bool _exit;
  nng_socket _sock;

  String _sender;
  String _pwd;

  std::mutex _mutex;
  Map<symbol_t, String> _mail_map;

  Map<StopLossType, IStopLoss*> _stlosses;
};

class VaRHandler: public HttpHandler {
public:
  VaRHandler(Server* handle): HttpHandler(handle) {}

  virtual void post(const httplib::Request& req, httplib::Response& res);
  virtual void get(const httplib::Request& req, httplib::Response& res);

  double ExpectedShortFall();
private:
  void run();
};

class DrawdownHandler: public HttpHandler {
public:
  DrawdownHandler(Server* handle): HttpHandler(handle) {}
  virtual void get(const httplib::Request& req, httplib::Response& res);

private:
};