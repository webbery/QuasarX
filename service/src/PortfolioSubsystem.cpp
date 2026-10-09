#include "PortfolioSubsystem.h"

double GetCost(const List<Asset>& assets) {
  double total = 0;
  for (auto& item: assets) {
    total += (item._price * item._quantity);
  }
  return total;
}

double ApplyFillToHolding(hold_t& holds, symbol_t symbol, int64_t quantity,
                          double price, time_t when, bool isOpen) {
  if (quantity <= 0) return 0.0;

  auto& history = holds[symbol];
  if (isOpen) {
    history.push_back({static_cast<uint32_t>(quantity), price, when});
    return 0.0;
  }

  // 先进先出扣减，同时累计被消耗掉的成本
  double consumed = 0.0;
  int64_t remaining = quantity;
  while (remaining > 0 && !history.empty()) {
    auto& front = history.front();
    if (front._quantity >= static_cast<uint32_t>(remaining)) {
      consumed += front._price * static_cast<double>(remaining);
      front._quantity -= static_cast<uint32_t>(remaining);
      remaining = 0;
    } else {
      consumed += front._price * static_cast<double>(front._quantity);
      remaining -= front._quantity;
      history.pop_front();
    }
  }
  if (history.empty()) {
    holds.erase(symbol);
  }
  return consumed;
}

PortfolioSubSystem::PortfolioSubSystem(Server* server)
  :_server(server)
{
  // Initialize portfolio
}

PortfolioInfo& PortfolioSubSystem::GetPortfolio(const String& id)
{
  if (id.empty()) {
    return _portfolios.begin()->second;
  }
  return _portfolios[id];
}

void PortfolioSubSystem::UpdateProfit(const String& id, double profit) {
  GetPortfolio(id)._profit += profit;
}

bool PortfolioSubSystem::HasPortfolio(const String& id) {
  return _portfolios.count(id);
}

void PortfolioSubSystem::SetDefault(const String& id) {
  _default = id;
}

hold_t& PortfolioSubSystem::GetHolding(const String& id) {
  if (id.empty()) {
    return _portfolios.begin()->second._holds;
  }
  return _portfolios[id]._holds;
}

void PortfolioSubSystem::AddPortfolio(const nlohmann::json& p) {
  JSON_REQUIRE_KEY(p, "id");
  JSON_REQUIRE_KEY(p, "pool");
  PortfolioInfo& pi = _portfolios[p["id"]];
  for (const String& symbol: p["pool"]) {
    pi._pools.insert(symbol);
  }
}

List<String> PortfolioSubSystem::GetAllPortfolio() {
  List<String> ids;
  for (auto& item: _portfolios) {
    ids.push_back(item.first);
  }
  return ids;
}

void PortfolioSubSystem::ErasePortfolio(const String& id) {
  _portfolios.erase(id);
}

// void PortfolioSubSystem::Update(symbol_t symbol, const TradeInfo& deals) {
//   auto& portfolio = _portfolios[_default];
//   auto itr = portfolio._holds.find(symbol);
//   if (itr == portfolio._holds.end()) {
    
//   }
// }
