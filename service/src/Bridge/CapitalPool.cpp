#include "Bridge/CapitalPool.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include <fstream>
#include <filesystem>
#include "json.hpp"

namespace fs = std::filesystem;

CapitalPool::CapitalPool(double initialCapital)
    : _initialCapital(initialCapital)
{
}

void CapitalPool::init(double initialCapital) {
    std::lock_guard<std::mutex> lock(_mutex);
    _initialCapital = initialCapital;
}

bool CapitalPool::allocate(const String& strategy, double requested) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    double totalAvailable = _initialCapital;
    for (const auto& [name, info] : _strategies) {
        if (info.active && name != strategy) {
            totalAvailable -= info.allocated;
        }
    }
    
    if (requested <= 0) {
        // 自动分配：剩余资金平均分配
        int activeCount = 1;
        for (const auto& [name, info] : _strategies) {
            if (info.active) activeCount++;
        }
        requested = totalAvailable / activeCount;
    }
    
    if (requested > totalAvailable) {
        WARN("[CapitalPool] Insufficient funds for {}: requested={}, available={}",
             strategy, requested, totalAvailable);
        return false;
    }
    
    StrategyCapitalInfo info;
    info.allocated = requested;
    info.available = requested;
    info.active = true;
    _strategies[strategy] = info;
    
    return true;
}

double CapitalPool::reclaim(const String& strategy) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it == _strategies.end()) {
        return 0.0;
    }

    // 只置为非活跃，不移除 entry、不清零 available：
    //   hasStrategy() 之后仍返回 true，故 DeleteStrategy→InitStrategy 路径不会再 allocate，
    //   而是直接沿用这里的 available 作为本次回测的工作资金（原有行为，勿改）。
    //   重复调用安全：active 已为 false 时再置一次无副作用。
    double reclaimed = it->second.available;
    it->second.active = false;

    return reclaimed;
}

void CapitalPool::reclaimAll() {
    std::lock_guard<std::mutex> lock(_mutex);
    _strategies.clear();
}

void CapitalPool::deactivate(const String& strategy) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        it->second.active = false;
    }
}

void CapitalPool::updateAvailable(const String& strategy, double delta) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        it->second.available += delta;
        if (it->second.available < 0) {
            WARN("[CapitalPool] Strategy {} available funds negative: {}",
                 strategy, it->second.available);
        }
    }
}

void CapitalPool::setAvailable(const String& strategy, double available) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        it->second.available = available;
    } else {
        StrategyCapitalInfo info;
        info.allocated = std::max(available, 0.0);
        info.available = available;
        info.active = true;
        _strategies[strategy] = info;
    }
}

StrategyCapitalInfo CapitalPool::get(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        return it->second;
    }
    return StrategyCapitalInfo{};
}

double CapitalPool::getAvailable(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        return it->second.available;
    }
    return 0.0;
}

bool CapitalPool::hasStrategy(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _strategies.count(strategy) > 0;
}

double CapitalPool::getUsed(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        return it->second.allocated - it->second.available;
    }
    return 0.0;
}

void CapitalPool::applyCreditFill(const String& strategy, const String& symbol,
                                  CreditAction action, int64_t quantity,
                                  double price, double marginRatio) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it == _strategies.end()) {
        WARN("[CapitalPool] applyCreditFill: strategy '{}' not registered, skip", strategy);
        return;
    }
    if (action == CreditAction::None || quantity <= 0 || price <= 0.0) {
        return;
    }

    auto& credit = it->second.credit;
    switch (action) {
        case CreditAction::MarginBuy: {
            // 融资买入：成交额按保证金比例拆成自有资金与借入现金。
            // 借入现金计入融资余额，逐日按融资利率计提利息。
            double gross = static_cast<double>(quantity) * price;
            double borrowed = gross * (1.0 - std::clamp(marginRatio, 0.0, 1.0));
            credit.borrowedCash += borrowed;
            credit.financedAmount[symbol] += borrowed;
            break;
        }
        case CreditAction::ShortSell: {
            // 融券卖出：在借股数增加，开空均价按加权更新。
            auto& shares = credit.borrowedShares[symbol];
            auto& avg = credit.shortAvgPrice[symbol];
            double oldValue = static_cast<double>(shares) * avg;
            shares += quantity;
            avg = (oldValue + static_cast<double>(quantity) * price) / static_cast<double>(shares);
            break;
        }
        case CreditAction::ShortCover: {
            // 买入平空：归还借券，在借股数减少。
            auto itShares = credit.borrowedShares.find(symbol);
            if (itShares == credit.borrowedShares.end()) {
                break;
            }
            itShares->second -= quantity;
            if (itShares->second <= 0) {
                credit.borrowedShares.erase(itShares);
                credit.shortAvgPrice.erase(symbol);
            }
            break;
        }
        case CreditAction::None:
            break;
    }
}

