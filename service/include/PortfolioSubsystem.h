#pragma once
#include "DataGroup.h"
#include "Util/system.h"
// #include "ql/math/matrix.hpp"
#include "json.hpp"

class BrokerSubSystem;
struct Asset {
  // count
  uint32_t _quantity;
  // unit price
  double _price;
  time_t _date;
};

using hold_t = Map<symbol_t, List<Asset>>;
// map to json
struct PortfolioInfo {
  hold_t _holds;
  double _principal = 0;
  Set<String> _pools;
  double _profit = 0;
};

double GetCost(const List<Asset>&);

class Server;
class PortfolioSubSystem {
public:
  PortfolioSubSystem(Server* server);

  List<String> GetAllPortfolio();

  bool HasPortfolio(const String& id);

  PortfolioInfo& GetPortfolio(const String& id = "");

  void UpdateProfit(const String& id, double profit);

  void ErasePortfolio(const String& id);

  void SetDefault(const String& id);

  String Default() { return _default; }

  double Position() { return _position; }

  void Update(symbol_t symbol, const TradeInfo& deals);

  hold_t& GetHolding(const String& id = "");

  friend class BrokerSubSystem;
private:
  void AddPortfolio(const nlohmann::json& p);

private:
  Server* _server;
  Map<String, PortfolioInfo> _portfolios;
  double _position;
  String _default;
};