#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <array>
#include <variant>
#include <expected>
#include <queue>

#ifdef _WIN32
#pragma warning(disable: 4828)
#endif
#ifdef USE_PMR
using String = std::pmr::string;

using StringView = std::string_view;

template<typename T>
using Vector = std::pmr::vector<T>;

template <typename K, typename V>
using Map = std::pmr::map<K, V>;

template <typename K, typename V>
using MultiMap = std::multimap<K, V>;

template <typename T>
using List = std::pmr::list<T>;

template <typename T>
using Queue = std::queue<T>;

template <typename T>
using Set = std::set<T>;

template<typename T1, typename T2>
using Pair = std::pair<T1, T2>;

template<typename T, int Nm>
using Array = std::array<T, Nm>;
#else
using String = std::string;

using StringView = std::string_view;

template<typename T>
using Vector = std::vector<T>;

template <typename K, typename V>
using Map = std::map<K, V>;

template <typename K, typename V>
using MultiMap = std::multimap<K, V>;

template <typename T>
using List = std::list<T>;

template <typename T>
using Queue = std::queue<T>;

template <typename T>
using Set = std::set<T>;

template<typename T1, typename T2>
using Pair = std::pair<T1, T2>;

template<typename T, int Nm>
using Array = std::array<T, Nm>;

template <typename ...T>
using Tuple = std::tuple<T...>;

template <typename T, typename ...Args>
using UnorderedSet = std::unordered_set<T, Args...>;

template<typename T, typename Err>
using Expected = std::expected<T, Err>;
using Boolean = Expected<bool, int>;
#endif

#ifndef YEAR_DAY
#define YEAR_DAY    252
#endif
#ifndef SECOND_PER_YEAR
#define SECOND_PER_YEAR 21772800
#endif 
#ifndef SECOND_PER_DAY
#define SECOND_PER_DAY 86400
#endif

#include "Eigen/Core"
#include "Util/log.h"


enum class contract_type: char {
    stock = 0,
    future = 1,
    put = 2,
    call = 3,
    fund = 4,
    index = 5,
};

struct alignas(4) symbol_t {
    /**
    * 0 - stock, 1-future, 2- put option, 3- call option 4- fund 5- index 6- BTC
     */
    contract_type _type : 8;
    char _exchange:8;
    unsigned short _opt : 16;
    union {
        struct { // option info
            uint32_t _reserved : 12;
            uint32_t _year : 6;
            uint32_t _month : 4;
            uint32_t _price : 10; // unit is 100
        };
        uint32_t _symbol : 32;
    };
};

using run_id_t = uint16_t;
using context_t = std::variant<bool, String, uint64_t, Vector<float>, List<symbol_t>, double, Vector<double>, Vector<uint64_t>, Eigen::MatrixXd>;
