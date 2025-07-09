#pragma once
#include "std_header.h"
#ifdef _USE_BOOST_
#include <boost/filesystem.hpp>
#include <boost/variant.hpp>
#include <boost/function_types/function_type.hpp>
#include <boost/function_types/parameter_types.hpp>
using namespace boost;
#else
#include <type_traits>
#include <utility>
#if __has_include(<filesystem>)
#include <variant>
#include <filesystem>
#elif __has_include(<experimental/filesystem>)
#include <variant>
#include <experimental/filesystem>
using namespace std::experimental;
#else
#error("compiler is not support C++17, please use boost.")
#endif
#endif // _USE_BOOST_
#include "system.h"

template<size_t N>
struct FixedString {
    constexpr FixedString(const char(&str)[N]) {
        std::ranges::copy_n(str, N, value);
    }
    char value[N] = {0};
    constexpr const char* data() const { return value; }
};

template<typename Container>
void split(const String& str, Container& ret_, const char* sep) {
  using value_type = typename Container::value_type;
  size_t pre = 0, cur = 0;
  String token;
  while ((cur = str.find(String(sep), pre)) != String::npos) {
    token = str.substr(pre, cur - pre);
    cur += 1;
    pre = cur;
    if constexpr (std::is_same<int64_t, value_type>::value) {
      ret_.push_back(atol(token.c_str()));
    } else {
      ret_.push_back(token.c_str());
    }
  }
  if constexpr (std::is_same<int64_t, value_type>::value) {
    ret_.push_back(atol((str.substr(pre, str.size() - pre).c_str())));
  } else {
    ret_.push_back(str.substr(pre, str.size() - pre));
  }
}

template<typename Container>
String concat(const Container& data, const char* sep) {
  String ret;
  for (auto& item: data) {
    if constexpr (std::is_same_v<typename Container::value_type, symbol_t>) {
      ret += get_symbol(item) + sep;
    } else {
      ret += item + sep;
    }
  }
  if (!ret.empty()) ret.pop_back();
  return ret;
}
// template<typename Container>
// void split(const std::pmr::string& str, Container& ret_, const std::pmr::string& sep) {
//   using value_type = typename Container::value_type;
//   size_t pre = 0, cur = 0;
//   std::pmr::string token;
//   while ((cur = str.find(sep, pre)) != String::npos) {
//     token = str.substr(pre, cur - pre);
//     cur += 1;
//     pre = cur;
//     if constexpr (std::is_same<int64_t, value_type>::value) {
//       ret_.push_back(atol(token.c_str()));
//     } else {
//       ret_.push_back(token.c_str());
//     }
//   }
//   if constexpr (std::is_same<int64_t, value_type>::value) {
//     ret_.push_back(atol((str.substr(pre, str.size() - pre).c_str())));
//   } else {
//     ret_.push_back(str.substr(pre, str.size() - pre));
//   }
// }

String to_lower(const String& str);
String to_upper(const String& str);

List<String> get_similar_words(const String& input, const List<String>& candidates);

String trim(const String& str);

String to_dot_string(double v, char dot = ',');

String format_symbol(const String& symbol);

String to_gbk(const String& str);

String to_base64(const String& bin);