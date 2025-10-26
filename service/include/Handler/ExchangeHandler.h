#pragma once
#include "Bridge/exchange.h"
#include "HttpHandler.h"
#include "server.h"

#define XTP_API     "xtp"   // 
#define CTP_API     "ctp"   // 
#define HX_API      "hx"   // ����

class ExchangeInterface;
class ExchangeHandler : public HttpHandler {
public:
  ExchangeHandler(Server* handle);
  ~ExchangeHandler();

  void doWork(const std::vector<std::string>& params);

  bool Use(const String& name);

  const Map<String, ExchangeInterface*>& GetExchanges() const {
    return _exchanges;
  }

  ExchangeInterface* GetExchangeByType(ExchangeType type);

  const Map<ExchangeType, ExchangeInterface*>& GetExchangesWithType() {
    return _type_excs;
  }

  String GetActiveStock() { return _activeStockName; }
  String GetActiveFuture() { return _activeFutureName; }

  virtual void post(const httplib::Request& req, httplib::Response& res);

  double Buy(const String& strategy,symbol_t symbol, const Order& order, TradeInfo& deals);
  double Sell(const String& strategy,symbol_t symbol, const Order& order, TradeInfo& deals);
  
private:
  ExchangeInfo GetExchangeInfo(const char* name);

  template<typename T>
  bool SwitchExchange(const String& name) {
    if (!_exchanges[name]) {
      auto ptr = new T(_server);
      auto info = GetExchangeInfo(name.c_str());
      if (!ptr->Init(info)) {
        printf("init fail.\n");
        delete ptr;
        return false;
      }
      _exchanges[name] = ptr;
    }
    auto ptr = _exchanges[name];
    if (!ptr->Login()) {
      printf("login fail.\n");
      return false;
    }
    //List<Pair<String, ExchangeName>> symbolMap;
    //ptr->GetSymbolExchanges(symbolMap);
    //_server->InitMarket(symbolMap);

    _server->SetActiveExchange(ptr);
    return true;
  }
private:
  bool _enableSimulation = false;
  String _activeStockName;
  String _activeFutureName;
  Map<String, ExchangeInterface*> _exchanges;
  Map<ExchangeType, ExchangeInterface*> _type_excs;
};