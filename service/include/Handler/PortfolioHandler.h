#pragma once
#include "HttpHandler.h"

class CommissionHandler : public HttpHandler {
public:
};

class PortfolioHandler : public HttpHandler {
public:
  PortfolioHandler(Server* handle);

  virtual void put(const httplib::Request& req, httplib::Response& res);

};