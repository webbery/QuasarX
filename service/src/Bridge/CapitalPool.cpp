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
    
    double reclaimed = it->second.available;
    it->second.active = false;
    
    return reclaimed;
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

double CapitalPool::getUsed(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _strategies.find(strategy);
    if (it != _strategies.end()) {
        return it->second.allocated - it->second.available;
    }
    return 0.0;
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
            strategiesJson[name] = {
                {"allocated", info.allocated},
                {"available", info.available},
                {"active", info.active}
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
