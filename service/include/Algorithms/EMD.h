#pragma once
#include "std_header.h"

/**
 * EMD — Empirical Mode Decomposition（经验模态分解）
 *
 * 将非平稳、非线性信号自适应分解为多个本征模态函数（IMF）
 * 和一个残差分量。适用于金融时间序列的多尺度分析。
 */
class EMD {
public:
    EMD();

    /**
     * @brief 经验模态分解
     * @param data 输入时间序列
     * @param numIMFs 期望的 IMF 分量数量
     * @return IMF 分量列表，每个 IMF 为等长时间序列
     */
    Vector<Vector<double>> emd(const Vector<double>& data, int numIMFs = 5);

private:
    /**
     * @brief 三次样条插值生成包络线
     */
    Vector<double> cubicSplineEnvelope(const Vector<double>& data,
                                        const Vector<int>& extremaIdx,
                                        int size);

    /**
     * @brief 判断是否满足 IMF 停止条件
     */
    bool isIMF(const Vector<double>& signal, const Vector<double>& meanEnvelope);
};
