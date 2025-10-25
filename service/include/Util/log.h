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
#include "spdlog/spdlog.h"

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef INFO
#define INFO(fmt_str, ...) \
  fmt::print("{} " fmt_str "\n", ToString(Now()),##__VA_ARGS__);
#endif
//#ifndef CUSTOM_WARN
//#define CUSTOM_WARN(fmt_str, ...) \
//  fmt::print(fmt_str "\n",##__VA_ARGS__);
//#endif
#ifndef LOG
#define LOG(...) \
  SPDLOG_INFO(__VA_ARGS__);
#endif
#ifndef WARN
#define WARN(...) \
  {SPDLOG_WARN(__VA_ARGS__);fmt::print(##__VA_ARGS__);fmt::print("\n");}
#endif
#ifndef FATAL
#define FATAL(...) \
  SPDLOG_ERROR( __VA_ARGS__);
#endif
#ifndef DEBUG_INFO
#ifdef _DEBUG 
#define DEBUG_INFO(...) \
  SPDLOG_DEBUG(__VA_ARGS__);
#else
#define DEBUG_INFO(fmt_str, ...)
#endif
#endif
#else // defined(_MSC_VER) && !defined(__clang__)
#ifndef INF.............................O
#define INFO(fmt_str, ...) \
  fmt::print("{} " fmt_str "\n", ToString(Now()),##__VA_ARGS__);
#endif
#ifndef LOG
#define LOG(fmt_str, ...) \
  SPDLOG_INFO(fmt_str, ##__VA_ARGS__);
#endif
#ifndef WARN
#define WARN(fmt_str, ...) \
  SPDLOG_WARN(fmt_str, ##__VA_ARGS__);
#endif
#ifndef FATAL
#define FATAL(fmt_str, ...) \
  SPDLOG_ERROR(fmt_str, ##__VA_ARGS__);
#endif
#ifndef DEBUG_INFO
#ifdef _DEBUG 
#define DEBUG_INFO(fmt_str, ...) \
  SPDLOG_DEBUG(fmt_str, ##__VA_ARGS__);
#else
#define DEBUG_INFO(fmt_str, ...)
#endif
#endif
#endif //defined(_MSC_VER) && !defined(__clang__)