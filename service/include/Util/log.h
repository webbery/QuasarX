#pragma once
#ifdef _WIN32
#pragma warning( push, 1 )
#endif
#include "fmt/color.h"
#include "fmt/core.h"
#ifdef _WIN32
#pragma warning( pop )
#endif

#ifndef INFO
#define INFO(fmt_str, ...) \
  fmt::print("" fmt_str "\n", ##__VA_ARGS__);
#endif
#ifndef LOG
#define LOG(fmt_str, ...) \
  fmt::print("{} " fmt_str "\n", ToString(Now()), ##__VA_ARGS__);
#endif
#ifndef WARN
#define WARN(fmt_str, ...) \
  fmt::print(fmt::fg(fmt::color::khaki), "WARNING[{}:{}]: " fmt_str "\n", __FILE__, __LINE__, ##__VA_ARGS__);
#endif
#ifndef FATAL
#define FATAL(fmt_str, ...) \
  fmt::print(fmt::fg(fmt::color::red), "ERROR: " fmt_str "\n", ##__VA_ARGS__)
#endif