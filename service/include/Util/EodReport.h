#pragma once
#include "std_header.h"
#include "json.hpp"
#include "DataContext.h"
#include "StrategyNode.h"
#include "MarketTiming/ManualTiming.h"

class Server;

/**
 * 日终策略报告配置
 */
struct EodDebugSpec {
    bool email = true;
    Vector<String> explain;           // 解释表达式列表（标量结果 + 子条件分解）
    Vector<String> classNames;        // 概率列语义，可空（默认 P0/P1/P2）
    int  seriesBars = 60;             // 默认 60 根，min(60, 窗口长度)
    Vector<String> seriesColumns;     // 空 = 自动（模型特征 + 概率 + OHLCV + signal）
    bool attachSeries = true;         // 必附 series.csv
    bool attachSnapshot = true;       // 必附 snapshot.csv
    bool attachFullDump = false;      // 默认不附全量 dump
    int  holdTopN = 5;                // HOLD 标的按 |filtered_strength| 取 TopN
    int  maxSnapshotColumns = 40;     // snapshot 列数上限，避免 925 列全量
    double maxAttachMB = 20.0;        // 附件总大小上限，超限只落盘不外发
};

/**
 * 日终策略报告输出
 */
struct EodDebugReport {
    nlohmann::json rows;              // 结构化数值（同时写入 decisions/{date}.json）
    String htmlSection;               // 邮件正文片段
    Vector<String> attachments;       // 实际落盘、可外发的文件
    String summaryLine;               // "snapshot.csv (3.1 KB) · series.csv (86 KB)"
    Vector<String> warnings;          // 降级说明（缺失列、超限丢弃等）
};

/**
 * 构建日终策略报告
 * 
 * @param server Server 实例
 * @param strategy 策略名称
 * @param graph 策略图（flow._graph）
 * @param context 冻结在决策 bar 的 DataContext（非 const，因为 explain 求值可能修改）
 * @param universe 信号池（SignalNode::GetPool()）
 * @param decisions 决策快照（ManualTiming::getDecisions()）
 * @param spec 配置
 * @return EodDebugReport 报告结构
 */
EodDebugReport BuildEodDebugReport(Server* server,
                                   const String& strategy,
                                   const List<QNode*>& graph,
                                   DataContext& context,
                                   const Set<symbol_t>& universe,
                                   const Map<symbol_t, DecisionSnapshot>& decisions,
                                   const EodDebugSpec& spec);
