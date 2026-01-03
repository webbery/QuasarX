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
using Boolean = Expected<bool, String>;
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

using feature_t = std::variant<bool, std::string, uint64_t, double, Vector<double>, Vector<float>, Vector<uint64_t>, Eigen::MatrixXd>;
