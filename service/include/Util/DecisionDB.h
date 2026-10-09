#pragma once
#include "Decision.h"
#include "Bridge/exchange.h"
#include "Util/DuckDBBaseT.h"
#include <vector>

struct DailyPositionRecord {
    std::string strategy;
    symbol_t symbol;
    time_t date;          // 交易日（当日零点 Unix 时间戳）
    int64_t position;     // 持仓股数
    double close_price;   // 当日收盘价
};

class DecisionDB : public DuckDBBaseT<DecisionDB> {
public:
    static DecisionDB& instance();

    // 写入决策记录，返回分配的 id
    int insertDecision(const DecisionRecord& record);

    // 按日期查询决策列表（date 格式 "YYYY-MM-DD"）
    std::vector<DecisionRecord> queryByDate(const std::string& date);

    // 检查决策是否存在
    bool exists(int id);

    // 按日期清除决策记录，返回删除行数
    int clearByDate(const std::string& date);

    // 标记决策已执行
    bool markExecuted(int id, int64_t exec_qty, double exec_price);

    // 标记决策已确认（关闭，不执行）
    bool markClosed(int id);

    // 日终持仓快照
    void insertDailyPosition(const DailyPositionRecord& record);
    std::vector<DailyPositionRecord> queryDailyPositions(const std::string& strategy,
                                                         time_t startDate = 0,
                                                         time_t endDate = 0);

    // 已成交决策（成交流水）：按标的分组，组内按成交时间正序。
    // TradeReport._side 0=买/1=卖，_flag 0=开仓/1=平仓，与 AddOrderBySide 一致。
    List<TradeInfo> queryExecutedFills(const std::string& strategy);

    // 有成交记录的所有策略名（去重）
    List<String> queryFilledStrategies();

    // 当前最大决策 id：进程重启后播种 id 计数器，
    // 否则 id 从 1 重新计数会撞上 PRIMARY KEY，INSERT OR REPLACE 覆盖历史成交。
    int64_t maxDecisionId();

    // 删除指定策略的所有历史记录（decisions + daily_positions）
    int deleteByStrategy(const std::string& strategy);

private:
    friend class DuckDBBaseT<DecisionDB>;
    void ensureTables();
};
