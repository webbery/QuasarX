#include "PortfolioSubsystem.h"

PortfolioSubSystem::PortfolioSubSystem(Server* server)
  :_server(server)
{
  // Initialize portfolio
}

PortfolioInfo& PortfolioSubSystem::GetPortfolio(int id)
{
  return _portfolios[id];
}

bool PortfolioSubSystem::HasPortfolio(int id) {
  return _portfolios.count(id);
}

void PortfolioSubSystem::SetDefault(int id) {
  _default = id;
}

int PortfolioSubSystem::CreatePortfolio() {
  if (_portfolios.empty()) {
    _default = 1;
    _portfolios[_default];
    return 1;
  }
  auto ritr = _portfolios.rbegin();
  if (ritr == _portfolios.rend()) {
    
  }
  int maxID = ritr->first + 1;
  _portfolios[maxID];
  return maxID;
}

void PortfolioSubSystem::AddPortfolio(const nlohmann::json& p) {
  PortfolioInfo& pi = _portfolios[p["id"]];
  for (const String& symbol: p["pool"]) {
    pi._pools.insert(symbol);
  }
}

List<int> PortfolioSubSystem::GetAllPortfolio() {
  List<int> ids;
  for (auto& item: _portfolios) {
    ids.push_back(item.first);
  }
  return ids;
}

void PortfolioSubSystem::ErasePortfolio(int id) {
  _portfolios.erase(id);
}

void PortfolioSubSystem::Update(const String& symbol, const DealInfo& deals) {

}

void PortfolioSubSystem::Start() {
  
}
void PortfolioSubSystem::Stop() {

}