#include "Algorithms/VMD.h"
#include "Algorithms/FFT.h"
#include "Algorithms/EMD_SIMD.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <fmt/core.h>

using complex_t = fft::complex_t;

VMD::VMD() {}

// ============================================================================
// 对称延拓
// ============================================================================

Vector<double> VMD::symmetricPad(const Vector<double>& data, size_t& outSize) {
    size_t n = data.size();

    // 对齐 vmdpy: fftSize = 2*n (而非 next_pow2(2n))
    // 配合 FFT.h 解除 next_pow2 限制 (FFTW3 支持任意因子分解长度)
    size_t fftSize = 2 * n;
    outSize = fftSize;

    // vmdpy 风格镜像延拓: padLeft = n/2, padRight = n - n/2
    size_t padLeft = n / 2;
    size_t padRight = n - padLeft;

    Vector<double> padded(fftSize);

    // 左端镜像反射: padded[i] = data[padLeft-1-i] (关于 data[0] 镜像)
    for (size_t i = 0; i < padLeft; ++i) {
        size_t srcIdx = (padLeft - 1 - i < n) ? (padLeft - 1 - i) : 0;
        padded[i] = data[srcIdx];
    }

    // 原始数据
    for (size_t i = 0; i < n; ++i) {
        padded[padLeft + i] = data[i];
    }

    // 右端镜像反射: padded[padLeft+n+i] = data[n-1-i] (关于 data[n-1] 镜像)
    for (size_t i = 0; i < padRight; ++i) {
        size_t srcIdx = (n - 1 - i >= 0) ? (n - 1 - i) : 0;
        padded[padLeft + n + i] = data[srcIdx];
    }

    return padded;
}

Vector<double> VMD::unpad(const Vector<double>& padded, size_t origSize, size_t padLeft) {
    Vector<double> out(origSize);
    for (size_t i = 0; i < origSize; ++i) {
        out[i] = padded[padLeft + i];
    }
    return out;
}

// ============================================================================
// VMD 核心分解
// ============================================================================

