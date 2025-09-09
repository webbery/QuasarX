#pragma once
#include "DataGroup.h"
#include "Util/system.h"
// #include "ql/math/matrix.hpp"
#include "json.hpp"

class BrokerSubSystem;
struct Asset {
  // count
  uint32_t _hold;
  // unit price
  float _price;
  String _symbol;
};

// map to json
struct PortfolioInfo {
  Map<symbol_t, Asset> _holds;
  double _principal;
  Set<String> _pools;
  
};

class Server;
class PortfolioSubSystem {
public:
  PortfolioSubSystem(Server* server);

  List<String> GetAllPortfolio();

  bool HasPortfolio(const String& id);

  PortfolioInfo& GetPortfolio(const String& id);

  void ErasePortfolio(const String& id);

  void SetDefault(const String& id);

  String Default() { return _default; }

  double Position() { return _position; }

  void Update(symbol_t symbol, const TradeInfo& deals);

  // A thread that decide how much contract to sell/buy after recieve buy/sell operator
  void Start();
  void Stop();

  friend class BrokerSubSystem;
private:
  void AddPortfolio(const nlohmann::json& p);

private:
  Server* _server;
  Map<String, PortfolioInfo> _portfolios;
  double _position;
  String _default;
};