#include "Algorithms/EMD_SIMD.h"
#include <cmath>
#include <numeric>

// ============================================================================
// EMD_SIMD.cpp — SIMD 加速的 EMD 核心函数实现
//
// 这些函数是对原 EMD.cpp 中同名函数的 SIMD 优化版本，保持算法逻辑不变。
// ============================================================================

/**
 * @brief SIMD 加速的包络线生成（线性插值替代三次样条）
 *
 * 在极值点之间用 SIMD 线性插值，端点外推。
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

    // 极值点间 SIMD 线性插值
    for (size_t i = 0; i < extremaIdx.size() - 1; ++i) {
        int i0 = extremaIdx[i];
        int i1 = extremaIdx[i + 1];
        double v0 = data[i0];
        double v1 = data[i1];
        simd_linear_interp(i0, i1, v0, v1, envPtr);
    }

    // 外推端点
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
List<Vector<double>> simd_emd(const Vector<double>& data,
                                int numIMFs,
                                int maxSiftingIter,
                                double sdThreshold) {
    List<Vector<double>> imfs;
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
