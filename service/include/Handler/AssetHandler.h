#pragma once
#include "HttpHandler.h"

class Server;
class AssetHandler : public HttpHandler {
public:
  AssetHandler(Server* exchanges);

  void doWork(const std::vector<std::string>& params);
private:
  // std::list<ExchangeInterface*> _exchanges;
};