StrategyCreditState CapitalPool::getCreditState(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        return it->second.credit;
    }
    return StrategyCreditState{};
}

double CapitalPool::getTotalAllocated() const {
    std::lock_guard<std::mutex> lock(_mutex);
    double total = 0;
    for (const auto& [name, info] : _strategies) {
        if (info.active) {
            total += info.allocated;
        }
    }
    return total;
}

double CapitalPool::getTotalAvailable() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _initialCapital - getTotalAllocated();
}

int CapitalPool::getActiveStrategyCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    int count = 0;
    for (const auto& [name, info] : _strategies) {
        if (info.active) count++;
    }
    return count;
}

bool CapitalPool::persist(const String& filePath) const {
    std::lock_guard<std::mutex> lock(_mutex);
    
    try {
        nlohmann::json json;
        json["initialCapital"] = _initialCapital;
        
        nlohmann::json strategiesJson;
        for (const auto& [name, info] : _strategies) {
            nlohmann::json creditJson;
            creditJson["borrowedCash"] = info.credit.borrowedCash;
            nlohmann::json sharesJson = nlohmann::json::object();
            for (const auto& [sym, qty] : info.credit.borrowedShares) {
                sharesJson[sym] = qty;
            }
            creditJson["borrowedShares"] = std::move(sharesJson);
            nlohmann::json avgJson = nlohmann::json::object();
            for (const auto& [sym, price] : info.credit.shortAvgPrice) {
                avgJson[sym] = price;
            }
            creditJson["shortAvgPrice"] = std::move(avgJson);
            nlohmann::json financedJson = nlohmann::json::object();
            for (const auto& [sym, amount] : info.credit.financedAmount) {
                financedJson[sym] = amount;
            }
            creditJson["financedAmount"] = std::move(financedJson);

            strategiesJson[name] = {
                {"allocated", info.allocated},
                {"available", info.available},
                {"active", info.active},
                {"credit", std::move(creditJson)}
            };
        }
        json["strategies"] = strategiesJson;
        
        fs::path path(filePath);
        if (path.has_parent_path()) {
            fs::create_directories(path.parent_path());
        }
        
        std::ofstream ofs(filePath);
        if (!ofs.is_open()) {
            FATAL("[CapitalPool] Failed to open file for writing: {}", filePath);
            return false;
        }
        ofs << json.dump(2);
        ofs.close();
        return true;
    } catch (const std::exception& e) {
        FATAL("[CapitalPool] Persist failed: {}", e.what());
        return false;
    }
}

bool CapitalPool::load(const String& filePath) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    try {
        if (!fs::exists(filePath)) {
            return false;
        }
        
        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            return false;
        }
        
        nlohmann::json json;
        ifs >> json;
        ifs.close();
        
        if (json.contains("initialCapital")) {
            _initialCapital = json["initialCapital"].get<double>();
        }
        
        if (json.contains("strategies")) {
            for (const auto& [name, val] : json["strategies"].items()) {
                StrategyCapitalInfo info;
                info.allocated = val.value("allocated", 0.0);
                info.available = val.value("available", 0.0);
                info.active = val.value("active", false);
                // 信用敞口：旧版本 capital_pool.json 没有 credit 段，全部走默认（无两融）
                if (val.contains("credit") && val["credit"].is_object()) {
                    const auto& credit = val["credit"];
                    info.credit.borrowedCash = credit.value("borrowedCash", 0.0);
                    auto readMap = [&credit](const char* key, auto& out) {
                        if (!credit.contains(key) || !credit[key].is_object()) return;
                        for (const auto& [sym, v] : credit[key].items()) {
                            if (v.is_number()) out[sym] = v.get<double>();
                        }
                    };
                    if (credit.contains("borrowedShares") && credit["borrowedShares"].is_object()) {
                        for (const auto& [sym, v] : credit["borrowedShares"].items()) {
                            if (v.is_number_integer() || v.is_number()) {
                                info.credit.borrowedShares[sym] = v.get<int64_t>();
                            }
                        }
                    }
                    readMap("shortAvgPrice", info.credit.shortAvgPrice);
                    readMap("financedAmount", info.credit.financedAmount);
                }
                _strategies[name] = info;
            }
        }
        
        INFO("[CapitalPool] Loaded from {}, initialCapital={:.0f}, strategies={}",
             filePath, _initialCapital, _strategies.size());
        return true;
    } catch (const std::exception& e) {
        FATAL("[CapitalPool] Load failed: {}", e.what());
        return false;
    }
}
