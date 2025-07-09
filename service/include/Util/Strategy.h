#pragma once
#include "StrategySubSystem.h"

class Server;

AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);
