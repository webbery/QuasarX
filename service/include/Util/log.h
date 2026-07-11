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
#include "Util/DuckDBLogger.h"

// ──────────────────────────────────────────────────────────────────────
// 节点 IO 日志宏（仅实盘模式，编译期开关控制）
//
// 用法（在节点 Process 函数中）：
//   NODE_IO_LOG("signal", _id,
//       input["buy"] = _buyExpr;
//       output["sig"] = 1;
//       meta["custom"] = "value";  // 可选
//   )
//
// 说明：
//   - _server 和 context 由宏自动捕获（要求节点类有 _server 成员，Process 有 context 参数）
//   - 宏内部自动构建 input/output/meta 三个 json 对象
//   - meta 自动填充 "mode":"realtime" 和 "epoch"
//   - 未定义 ENABLE_NODE_IO_LOGGING 时宏展开为空（零开销）
// ──────────────────────────────────────────────────────────────────────

#ifdef ENABLE_NODE_IO_LOGGING

#define NODE_IO_LOG(node_type, node_id, body) \
    do { \
        if (_server->GetRunningMode() != RuningType::Backtest && \
            DuckDBLogger::instance().is_initialized()) { \
            try { \
                nlohmann::json input; \
                nlohmann::json output; \
                nlohmann::json meta; \
                meta["mode"] = "realtime"; \
                meta["epoch"] = context.GetEpoch(); \
                { body } \
                DuckDBLogger::instance().log_node_io( \
                    context.CurrentStrategy(), \
                    context.GetEpoch(), \
                    node_type, \
                    std::to_string(node_id), \
                    input.dump(), output.dump(), meta.dump()); \
            } catch (const std::exception& e) { \
                WARN("[NodeIO] " node_type " logging failed: {}", e.what()); \
            } \
        } \
    } while(0)

/**
 * @brief 轻量版宏（metadata 用静态字符串，避免构建 meta json）
 */
#define NODE_IO_LOG_FAST(node_type, node_id, body) \
    do { \
        if (_server->GetRunningMode() != RuningType::Backtest && \
            DuckDBLogger::instance().is_initialized()) { \
            try { \
                nlohmann::json input; \
                nlohmann::json output; \
                { body } \
                DuckDBLogger::instance().log_node_io( \
                    context.CurrentStrategy(), \
                    context.GetEpoch(), \
                    node_type, \
                    std::to_string(node_id), \
                    input.dump(), output.dump(), \
                    R"({"mode":"realtime","epoch":)" + std::to_string(context.GetEpoch()) + "}"); \
            } catch (const std::exception& e) { \
                WARN("[NodeIO] " node_type " logging failed: {}", e.what()); \
            } \
        } \
    } while(0)

#else
    // 未定义时宏展开为空，零字节代码
    #define NODE_IO_LOG(node_type, node_id, body)
    #define NODE_IO_LOG_FAST(node_type, node_id, body)
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef INFO
#define INFO(...) \
    SPDLOG_WARN(__VA_ARGS__);fmt::print("{} ", ToString(Now())); fmt::print(##__VA_ARGS__);fmt::print("\n");
#endif
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
#ifndef IMPORTANT
#define IMPORTANT(...) \
  {SPDLOG_WARN(__VA_ARGS__);fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold, "[重要] ");fmt::print(##__VA_ARGS__);fmt::print("\n");}
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
#ifndef INFO
#define INFO(fmt_str, ...) \
  fmt::print("{} {} " fmt_str "\n", ToString(Now()), __LINE__, ##__VA_ARGS__);SPDLOG_INFO(fmt_str, ##__VA_ARGS__);
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
  fmt::print("{} {} " fmt_str "\n", ToString(Now()), __LINE__, ##__VA_ARGS__);SPDLOG_ERROR(fmt_str, ##__VA_ARGS__);
#endif
#ifndef IMPORTANT
#define IMPORTANT(fmt_str, ...) \
  fmt::print("{} {} " fmt_str "\n", ToString(Now()), __LINE__, ##__VA_ARGS__);SPDLOG_WARN(fmt_str, ##__VA_ARGS__);
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

// 抛出异常并记录日志
#ifndef THROW_EXCEPTION
#define THROW_EXCEPTION(fmt_str, ...) \
    do { \
        std::string msg = fmt::format(fmt_str, ##__VA_ARGS__); \
        SPDLOG_ERROR("{}", msg); \
        throw std::runtime_error(msg); \
    } while(0)
#endif

// ==================== 策略日志宏（DuckDB） ====================
// 策略执行日志自动写入DuckDB + spdlog双写
// 普通日志和性能日志暂不接入DuckDB，仅使用spdlog

#ifndef STRATEGY_LOG
#define STRATEGY_LOG(strategy, level, fmt_str, ...) \
    do { \
        std::string _msg = fmt::format(fmt_str, ##__VA_ARGS__); \
        SPDLOG_INFO("[{}] {}", strategy, _msg); \
        if (DuckDBLogger::instance().is_initialized()) { \
            DuckDBLogger::instance().log_strategy(strategy, level, _msg); \
        } \
    } while(0)
#endif

#ifndef STRATEGY_INFO
#define STRATEGY_INFO(strategy, fmt_str, ...) \
    STRATEGY_LOG(strategy, "INFO", fmt_str, ##__VA_ARGS__)
#endif

#ifndef STRATEGY_WARN
#define STRATEGY_WARN(strategy, fmt_str, ...) \
    STRATEGY_LOG(strategy, "WARN", fmt_str, ##__VA_ARGS__)
#endif

#ifndef STRATEGY_ERROR
#define STRATEGY_ERROR(strategy, fmt_str, ...) \
    STRATEGY_LOG(strategy, "ERROR", fmt_str, ##__VA_ARGS__)
#endif

#ifndef STRATEGY_IMPORTANT
#define STRATEGY_IMPORTANT(strategy, fmt_str, ...) \
    STRATEGY_LOG(strategy, "IMPORTANT", fmt_str, ##__VA_ARGS__)
#endif

// 带上下文JSON的策略日志
#ifndef STRATEGY_LOG_WITH_CTX
#define STRATEGY_LOG_WITH_CTX(strategy, level, context_json, fmt_str, ...) \
    do { \
        std::string _msg = fmt::format(fmt_str, ##__VA_ARGS__); \
        SPDLOG_INFO("[{}] {}", strategy, _msg); \
        if (DuckDBLogger::instance().is_initialized()) { \
            DuckDBLogger::instance().log_strategy(strategy, level, _msg, context_json); \
        } \
    } while(0)
#endif