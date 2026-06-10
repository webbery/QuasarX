#include "Algorithms/CEEMDAN.h"
#include "Algorithms/EMD_SIMD.h"
#include <cmath>
#include <numeric>
#include <fmt/core.h>

CEEMDAN::CEEMDAN() {}

// ============================================================================
// 公开接口: decompose
// ============================================================================

CEEMDAN::Result CEEMDAN::decompose(const Vector<double>& data, const Config& cfg) {
    Result result;
    result.residual = data;

    if (data.size() < 10) {
        _summary = fmt::format("CEEMDAN skipped: data too short ({} points)", data.size());
        result.actualIMFs = 0;
        return result;
    }

    // 1. 预生成噪声集合
    auto noises = generateNoiseEnsemble(data.size(), cfg);

    // 2. 逐阶段提取 IMF
    for (int k = 1; k <= cfg.numIMFs; ++k) {
        Vector<double> imf_k;

        if (k == 1) {
            // 阶段 1: 直接加白噪声
            imf_k = computeFirstIMF(data, noises, cfg);
        } else {
            // 阶段 k: 添加 EMD_k(噪声)
            imf_k = computeIMFk(result.residual, k, noises, cfg);
        }

        result.imfs.push_back(imf_k);

        // 更新残差 (SIMD): residual -= imf_k
        int sz = static_cast<int>(result.residual.size());
        simd_sub(result.residual.data(), imf_k.data(), result.residual.data(), sz);

        // 检查残差是否单调 → 单调则停止
        if (isMonotonic(result.residual)) {
            break;
        }
    }

    result.actualIMFs = static_cast<int>(result.imfs.size());
    result.reconstructionError = computeReconstructionError(data, result);

    _summary = fmt::format("CEEMDAN: {} IMFs from {} points, {} ensembles, recon_err={:.2e}",
                           result.actualIMFs, data.size(), cfg.ensembles, result.reconstructionError);

    return result;
}

String CEEMDAN::getSummary() const {
    return _summary;
}

// ============================================================================
// 噪声生成
// ============================================================================

Vector<Vector<double>> CEEMDAN::generateNoiseEnsemble(size_t n, const Config& cfg) {
    Vector<Vector<double>> noises(cfg.ensembles, Vector<double>(n));

    std::mt19937_64 rng(cfg.seed != 0 ? cfg.seed : std::random_device{}());

    double signalStd = computeStd(Vector<double>()); // 暂未传入信号，在阶段中自适应
    (void)signalStd;

    // 生成标准正态分布噪声 (均值=0, 标准差=1)，实际幅度在阶段中乘以 ε
    for (int i = 0; i < cfg.ensembles; ++i) {
        std::normal_distribution<double> dist(0.0, 1.0);
        for (size_t j = 0; j < n; ++j) {
            noises[i][j] = dist(rng);
        }
    }

    return noises;
}

double CEEMDAN::computeStd(const Vector<double>& data) {
    if (data.empty()) return 1.0;
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double sqSum = 0.0;
    for (double v : data) sqSum += (v - mean) * (v - mean);
    return std::sqrt(sqSum / data.size());
}

// ============================================================================
// 阶段 1: IMF_1 = mean( EMD_1(x + ε·w^i) )
// ============================================================================

Vector<double> CEEMDAN::computeFirstIMF(const Vector<double>& data,
                                          const Vector<Vector<double>>& noises,
                                          const Config& cfg) {
    size_t n = data.size();
    double dataStd = computeStd(data);
    double epsilon = cfg.noiseStd * dataStd;

    // 每个噪声样本: x + ε·w^i，提取第 1 个 IMF
    Vector<Vector<double>> firstIMFs(cfg.ensembles);

    for (int i = 0; i < cfg.ensembles; ++i) {
        // 构造带噪声信号 (SIMD): noisy = data + ε·noise
        Vector<double> noisy(n);
        simd_fma(data.data(), noises[i].data(), epsilon, noisy.data(), n);

        // 执行 EMD，取第 1 个 IMF
        auto imfs = simd_emd(noisy, 1, cfg.maxSiftingIter, cfg.sdThreshold);
        if (!imfs.empty()) {
            firstIMFs[i] = imfs.front();
        } else {
            firstIMFs[i] = Vector<double>(n, 0.0);
        }
    }

    // 集合平均 (SIMD)
    Vector<double> result(n);
    simd_ensemble_average(firstIMFs, result.data(), n);
    return result;
}

// ============================================================================
// 阶段 k: IMF_k = mean( EMD_1(r_{k-1} + ε_{k-1}·EMD_k(w^i)) )
// ============================================================================

Vector<double> CEEMDAN::computeIMFk(const Vector<double>& residual,
                                      int k,
                                      const Vector<Vector<double>>& noises,
                                      const Config& cfg) {
    size_t n = residual.size();
    double resStd = computeStd(residual);
    double epsilon = cfg.noiseStd * resStd;

    Vector<Vector<double>> imfBuffers(cfg.ensembles);

    for (int i = 0; i < cfg.ensembles; ++i) {
        // 获取噪声的第 k 个 IMF 分量
        Vector<double> noiseIMF = getNoiseIMF(noises[i], k, cfg);

        // 构造信号: r_{k-1} + ε·EMD_k(w^i)
        Vector<double> perturbed(n);
        simd_fma(residual.data(), noiseIMF.data(), epsilon, perturbed.data(), n);

        // 执行 EMD，取第 1 个 IMF
        auto imfs = simd_emd(perturbed, 1, cfg.maxSiftingIter, cfg.sdThreshold);
        if (!imfs.empty()) {
            imfBuffers[i] = imfs.front();
        } else {
            imfBuffers[i] = Vector<double>(n, 0.0);
        }
    }

    // 集合平均 (SIMD)
    Vector<double> result(n);
    simd_ensemble_average(imfBuffers, result.data(), n);
    return result;
}

// ============================================================================
// 噪声 IMF 提取: 对纯噪声执行 EMD 并返回第 k 个分量
// ============================================================================

Vector<double> CEEMDAN::getNoiseIMF(const Vector<double>& noise, int k, const Config& cfg) {
    // 对噪声执行完整的 EMD 分解
    auto imfs = simd_emd(noise, k, cfg.maxSiftingIter, cfg.sdThreshold);

    // 返回第 k 个 IMF (0-indexed)
    if (k <= static_cast<int>(imfs.size())) {
        auto it = imfs.begin();
        std::advance(it, k - 1);
        return *it;
    }

    // 如果噪声 EMD 无法产生第 k 个 IMF，返回零向量
    return Vector<double>(noise.size(), 0.0);
}

// ============================================================================
// 辅助函数
// ============================================================================

bool CEEMDAN::isMonotonic(const Vector<double>& v) {
    if (v.size() < 2) return true;
    bool increasing = (v[1] >= v[0]);
    for (size_t i = 1; i < v.size(); ++i) {
        if (increasing && v[i] < v[i - 1]) return false;
        if (!increasing && v[i] > v[i - 1]) return false;
    }
    return true;
}

double CEEMDAN::computeReconstructionError(const Vector<double>& original,
                                             const Result& result) {
    if (original.empty()) return 0.0;
    size_t n = original.size();

    // reconstructed = Σ imfs + residual
    Vector<double> reconstructed(n, 0.0);
    for (const auto& imf : result.imfs) {
        simd_accumulate(imf.data(), reconstructed.data(), n);
    }
    simd_accumulate(result.residual.data(), reconstructed.data(), n);

    // RMS error
    double sqError = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double err = reconstructed[i] - original[i];
        sqError += err * err;
    }
    return std::sqrt(sqError / n);
}
