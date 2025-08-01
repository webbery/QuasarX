#pragma once
#include "DataHandler.h"
#include "Util/system.h"
#include "ql/math/matrix.hpp"
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

  int CreatePortfolio();

  List<int> GetAllPortfolio();

  bool HasPortfolio(int id);

  PortfolioInfo& GetPortfolio(int id);

  void ErasePortfolio(int id);

  void SetDefault(int id);

  int Default() { return _default; }

  double Position() { return _position; }

  void Update(symbol_t symbol, const DealInfo& deals);

  // A thread that decide how much contract to sell/buy after recieve buy/sell operator
  void Start();
  void Stop();

  friend class BrokerSubSystem;
private:
  void AddPortfolio(const nlohmann::json& p);

private:
  Server* _server;
  Map<int, PortfolioInfo> _portfolios;
  double _position;
  int _default = 0;
};