#pragma once
#include "Decision.h"
#include "Util/DuckDBBaseT.h"
#include <vector>

class DecisionDB : public DuckDBBaseT<DecisionDB> {
public:
    static DecisionDB& instance();

    // 写入决策记录，返回分配的 id
    int insertDecision(const DecisionRecord& record);

    // 按日期查询决策列表（date 格式 "YYYY-MM-DD"）
    std::vector<DecisionRecord> queryByDate(const std::string& date);

    // 标记决策已执行
    bool markExecuted(int id, int64_t exec_qty, double exec_price);

private:
    friend class DuckDBBaseT<DecisionDB>;
    void ensureTables();
};
