#pragma once
#include "std_header.h"
#include "httplib.h"

class Server;
class HttpHandler {
public:
  HttpHandler(Server* server) :_server(server) {}

  virtual ~HttpHandler() {}

  virtual void get(const httplib::Request& req, httplib::Response& res) { }
  virtual void post(const httplib::Request& req, httplib::Response& res) { }
  virtual void del(const httplib::Request& req, httplib::Response& res) { }
  virtual void put(const httplib::Request& req, httplib::Response& res) { }

protected:
  Server* _server;
};