VMD::Result VMD::decompose(const Vector<double>& data, const Config& cfg) {
    Result result;

    if (data.size() < 10) {
        _summary = fmt::format("VMD skipped: data too short ({} points)", data.size());
        result.actualK = 0;
        return result;
    }

    size_t origN = data.size();
    size_t fftSize;

    // 对称延拓到 2*n (对齐 vmdpy 黄金标准),不再 next_pow2
    Vector<double> padded;
    if (cfg.symmetricPad) {
        padded = symmetricPad(data, fftSize);
    } else {
        fftSize = 2 * data.size();  // 与 vmdpy 一致
        padded = Vector<double>(fftSize, 0.0);
        for (size_t i = 0; i < data.size(); ++i) padded[i] = data[i];
    }

    size_t padLeft = (fftSize - origN) / 2;

    // 正变换: f̂ = FFT(padded)
    auto fHat = fft::rfft(padded.data(), fftSize);

    size_t K = cfg.K;
    double alpha = cfg.alpha;
    double tau = cfg.tau;
    double tol = cfg.tol;
    int maxIter = cfg.maxIter;

    // 频率轴 (归一化, 0~0.5, 长度 = fftSize/2 + 1)
    size_t freqLen = fftSize / 2 + 1;
    Vector<double> omegaAxis(freqLen);
    for (size_t i = 0; i < freqLen; ++i) {
        omegaAxis[i] = static_cast<double>(i) / static_cast<double>(fftSize);
    }

    // 初始化: û_k = 0, ω_k = 均匀分布(对齐 vmdpy), λ̂ = 0
    Vector<Vector<complex_t>> uHat(K, Vector<complex_t>(freqLen, complex_t(0.0, 0.0)));
    Vector<double> omega(K);
    for (size_t k = 0; k < K; ++k) {
        // 初始中心频率均匀分布,从 0 开始;不包含 0.5 (Nyquist)
        // 对齐 vmdpy: omega_plus[0, i] = (0.5/K) * i
        omega[k] = static_cast<double>(k) / static_cast<double>(K) * 0.5;
    }

    Vector<complex_t> lambdaHat(freqLen, complex_t(0.0, 0.0));

    // Σ_j û_j 与频域残差的工作缓冲: 都放在迭代循环外全程复用,
    // 避免每轮迭代 / 每个 k 重新分配 (residualHat 与函数末尾的时域 residual 区分开)
    Vector<complex_t> uHatTotal(freqLen);
    Vector<complex_t> residualHat(freqLen);

    bool converged = false;
    int iter = 0;
    double eps = 1.0;  // 上一轮的收敛指标 uDiff (初值仅占位: iter==1 直接跳过判据)

    for (iter = 1; iter <= maxIter; ++iter) {
        // 保存上一轮的 û_k,用于收敛检查 (vmdpy uDiff 判据)
        auto uHatOld = uHat;

        // ---- 第 1 步: 更新每个 IMF 的频域表示 û_k ----
        // uHatTotal[i] = Σ_j û_j[i]: 轮首刷新, 之后每更新一个 û_k 增量维护,
        // 于是"除 k 之外之和"退化为一次减法 (等价 vmdpy 的 sum_uk 累加器写法),
        // 省掉原先每个 k 都重算 O(K) 项的 O(K²) 重复求和
        for (size_t i = 0; i < freqLen; ++i) {
            complex_t sum(0.0, 0.0);
            for (size_t k = 0; k < K; ++k) sum += uHat[k][i];
            uHatTotal[i] = sum;
        }

        for (size_t k = 0; k < K; ++k) {
            // 计算残差: f̂ - Σ_{j≠k} û_j - λ̂/2 (对齐 vmdpy / 原 MATLAB 符号)
            for (size_t i = 0; i < freqLen; ++i) {
                residualHat[i] = fHat[i] - (uHatTotal[i] - uHat[k][i]) - lambdaHat[i] * 0.5;
            }

            // 维纳滤波: û_k = residual / [1 + α(ω - ω_k)²]
            // 系数与 vmdpy / 原 MATLAB 参考实现一致: 单边半谱下惩罚项为 α
            // (论文式(15) 的 2α 对应双侧全谱推导, 此处不可再乘 2)
            double wk = omega[k];
            for (size_t i = 0; i < freqLen; ++i) {
                double freqDiff = omegaAxis[i] - wk;
                double denom = 1.0 + alpha * freqDiff * freqDiff;
                uHat[k][i] = residualHat[i] / denom;
            }

            // 增量维护 Σ_j û_j。刻意保持为独立的元素级循环:
            // 既不在其中做归约, 也不并入上面的更新循环 —— 融合写法会打断
            // 编译器对更新循环的自动向量化, 实测慢 2.3 倍
            for (size_t i = 0; i < freqLen; ++i) {
                uHatTotal[i] += uHat[k][i] - uHatOld[k][i];
            }
        }

        // ---- 第 2 步: 更新中心频率 ω_k (功率谱质心) ----
        for (size_t k = 0; k < K; ++k) {
            double num = 0.0, den = 0.0;
            for (size_t i = 0; i < freqLen; ++i) {
                double magSq = std::norm(uHat[k][i]);
                num += omegaAxis[i] * magSq;
                den += magSq;
            }
            if (den > 1e-12) {
                omega[k] = num / den;
            }
        }

        // ---- 第 3 步: 对偶上升 (对齐 vmdpy: λ̂ ← λ̂ + τ·(Σ û_k - f̂)) ----
        if (tau > 0.0) {
            for (size_t i = 0; i < freqLen; ++i) {
                complex_t sum(0.0, 0.0);
                for (size_t k = 0; k < K; ++k) sum += uHat[k][i];
                lambdaHat[i] += tau * (sum - fHat[i]);
            }
        }

        // ---- 第 4 步: 收敛检查 (对齐 vmdpy: uDiff = (1/T)Σ_k ‖û_k - û_k_old‖² < tol) ----
        // 判据取模态频谱变化而非中心频率变化: ω 停滞 ≠ 模态收敛,
        // 低频/直流模态(ω≈0)的幅度分配在 ω 稳定后仍会继续重分配
        // 首轮迭代跳过:上一轮 û 全为 0,与初始化比较无意义
        // (归约独立成环: 若并入第 1 步的更新循环会打断其自动向量化)
        if (iter == 1) continue;

        eps = 0.0;
        for (int k = 0; k < K; ++k) {
            for (size_t i = 0; i < freqLen; ++i) {
                eps += std::norm(uHat[k][i] - uHatOld[k][i]);
            }
        }
        eps /= static_cast<double>(fftSize);

        if (eps < tol) {
            converged = true;
            break;
        }
    }

    // ---- IFFT: 频域 û_k → 时域 IMF ----
    for (int k = 0; k < K; ++k) {
        // 构造完整频谱 (共轭对称, 保证 IFFT 输出实数)
        Vector<complex_t> fullSpectrum(fftSize);
        for (size_t i = 0; i < freqLen; ++i) {
            fullSpectrum[i] = uHat[k][i];
        }
        // 共轭对称填充后半部分
        for (size_t i = 1; i < fftSize / 2; ++i) {
            fullSpectrum[fftSize - i] = std::conj(uHat[k][i]);
        }

        auto imfFull = fft::irfft(fullSpectrum, fftSize);

        // 截取原始长度部分
        auto imf = unpad(imfFull, origN, padLeft);
        result.imfs.push_back(imf);
    }

    // 残差 = 原始信号 - Σ IMF
    Vector<double> residual(origN, 0.0);
    for (size_t i = 0; i < origN; ++i) residual[i] = data[i];
    for (const auto& imf : result.imfs) {
        simd_sub(residual.data(), imf.data(), residual.data(), origN);
    }
    result.residual = residual;

    // 中心频率
    result.centerFreqs.resize(K);
    for (int k = 0; k < K; ++k) result.centerFreqs[k] = omega[k];

    // ---- 按中心频率升序排列 IMF + centerFreqs ----
    // ADMM 迭代后,IMF 顺序可能乱(相近频率 mode 互换),需排序保证
    // imf_info[i].center_freq 单调递增,且 IMF 与 vmdpy 按频率排序后的列表对齐
    Vector<size_t> sortIdx(K);
    for (size_t k = 0; k < K; ++k) sortIdx[k] = k;
    std::sort(sortIdx.begin(), sortIdx.end(),
              [&result](size_t a, size_t b) { return result.centerFreqs[a] < result.centerFreqs[b]; });
    Vector<Vector<double>> imfsSorted(K);
    Vector<double> freqsSorted(K);
    for (size_t k = 0; k < K; ++k) {
        imfsSorted[k] = result.imfs[sortIdx[k]];
        freqsSorted[k] = result.centerFreqs[sortIdx[k]];
    }
    result.imfs = std::move(imfsSorted);
    result.centerFreqs = std::move(freqsSorted);

    result.actualK = K;
    result.iterations = iter;
    result.convergenceError = std::sqrt(eps);
    result.converged = converged;

    _summary = fmt::format("VMD: {} IMFs from {} pts, α={}, τ={}, iter={}, eps={:.2e}{}",
                           K, origN, alpha, tau, iter, result.convergenceError,
                           converged ? " (converged)" : " (max_iter)");

    return result;
}

String VMD::getSummary() const {
    return _summary;
}
