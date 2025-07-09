#pragma once
#include "HttpHandler.h"

class Server;
class AccountHandler : public HttpHandler {
public:
  AccountHandler(Server* exchanges);

    void doWork(const std::vector<std::string>& params);

private:
};