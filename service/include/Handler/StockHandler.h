#pragma once
#include "std_header.h"
#include "HttpHandler.h"
#include "json.hpp"
#include <cstddef>
#include "Util/string_algorithm.h"

class Server;
class StockHandler :public HttpHandler {
public:
  StockHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

  void checkHelp();

private:
  void display(const std::vector<std::string>& cols, const std::list<std::tuple<std::string, double, double>>& data);

private:
};

class StockHistoryHandler: public HttpHandler {
public:
  StockHistoryHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

private:
};

class StockDetailHandler : public HttpHandler {
public:
  StockDetailHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

private:
};

class StockPrivilege : public HttpHandler {
public:
    StockPrivilege(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
};

class StockParams : public HttpHandler {
public:
    StockParams(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void put(const httplib::Request& req, httplib::Response& res);
};