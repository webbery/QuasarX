#pragma once
#include "DataGroup.h"

namespace finance {

/**
 * @brief 三阶段增长模型
 */
double stage3GM(double g1, double g2, double D, double T1, double T2, double r);

}

bool LoadStockQuote(DataFrame& df, const String& path);