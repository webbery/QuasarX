#pragma once
#include "Decision.h"
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

    // 标记决策已执行
    bool markExecuted(int id, int64_t exec_qty, double exec_price);

    // 日终持仓快照
    void insertDailyPosition(const DailyPositionRecord& record);
    std::vector<DailyPositionRecord> queryDailyPositions(const std::string& strategy,
                                                         time_t startDate = 0,
                                                         time_t endDate = 0);

private:
    friend class DuckDBBaseT<DecisionDB>;
    void ensureTables();
};
