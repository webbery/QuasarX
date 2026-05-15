#include "Algorithms/EMD.h"
#include <cmath>
#include <numeric>

EMD::EMD() {}

Vector<double> EMD::cubicSplineEnvelope(const Vector<double>& data,
                                          const Vector<int>& extremaIdx,
                                          int size) {
    Vector<double> envelope(size, 0.0);

    if (extremaIdx.empty()) {
        return envelope;
    }

    if (extremaIdx.size() == 1) {
        std::fill(envelope.begin(), envelope.end(), data[extremaIdx[0]]);
        return envelope;
    }

    // 简化实现：线性插值（完整版本可用 boost::math::interpolators::cubic_b_spline）
    for (size_t i = 0; i < extremaIdx.size() - 1; ++i) {
        int i0 = extremaIdx[i];
        int i1 = extremaIdx[i + 1];
        double v0 = data[i0];
        double v1 = data[i1];
        for (int j = i0; j <= i1; ++j) {
            double t = (double)(j - i0) / (i1 - i0);
            envelope[j] = v0 + t * (v1 - v0);
        }
    }

    // 外推端点
    for (int j = 0; j < extremaIdx[0]; ++j) {
        envelope[j] = envelope[extremaIdx[0]];
    }
    for (int j = extremaIdx.back() + 1; j < size; ++j) {
        envelope[j] = envelope[extremaIdx.back()];
    }

    return envelope;
}

bool EMD::isIMF(const Vector<double>& signal, const Vector<double>& meanEnvelope) {
    // 简化停止条件：均值包络幅值足够小
    double signalRange = 0.0;
    double minVal = signal[0], maxVal = signal[0];
    for (auto v : signal) {
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    signalRange = maxVal - minVal;
    if (signalRange < 1e-10) return true;

    double meanMax = 0.0;
    for (auto v : meanEnvelope) {
        double absV = std::abs(v);
        if (absV > meanMax) meanMax = absV;
    }

    // 阈值：均值包络最大值 < 信号范围的 2%
    return (meanMax / signalRange) < 0.02;
}

List<Vector<double>> EMD::emd(const Vector<double>& data, int numIMFs) {
    List<Vector<double>> imfs;
    Vector<double> residual = data;
    int maxSiftingIter = 10;

    for (int n = 0; n < numIMFs; ++n) {
        Vector<double> h = residual;

        for (int iter = 0; iter < maxSiftingIter; ++iter) {
            // 找极值点
            Vector<int> maxIdx, minIdx;
            for (int i = 1; i < (int)h.size() - 1; ++i) {
                if (h[i] > h[i - 1] && h[i] > h[i + 1]) {
                    maxIdx.push_back(i);
                } else if (h[i] < h[i - 1] && h[i] < h[i + 1]) {
                    minIdx.push_back(i);
                }
            }

            if (maxIdx.size() < 2 || minIdx.size() < 2) {
                break;  // 极值点不足，停止筛选
            }

            // 上下包络
            auto upperEnv = cubicSplineEnvelope(h, maxIdx, h.size());
            auto lowerEnv = cubicSplineEnvelope(h, minIdx, h.size());

            // 均值包络
            Vector<double> meanEnv(h.size());
            for (size_t i = 0; i < h.size(); ++i) {
                meanEnv[i] = (upperEnv[i] + lowerEnv[i]) / 2.0;
            }

            if (isIMF(h, meanEnv)) {
                break;
            }

            // 逐元素减法: h = h - meanEnv
            for (size_t i = 0; i < h.size(); ++i) {
                h[i] = h[i] - meanEnv[i];
            }
        }

        imfs.push_back(h);

        // 更新残差: residual = residual - h
        for (size_t i = 0; i < residual.size(); ++i) {
            residual[i] = residual[i] - h[i];
        }

        // 残差为单调函数时停止
        bool monotonic = true;
        bool increasing = (residual.size() > 1 && residual[1] >= residual[0]);
        for (size_t i = 1; i < residual.size(); ++i) {
            if (increasing && residual[i] < residual[i - 1]) { monotonic = false; break; }
            if (!increasing && residual[i] > residual[i - 1]) { monotonic = false; break; }
        }
        if (monotonic) break;
    }

    // 补齐不足 numIMFs 的空 IMF
    while ((int)imfs.size() < numIMFs) {
        imfs.push_back(Vector<double>(data.size(), 0.0));
    }

    return imfs;
}
