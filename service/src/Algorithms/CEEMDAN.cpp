#include "Algorithms/CEEMDAN.h"
#include "Algorithms/EMD_SIMD.h"
#include <cmath>
#include <numeric>
#include <fmt/core.h>

#ifdef _OPENMP
#include <omp.h>
#endif

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

    // 2. 预计算所有噪声的完整 EMD 分解（缓存，后续 stage 直接查表）
    auto noiseIMFs = precomputeNoiseIMFs(noises, cfg);

    // 3. 逐阶段提取 IMF
    for (int k = 1; k <= cfg.numIMFs; ++k) {
        Vector<double> imf_k;

        if (k == 1) {
            // 阶段 1: 直接加白噪声
            imf_k = computeFirstIMF(data, noises, cfg);
        } else {
            // 阶段 k: 使用缓存的噪声 IMF（无需重新分解）
            imf_k = computeIMFk(result.residual, k, noiseIMFs, cfg);
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
// 噪声 IMF 预计算（OpenMP 并行）
//
// 对每个噪声样本做完整 EMD 分解，缓存所有 IMF 分量。
// 后续 stage k 直接从缓存取第 k-1 个 IMF，避免重复分解。
//
// 原始开销: Σ(k=1..K) ensembles × EMD(k) ≈ K²/2 × ensembles × EMD(1)
// 缓存后:   ensembles × EMD(K)（一次性，可并行）
// ============================================================================

Vector<Vector<Vector<double>>> CEEMDAN::precomputeNoiseIMFs(
    const Vector<Vector<double>>& noises, const Config& cfg) {

    const int N = cfg.ensembles;
    // noiseIMFs[i][k] = 第 i 个噪声的第 k 个 IMF（0-indexed）
    Vector<Vector<Vector<double>>> noiseIMFs(N);

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int i = 0; i < N; ++i) {
        // 对单个噪声做完整 EMD 分解（提取 numIMFs 个分量）
        noiseIMFs[i] = simd_emd(noises[i], cfg.numIMFs,
                                 cfg.maxSiftingIter, cfg.sdThreshold);
    }

    return noiseIMFs;
}

// ============================================================================
// 阶段 1: IMF_1 = mean( EMD_1(x + ε·w^i) )  [OpenMP 并行]
// ============================================================================

Vector<double> CEEMDAN::computeFirstIMF(const Vector<double>& data,
                                          const Vector<Vector<double>>& noises,
                                          const Config& cfg) {
    size_t n = data.size();
    double dataStd = computeStd(data);
    double epsilon = cfg.noiseStd * dataStd;

    // 每个噪声样本: x + ε·w^i，提取第 1 个 IMF
    Vector<Vector<double>> firstIMFs(cfg.ensembles);

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
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
// 阶段 k: IMF_k = mean( EMD_1(r_{k-1} + ε·noiseIMF[k-1][i]) )  [OpenMP 并行]
//
// 使用预计算的 noiseIMFs 缓存，直接查表获取噪声的第 k 个 IMF，
// 不再调用 getNoiseIMF() 重新做 EMD 分解。
// ============================================================================

Vector<double> CEEMDAN::computeIMFk(const Vector<double>& residual,
                                      int k,
                                      const Vector<Vector<Vector<double>>>& noiseIMFs,
                                      const Config& cfg) {
    size_t n = residual.size();
    double resStd = computeStd(residual);
    double epsilon = cfg.noiseStd * resStd;

    Vector<Vector<double>> imfBuffers(cfg.ensembles);

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int i = 0; i < cfg.ensembles; ++i) {
        // 从缓存获取噪声的第 k 个 IMF（0-indexed: k-1）
        Vector<double> noiseIMF;
        if (k - 1 < static_cast<int>(noiseIMFs[i].size())) {
            noiseIMF = noiseIMFs[i][k - 1];
        } else {
            noiseIMF = Vector<double>(n, 0.0);
        }

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
