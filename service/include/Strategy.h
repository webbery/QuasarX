#pragma once
#include "std_header.h"
#include "json.hpp"

#define BASIC_NAME  "Basic"

enum class ContractOperator: unsigned char {
  Hold = 0,
  Long = 1,
  Sell = 2,
  Short = 4,
  Done = (0x1<<7),
};

enum class StrategyType: char {
    ST_Unknow,
    ST_InterDay,
    ST_IntraDay,
    ST_Count,
};

enum class PredictType: char {
    PT_UpDown,
    
};
class IStrategy {
public:
    virtual ~IStrategy() {}
    virtual int generate(const Vector<float>& prediction) = 0;
    virtual bool is_valid() = 0;
};

struct AgentStrategyInfo;
AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);
