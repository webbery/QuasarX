#include "PortfolioSubsystem.h"

double GetCost(const List<Asset>& assets) {
  double total = 0;
  for (auto& item: assets) {
    total += (item._price * item._quantity);
  }
  return total;
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

void PortfolioSubSystem::UpdateProfit(double profit) {
  GetPortfolio()._profit += profit;
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

void PortfolioSubSystem::Update(symbol_t symbol, const TradeInfo& deals) {
  auto& portfolio = _portfolios[_default];
  auto itr = portfolio._holds.find(symbol);
  if (itr == portfolio._holds.end()) {
    
  }
}
