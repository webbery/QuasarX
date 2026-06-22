#include "Algorithms/EMD_SIMD.h"
#include <cmath>
#include <numeric>

// ============================================================================
// EMD_SIMD.cpp — SIMD 加速的 EMD 核心函数实现
//
// 这些函数是对原 EMD.cpp 中同名函数的 SIMD 优化版本，保持算法逻辑不变。
// ============================================================================

/**
 * @brief 三次样条包络线生成
 *
 * 在极值点之间使用自然三次样条插值（与 pyEMD 默认行为一致）。
 * 端点外推：使用最近极值点的值。
 */
Vector<double> simd_cubicSplineEnvelope(const Vector<double>& data,
                                          const Vector<int>& extremaIdx,
                                          int size) {
    Vector<double> envelope(size, 0.0);
    double* envPtr = envelope.data();

    if (extremaIdx.empty()) {
        return envelope;
    }

    if (extremaIdx.size() == 1) {
        double val = data[extremaIdx[0]];
        for (int j = 0; j < size; ++j) envPtr[j] = val;
        return envelope;
    }

    // 收集极值点对应的数据值
    int nPts = static_cast<int>(extremaIdx.size());
    Vector<int> xPts(nPts);
    Vector<double> yPts(nPts);
    for (int i = 0; i < nPts; ++i) {
        xPts[i] = extremaIdx[i];
        yPts[i] = data[extremaIdx[i]];
    }

    // 使用三次样条在极值点间插值
    natural_cubic_spline(xPts.data(), yPts.data(), nPts, envPtr, 0, size - 1);

    // 外推端点：使用最近极值点的值（与自然边界条件一致）
    double firstVal = envelope[extremaIdx[0]];
    double lastVal = envelope[extremaIdx.back()];
    for (int j = 0; j < extremaIdx[0]; ++j) envPtr[j] = firstVal;
    for (int j = extremaIdx.back() + 1; j < size; ++j) envPtr[j] = lastVal;

    return envelope;
}

/**
 * @brief SIMD 加速的 IMF 停止条件判断
 */
bool simd_isIMF(const Vector<double>& signal, const Vector<double>& meanEnvelope) {
    double signalRange = simd_range(signal.data(), signal.size());
    if (signalRange < 1e-10) return true;

    double meanMax = simd_abs_max(meanEnvelope.data(), meanEnvelope.size());
    return (meanMax / signalRange) < 0.02;
}

/**
 * @brief SIMD 加速的极值点检测
 */
void simd_findExtrema(const Vector<double>& h, Vector<int>& maxIdx, Vector<int>& minIdx) {
    maxIdx.clear();
    minIdx.clear();
    int n = static_cast<int>(h.size());
    if (n < 3) return;

    const double* data = h.data();
    for (int i = 1; i < n - 1; ++i) {
        if (data[i] > data[i - 1] && data[i] > data[i + 1]) {
            maxIdx.push_back(i);
        } else if (data[i] < data[i - 1] && data[i] < data[i + 1]) {
            minIdx.push_back(i);
        }
    }
}

/**
 * @brief SIMD 加速的单 IMF 提取（筛选过程）
 *
 * 输入: residual (残差信号)
 * 输出: h (提取出的 IMF)
 */
Vector<double> simd_extractIMF(const Vector<double>& residual,
                                 int maxSiftingIter,
                                 double sdThreshold) {
    Vector<double> h = residual;
    int n = static_cast<int>(h.size());
    double* hPtr = h.data();

    Vector<double> meanEnv(n);

    for (int iter = 0; iter < maxSiftingIter; ++iter) {
        // 找极值点
        Vector<int> maxIdx, minIdx;
        simd_findExtrema(h, maxIdx, minIdx);

        if (maxIdx.size() < 2 || minIdx.size() < 2) {
            break;  // 极值点不足，停止筛选
        }

        // SIMD 包络线
        auto upperEnv = simd_cubicSplineEnvelope(h, maxIdx, n);
        auto lowerEnv = simd_cubicSplineEnvelope(h, minIdx, n);

        // SIMD 均值包络
        simd_mean_envelope(upperEnv.data(), lowerEnv.data(), meanEnv.data(), n);

        // 检查停止条件
        if (simd_isIMF(h, meanEnv)) {
            break;
        }

        // SIMD 筛选减法: h -= meanEnv
        simd_sifting_subtract(hPtr, meanEnv.data(), n);
    }

    return h;
}

/**
 * @brief SIMD 加速的 EMD 分解（完整流程）
 */
Vector<Vector<double>> simd_emd(const Vector<double>& data,
                                int numIMFs,
                                int maxSiftingIter,
                                double sdThreshold) {
    Vector<Vector<double>> imfs;
    Vector<double> residual = data;

    for (int n = 0; n < numIMFs; ++n) {
        Vector<double> h = simd_extractIMF(residual, maxSiftingIter, sdThreshold);
        imfs.push_back(h);

        // SIMD 残差更新: residual -= h
        int sz = static_cast<int>(residual.size());
        simd_sub(residual.data(), h.data(), residual.data(), sz);

        // 检查残差是否单调
        bool monotonic = true;
        bool increasing = (sz > 1 && residual[1] >= residual[0]);
        for (int i = 1; i < sz; ++i) {
            if (increasing && residual[i] < residual[i - 1]) { monotonic = false; break; }
            if (!increasing && residual[i] > residual[i - 1]) { monotonic = false; break; }
        }
        if (monotonic) break;
    }

    // 补齐不足 numIMFs 的空 IMF
    while (static_cast<int>(imfs.size()) < numIMFs) {
        imfs.push_back(Vector<double>(data.size(), 0.0));
    }

    return imfs;
}
