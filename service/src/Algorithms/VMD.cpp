#include "Algorithms/VMD.h"
#include "Algorithms/FFT.h"
#include "Algorithms/EMD_SIMD.h"
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

    // 计算需要的填充长度：扩展到 2 的幂
    int bits = 0;
    size_t fftSize = 1;
    // 目标长度至少为 2*n (保证足够的分辨率)
    while (fftSize < 2 * n) {
        fftSize <<= 1;
        bits++;
    }

    outSize = fftSize;

    // 计算左右填充长度
    size_t totalPad = fftSize - n;
    size_t padLeft = totalPad / 2;
    size_t padRight = totalPad - padLeft;

    Vector<double> padded(fftSize);

    // 左端对称延拓 (镜像)
    for (size_t i = 0; i < padLeft; ++i) {
        size_t srcIdx = (i < n) ? (padLeft - i) : 0;
        padded[i] = data[srcIdx];
    }

    // 原始数据
    for (size_t i = 0; i < n; ++i) {
        padded[padLeft + i] = data[i];
    }

    // 右端对称延拓 (镜像)
    for (size_t i = 0; i < padRight; ++i) {
        size_t srcIdx = (n - 2 - i >= 0) ? (n - 2 - i) : (n - 1);
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

    // 对称延拓到 2 的幂
    Vector<double> padded;
    if (cfg.symmetricPad) {
        padded = symmetricPad(data, fftSize);
    } else {
        int bits = 0;
        fftSize = 1;
        while (fftSize < data.size()) { fftSize <<= 1; bits++; }
        padded = Vector<double>(fftSize, 0.0);
        for (size_t i = 0; i < data.size(); ++i) padded[i] = data[i];
    }

    size_t padLeft = (fftSize - origN) / 2;

    // 正变换: f̂ = FFT(padded)
    auto fHat = fft::rfft(padded.data(), fftSize);

    int K = cfg.K;
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

    // 初始化: û_k = 0, ω_k = 均匀分布, λ̂ = 0
    Vector<Vector<complex_t>> uHat(K, Vector<complex_t>(freqLen, complex_t(0.0, 0.0)));
    Vector<double> omega(K);
    for (int k = 0; k < K; ++k) {
        // 初始中心频率均匀分布
        omega[k] = (static_cast<double>(k) + 1.0) / static_cast<double>(K) * 0.5;
    }

    Vector<complex_t> lambdaHat(freqLen, complex_t(0.0, 0.0));

    bool converged = false;
    int iter = 0;
    double eps = 0.0;

    for (iter = 1; iter <= maxIter; ++iter) {
        // 保存旧的 û_k 用于收敛检查
        auto uHatOld = uHat;

        // ---- 第 1 步: 更新每个 IMF 的频域表示 û_k ----
        for (int k = 0; k < K; ++k) {
            // 计算残差: f̂ - Σ_{i≠k} û_i + λ̂/2
            Vector<complex_t> residual(freqLen);
            for (size_t i = 0; i < freqLen; ++i) {
                complex_t sum(0.0, 0.0);
                for (int j = 0; j < K; ++j) {
                    if (j == k) continue;
                    sum += uHat[j][i];
                }
                residual[i] = fHat[i] - sum + lambdaHat[i] * 0.5;
            }

            // 维纳滤波: û_k = residual / [1 + 2α(ω - ω_k)²]
            double wk = omega[k];
            double twoAlpha = 2.0 * alpha;
            for (size_t i = 0; i < freqLen; ++i) {
                double freqDiff = omegaAxis[i] - wk;
                double denom = 1.0 + twoAlpha * freqDiff * freqDiff;
                uHat[k][i] = residual[i] / denom;
            }
        }

        // ---- 第 2 步: 更新中心频率 ω_k (功率谱质心) ----
        for (int k = 0; k < K; ++k) {
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

        // ---- 第 3 步: 对偶上升 λ̂ ← λ̂ + τ·(f̂ - Σ û_k) ----
        if (tau > 0.0) {
            for (size_t i = 0; i < freqLen; ++i) {
                complex_t sum(0.0, 0.0);
                for (int k = 0; k < K; ++k) sum += uHat[k][i];
                lambdaHat[i] += tau * (fHat[i] - sum);
            }
        }

        // ---- 第 4 步: 收敛检查 ----
        eps = 0.0;
        for (int k = 0; k < K; ++k) {
            double numSq = 0.0, denSq = 0.0;
            for (size_t i = 0; i < freqLen; ++i) {
                double diff = std::norm(uHat[k][i] - uHatOld[k][i]);
                numSq += diff;
                denSq += std::norm(uHatOld[k][i]);
            }
            if (denSq > 1e-12) {
                eps += numSq / denSq;
            }
        }

        if (eps < tol * tol) {
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
