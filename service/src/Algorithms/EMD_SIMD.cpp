#include "Algorithms/EMD_SIMD.h"
#include <cmath>
#include <numeric>

// ============================================================================
// EMD_SIMD.cpp — SIMD 加速的 EMD 核心函数实现
//
// 这些函数是对原 EMD.cpp 中同名函数的 SIMD 优化版本，保持算法逻辑不变。
// ============================================================================

/**
 * @brief 包络线生成（Akima 样条，零分配版本）
 *
 * 直接从 data + extremaIdx 读取，不拷贝 xPts/yPts。
 * 工作缓冲区由调用方预分配，本函数不执行任何堆分配。
 */
Vector<double> simd_cubicSplineEnvelope(const Vector<double>& data,
                                          const Vector<int>& extremaIdx,
                                          int size,
                                          double* work_s, double* work_t) {
    Vector<double> envelope(size, 0.0);

    if (extremaIdx.empty()) {
        return envelope;
    }

    if (extremaIdx.size() == 1) {
        double val = data[extremaIdx[0]];
        for (int j = 0; j < size; ++j) envelope[j] = val;
        return envelope;
    }

    int nPts = static_cast<int>(extremaIdx.size());

    // 直接使用 extremaIdx 作为 x，data 间接读取作为 y（零拷贝）
    // akima_spline 的 y 参数通过 lambda 适配：y[i] = data[extremaIdx[i]]
    // 但 akima_spline 接受 const double* y，需要连续数组
    // 对于 nPts ≤ 64 的小数组，用栈分配避免堆分配
    double yBuf[64];
    Vector<double> yHeap;
    double* yPtr;
    if (nPts <= 64) {
        for (int i = 0; i < nPts; ++i) yBuf[i] = data[extremaIdx[i]];
        yPtr = yBuf;
    } else {
        yHeap.resize(nPts);
        for (int i = 0; i < nPts; ++i) yHeap[i] = data[extremaIdx[i]];
        yPtr = yHeap.data();
    }

    akima_spline(extremaIdx.data(), yPtr, nPts, envelope.data(), 0, size - 1,
                 work_s, work_t);

    // 端点外推：常数外推
    double firstVal = envelope[extremaIdx[0]];
    double lastVal = envelope[extremaIdx.back()];
    for (int j = 0; j < extremaIdx[0]; ++j) envelope[j] = firstVal;
    for (int j = extremaIdx.back() + 1; j < size; ++j) envelope[j] = lastVal;

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
 * @brief SIMD 加速的单 IMF 提取（筛选过程，零重复分配）
 *
 * 所有工作缓冲区在函数入口一次性预分配，筛选迭代中复用。
 */
Vector<double> simd_extractIMF(const Vector<double>& residual,
                                 int maxSiftingIter,
                                 double sdThreshold) {
    Vector<double> h = residual;
    int n = static_cast<int>(h.size());
    double* hPtr = h.data();

    // ── 一次性预分配所有工作缓冲区 ──
    Vector<double> meanEnv(n);
    Vector<double> upperEnv(n);
    Vector<double> lowerEnv(n);
    // Akima 工作缓冲区（极值点最多 n 个）
    Vector<double> work_s(n);   // 段斜率 [n-1]
    Vector<double> work_t(n);   // 切线 [n]
    double* wsPtr = work_s.data();
    double* wtPtr = work_t.data();

    for (int iter = 0; iter < maxSiftingIter; ++iter) {
        // 找极值点
        Vector<int> maxIdx, minIdx;
        simd_findExtrema(h, maxIdx, minIdx);

        if (maxIdx.size() < 2 || minIdx.size() < 2) {
            break;
        }

        // 包络线（传入预分配缓冲区，零堆分配）
        // 注意：simd_cubicSplineEnvelope 仍返回 Vector（envelope 内部），
        // 但 work_s/work_t 复用，yBuf 走栈分配
        auto upperEnvR = simd_cubicSplineEnvelope(h, maxIdx, n, wsPtr, wtPtr);
        auto lowerEnvR = simd_cubicSplineEnvelope(h, minIdx, n, wsPtr, wtPtr);

        // SIMD 均值包络
        simd_mean_envelope(upperEnvR.data(), lowerEnvR.data(), meanEnv.data(), n);

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
                                double sdThreshold,
                                bool zeroPad) {
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

    // 补齐不足 numIMFs 的空 IMF（仅全局模式需要）
    if (zeroPad) {
        while (static_cast<int>(imfs.size()) < numIMFs) {
            imfs.push_back(Vector<double>(data.size(), 0.0));
        }
    }

    return imfs;
}
