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
     *
     * 零均值保证：真正经过筛选（sifting）的 IMF 分量会被显式零均值化，
     * 偏移量转移给残差以保持「Σ IMF + 残差 == 原信号」的重构不变式。
     * 极值点不足而退化成原始残差的分量（单调趋势）不做零均值化。
     */
    Vector<Vector<double>> emd(const Vector<double>& data, int numIMFs = 5, bool zeroPad = true);

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
