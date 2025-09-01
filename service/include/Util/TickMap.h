#pragma once
#include "std_header.h"
#include <cstdint>
#include <limits>
#include <mutex>

template <typename K, typename V>
class TickMap {
public:
    TickMap(int max_tick):_tick(max_tick) {}

    void Update() {
        Set<K> erase_keys;
        for (auto& item: _avliable) {
            --item.second;
            if (item.second <= 0) {
                erase_keys.insert(item.first);
            }
        }
        std::unique_lock<std::mutex> lck(_mtx);
        for (auto& k: erase_keys) {
            _avliable.erase(k);
            _inst.erase(k);
        }
    }

    V& operator[](const K& k) {
        auto& ref = _inst[k];
        if (_avliable[k] != std::numeric_limits<uint32_t>::max()) {
            _avliable[k] += 1;
        }
        return ref;
    }

    bool Exist(const K& k) {
        std::unique_lock<std::mutex> lck(_mtx);
        return _avliable.count(k);
    }
private:
    short _tick;

    std::mutex _mtx;

    Map<K, V> _inst;
    Map<K, uint32_t> _avliable;
};