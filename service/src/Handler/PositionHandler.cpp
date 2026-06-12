#include "Handler/PositionHandler.h"
#include "ExchangeManager.h"
#include "BrokerSubSystem.h"
#include "server.h"
#include "Util/string_algorithm.h"

PositionHandler::PositionHandler(Server* server):HttpHandler(server) {

}

void PositionHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto exchange = _server->GetExchangeManager()->GetExchangeByType(ExchangeType::EX_HX);
    AccountPosition positions;
    nlohmann::json result;
    if (!exchange->GetPosition(positions)) {
        res.status = 500;
        return ;
    }
    nlohmann::json positionsArray;
    for (auto& item: positions._positions) {
        nlohmann::json pos;
        pos["id"] = get_symbol(item._symbol);
        pos["name"] = to_utf8(item._name);
        pos["price"] = item._price;
        pos["curPrice"] = item._curPrice;
        pos["quantity"] = item._holds;
        pos["valid_quantity"] = item._validHolds;
        positionsArray.emplace_back(std::move(pos));
    }
    result["positions"] = positionsArray;
    
    // 添加资金池信息
    auto* broker = _server->GetBrokerSubSystem();
    if (broker) {
        auto* pool = broker->GetCapitalPool();
        if (pool) {
            nlohmann::json capital;
            capital["initialCapital"] = pool->getInitialCapital();
            capital["totalAllocated"] = pool->getTotalAllocated();
            capital["totalAvailable"] = pool->getTotalAvailable();
            capital["activeStrategies"] = pool->getActiveStrategyCount();
            
            // 各策略资金详情
            nlohmann::json strategies;
            // 注意：CapitalPool 需要暴露遍历接口，这里简化处理
            // 实际需要通过 pool->get(strategyName) 逐个获取
            
            result["capital"] = capital;
        }
    }
    
    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
