#include "PortfolioSubsystem.h"

PortfolioSubSystem::PortfolioSubSystem(Server* server)
  :_server(server)
{
  // Initialize portfolio
}

PortfolioInfo& PortfolioSubSystem::GetPortfolio(const String& id)
{
  return _portfolios[id];
}

bool PortfolioSubSystem::HasPortfolio(const String& id) {
  return _portfolios.count(id);
}

void PortfolioSubSystem::SetDefault(const String& id) {
  _default = id;
}

// int PortfolioSubSystem::CreatePortfolio() {
//   if (_portfolios.empty()) {
//     _default = 1;
//     _portfolios[_default];
//     return 1;
//   }
//   auto ritr = _portfolios.rbegin();
//   if (ritr == _portfolios.rend()) {
    
//   }
//   int maxID = ritr->first + 1;
//   _portfolios[maxID];
//   return maxID;
// }

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

void PortfolioSubSystem::Start() {
  
}
void PortfolioSubSystem::Stop() {

}