#pragma once
#ifdef _WIN32
#pragma warning( push, 1 )
#endif
#define FMT_HEADER_ONLY
#include "fmt/color.h"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include <fmt/format.h>
#ifdef _WIN32
#pragma warning( pop )
#else
#ifndef FMTLOG_HEADER_ONLY
#define FMTLOG_HEADER_ONLY
#endif // FMTLOG_HEADER_ONLY
#endif
#include "fmtlog.h"

#ifndef INFO
#define INFO(fmt_str, ...) \
  fmt::print("" fmt_str "\n", ##__VA_ARGS__);
#endif
#ifndef LOG
#define LOG(fmt_str, ...) \
  FMTLOG(fmtlog::INF, fmt_str "\n", ##__VA_ARGS__);
#endif
#ifndef WARN
#define WARN(fmt_str, ...) \
  fmt::print(fmt::fg(fmt::color::khaki), "WARNING[{}:{}]: " fmt_str "\n", __FILE__, __LINE__, ##__VA_ARGS__);
#endif
#ifndef FATAL
#define FATAL(fmt_str, ...) \
  fmt::print(fmt::fg(fmt::color::red), "ERROR: " fmt_str "\n", ##__VA_ARGS__)
#endif
#ifndef DEBUG_INFO
#ifdef _DEBUG 
#define DEBUG_INFO(fmt_str, ...) \
  fmt::print("{} " fmt_str "\n", ToString(Now()), ##__VA_ARGS__);
#else
#define DEBUG_INFO(fmt_str, ...)
#endif
#endif