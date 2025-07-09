#pragma once
#include "std_header.h"

namespace finance {

/**
 * @brief 三阶段增长模型
 */
double stage3GM(double g1, double g2, double D, double T1, double T2, double r);

double sharp_ratio(double ret, double sigma, double free_rate);

double correlation(const std::vector<double>& r1, const std::vector<double>& v2);

}

/**
 * 计算后复权
 */
void forward_adjust(const Vector<double>& prices, const List<Pair<int, double>>& fh);
