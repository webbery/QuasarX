#pragma once
#include "HttpHandler.h"

class Server;
class BrokerHandler : public HttpHandler {
public:
  BrokerHandler(Server* handle);

  void doWork(const std::vector<std::string>& params);
};