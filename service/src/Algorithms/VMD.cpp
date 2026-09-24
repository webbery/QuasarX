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
// VMDState 初始化
// ============================================================================

void VMD::VMDState::init(int K, size_t freqLen) {
    _uHat_re.assign(K, Vector<double>(freqLen, 0.0));
    _uHat_im.assign(K, Vector<double>(freqLen, 0.0));
    _uHatTotal_re.assign(freqLen, 0.0);
    _uHatTotal_im.assign(freqLen, 0.0);
    _fHat_re.assign(freqLen, 0.0);
    _fHat_im.assign(freqLen, 0.0);
    _lambdaHat_re.assign(freqLen, 0.0);
    _lambdaHat_im.assign(freqLen, 0.0);
    _omegaAxis.assign(freqLen, 0.0);
    _epsAcc.assign(freqLen, 0.0);
    _omega.assign(K, 0.0);
}

// ============================================================================
// 融合内层核: 残差 → 维纳滤波 → Σû 增量维护 → 收敛量累积
// ============================================================================

/**
 * @brief 单次遍历完成原先 4 个独立循环的工作
 *
 * 原实现每个 mode 需要 4 趟独立遍历:
 *   ① residualHat[i] = f̂ - (Σ_{j≠k}û_j) - λ̂/2
 *   ② û_k[i] = residualHat[i] * (1/(1+α(ω_i-ω_k)²))
 *   ③ Σû[i] += û_k[i] - û_k_old[i]
 *   ④ eps += |û_k[i] - û_k_old[i]|²      (另加一趟 K×freqLen 的 uHatOld 拷贝)
 * 4 趟的总线流量是 (load 13 + store 6) 次/元素, 且 residualHat 的写回/重读
 * 与 uHatOld 的快照拷贝占了大头 —— 实测瓶颈是收发端口吞吐而非浮点算力。
 *
 * 融合后单趟完成: 旧值只在寄存器里出现一次, 残差不落地, 收敛量就地累积到
 * epsAcc[i] (逐元素内存累加器, 无归约依赖 → 可向量化), 总线流量降到
 * (load 10 + store 5) 次/元素, 且不再需要 uHatOld 快照。
 *
 * 数学上与分趟实现等价 (逐元素运算顺序不变, 仅 eps 的求和各元素间交换了次序)。
 */
static void vmdUpdateMode(double* ukRe, double* ukIm,
                          const double* fRe, const double* fIm,
                          double* totRe, double* totIm,
                          const double* lamRe, const double* lamIm,
                          const double* omegaAxis, double* epsAcc,
                          double wk, double alpha, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    const __m512d vhalf = _mm512_set1_pd(0.5);
    const __m512d vone = _mm512_set1_pd(1.0);
    const __m512d valpha = _mm512_set1_pd(alpha);
    const __m512d vwk = _mm512_set1_pd(wk);
    for (; i + 7 < n; i += 8) {
        const __m512d vf_re = _mm512_loadu_pd(fRe + i);
        const __m512d vf_im = _mm512_loadu_pd(fIm + i);
        const __m512d vtot_re = _mm512_loadu_pd(totRe + i);
        const __m512d vtot_im = _mm512_loadu_pd(totIm + i);
        const __m512d vlam_re = _mm512_loadu_pd(lamRe + i);
        const __m512d vlam_im = _mm512_loadu_pd(lamIm + i);
        const __m512d vold_re = _mm512_loadu_pd(ukRe + i);
        const __m512d vold_im = _mm512_loadu_pd(ukIm + i);

        // 残差: f̂ - (Σ_j û_j - û_k) - λ̂/2
        const __m512d vres_re = _mm512_fnmadd_pd(vlam_re, vhalf,
            _mm512_sub_pd(vf_re, _mm512_sub_pd(vtot_re, vold_re)));
        const __m512d vres_im = _mm512_fnmadd_pd(vlam_im, vhalf,
            _mm512_sub_pd(vf_im, _mm512_sub_pd(vtot_im, vold_im)));

        // 维纳滤波系数倒数: 1/[1 + α(ω_i - ω_k)²] —— 一次除法供实/虚两路复用
        const __m512d vdw = _mm512_sub_pd(_mm512_loadu_pd(omegaAxis + i), vwk);
        const __m512d vdenom = _mm512_fmadd_pd(valpha, _mm512_mul_pd(vdw, vdw), vone);
        const __m512d vinv = _mm512_div_pd(vone, vdenom);
        const __m512d vnew_re = _mm512_mul_pd(vres_re, vinv);
        const __m512d vnew_im = _mm512_mul_pd(vres_im, vinv);

        // 增量维护 Σû 与收敛量累积（旧值来自寄存器, 无需快照平面）
        const __m512d vd_re = _mm512_sub_pd(vnew_re, vold_re);
        const __m512d vd_im = _mm512_sub_pd(vnew_im, vold_im);
        _mm512_storeu_pd(ukRe + i, vnew_re);
        _mm512_storeu_pd(ukIm + i, vnew_im);
        _mm512_storeu_pd(totRe + i, _mm512_add_pd(vtot_re, vd_re));
        _mm512_storeu_pd(totIm + i, _mm512_add_pd(vtot_im, vd_im));
        __m512d vacc = _mm512_fmadd_pd(vd_re, vd_re, _mm512_loadu_pd(epsAcc + i));
        _mm512_storeu_pd(epsAcc + i, _mm512_fmadd_pd(vd_im, vd_im, vacc));
    }
#elif defined(__AVX2__)
    const __m256d vhalf = _mm256_set1_pd(0.5);
    const __m256d vone = _mm256_set1_pd(1.0);
    const __m256d valpha = _mm256_set1_pd(alpha);
    const __m256d vwk = _mm256_set1_pd(wk);
    for (; i + 3 < n; i += 4) {
        const __m256d vf_re = _mm256_loadu_pd(fRe + i);
        const __m256d vf_im = _mm256_loadu_pd(fIm + i);
        const __m256d vtot_re = _mm256_loadu_pd(totRe + i);
        const __m256d vtot_im = _mm256_loadu_pd(totIm + i);
        const __m256d vlam_re = _mm256_loadu_pd(lamRe + i);
        const __m256d vlam_im = _mm256_loadu_pd(lamIm + i);
        const __m256d vold_re = _mm256_loadu_pd(ukRe + i);
        const __m256d vold_im = _mm256_loadu_pd(ukIm + i);

        const __m256d vres_re = _mm256_sub_pd(_mm256_sub_pd(vf_re, _mm256_sub_pd(vtot_re, vold_re)),
                                              _mm256_mul_pd(vlam_re, vhalf));
        const __m256d vres_im = _mm256_sub_pd(_mm256_sub_pd(vf_im, _mm256_sub_pd(vtot_im, vold_im)),
                                              _mm256_mul_pd(vlam_im, vhalf));

        const __m256d vdw = _mm256_sub_pd(_mm256_loadu_pd(omegaAxis + i), vwk);
        const __m256d vdenom = _mm256_fmadd_pd(valpha, _mm256_mul_pd(vdw, vdw), vone);
        const __m256d vinv = _mm256_div_pd(vone, vdenom);
        const __m256d vnew_re = _mm256_mul_pd(vres_re, vinv);
        const __m256d vnew_im = _mm256_mul_pd(vres_im, vinv);

        const __m256d vd_re = _mm256_sub_pd(vnew_re, vold_re);
        const __m256d vd_im = _mm256_sub_pd(vnew_im, vold_im);
        _mm256_storeu_pd(ukRe + i, vnew_re);
        _mm256_storeu_pd(ukIm + i, vnew_im);
        _mm256_storeu_pd(totRe + i, _mm256_add_pd(vtot_re, vd_re));
        _mm256_storeu_pd(totIm + i, _mm256_add_pd(vtot_im, vd_im));
        __m256d vacc = _mm256_fmadd_pd(vd_re, vd_re, _mm256_loadu_pd(epsAcc + i));
        _mm256_storeu_pd(epsAcc + i, _mm256_fmadd_pd(vd_im, vd_im, vacc));
    }
#endif
    for (; i < n; ++i) {
        const double oldRe = ukRe[i];
        const double oldIm = ukIm[i];
        const double resRe = fRe[i] - (totRe[i] - oldRe) - lamRe[i] * 0.5;
        const double resIm = fIm[i] - (totIm[i] - oldIm) - lamIm[i] * 0.5;
        const double dw = omegaAxis[i] - wk;
        const double inv = 1.0 / (1.0 + alpha * dw * dw);
        const double newRe = resRe * inv;
        const double newIm = resIm * inv;
        const double dRe = newRe - oldRe;
        const double dIm = newIm - oldIm;
        ukRe[i] = newRe;
        ukIm[i] = newIm;
        totRe[i] += dRe;
        totIm[i] += dIm;
        epsAcc[i] += dRe * dRe + dIm * dIm;
    }
}

// ============================================================================
// SoA 布局 + 循环融合的 VMD 核心迭代
// ============================================================================

VMD::IterStats VMD::decomposeSoA(VMDState& state, const Config& cfg, size_t fftSize) {
    const size_t K = cfg.K;
    const double alpha = cfg.alpha;
    const double tau = cfg.tau;
    const double tol = cfg.tol;
    const int maxIter = cfg.maxIter;
    const size_t freqLen = fftSize / 2 + 1;

    IterStats stats;

    for (int iter = 1; iter <= maxIter; ++iter) {
        stats._iterations = iter;

        // ---- 第 1 步: 轮首刷新 Σ_j û_j (k=0 赋值, 其余累加) ----
        // 按 k 分趟、每趟连续访存: 无内层归约 → 整趟可向量化。
        // 求和次序与"逐 i 内层累加"完全一致 (u_0 + u_1 + ...), 数值等价。
        std::copy(state._uHat_re[0].begin(), state._uHat_re[0].end(),
                  state._uHatTotal_re.begin());
        std::copy(state._uHat_im[0].begin(), state._uHat_im[0].end(),
                  state._uHatTotal_im.begin());
        for (size_t k = 1; k < K; ++k) {
            const double* ur = state._uHat_re[k].data();
            const double* ui = state._uHat_im[k].data();
            double* tr = state._uHatTotal_re.data();
            double* ti = state._uHatTotal_im.data();
            for (size_t i = 0; i < freqLen; ++i) {
                tr[i] += ur[i];
                ti[i] += ui[i];
            }
        }

        // ---- 第 2 步: 逐个 mode 更新 (融合内层核) ----
        if (iter > 1) {
            std::fill(state._epsAcc.begin(), state._epsAcc.end(), 0.0);
        }
        for (size_t k = 0; k < K; ++k) {
            vmdUpdateMode(state._uHat_re[k].data(), state._uHat_im[k].data(),
                          state._fHat_re.data(), state._fHat_im.data(),
                          state._uHatTotal_re.data(), state._uHatTotal_im.data(),
                          state._lambdaHat_re.data(), state._lambdaHat_im.data(),
                          state._omegaAxis.data(), state._epsAcc.data(),
                          state._omega[k], alpha, freqLen);
        }

        // ---- 第 3 步: 更新中心频率 ω_k (功率谱质心) ----
        for (size_t k = 0; k < K; ++k) {
            auto [num, den] = simd_reduce_weighted_mag_sq(
                state._uHat_re[k].data(), state._uHat_im[k].data(),
                state._omegaAxis.data(), freqLen);
            if (den > 1e-12) {
                state._omega[k] = num / den;
            }
        }

        // ---- 第 4 步: 对偶上升 (对齐 vmdpy: λ̂ ← λ̂ + τ·(Σ û_k - f̂)) ----
        // 此处按原实现重新求和, 不用增量维护的 uHatTotal:
        // 增量累加在多轮迭代后会累积与"重新求和"不同的舍入尾差 (tau>0 时该差值
        // 会经 λ̂ 反馈放大), 保持重算可让本路径与优化前逐位一致。
        if (tau > 0.0) {
            double* lr = state._lambdaHat_re.data();
            double* li = state._lambdaHat_im.data();
            const double* fr = state._fHat_re.data();
            const double* fi = state._fHat_im.data();
            for (size_t i = 0; i < freqLen; ++i) {
                double sumRe = 0.0, sumIm = 0.0;
                for (size_t k = 0; k < K; ++k) {
                    sumRe += state._uHat_re[k][i];
                    sumIm += state._uHat_im[k][i];
                }
                lr[i] += tau * (sumRe - fr[i]);
                li[i] += tau * (sumIm - fi[i]);
            }
        }

        // ---- 第 5 步: 收敛检查 (对齐 vmdpy: uDiff = (1/T)Σ_k ‖û_k - û_k_old‖² < tol) ----
        // 判据取模态频谱变化而非中心频率变化: ω 停滞 ≠ 模态收敛,
        // 低频/直流模态(ω≈0)的幅度分配在 ω 稳定后仍会继续重分配。
        // 首轮跳过: 上一轮 û 全为 0, 与初始化比较无意义。
        if (iter == 1) continue;

        stats._eps = simd_sum(state._epsAcc.data(), freqLen) / static_cast<double>(fftSize);
        if (stats._eps < tol) {
            stats._converged = true;
            break;
        }
    }

    return stats;
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
    size_t freqLen = fftSize / 2 + 1;

    // 初始化 SoA 状态
    VMDState state;
    state.init(static_cast<int>(K), freqLen);

    // 将 fHat 从 complex_t 拆分为实部/虚部
    for (size_t i = 0; i < freqLen; ++i) {
        state._fHat_re[i] = fHat[i].real();
        state._fHat_im[i] = fHat[i].imag();
    }

    // 初始化中心频率: 均匀分布
    for (size_t k = 0; k < K; ++k) {
        state._omega[k] = static_cast<double>(k) / static_cast<double>(K) * 0.5;
    }

    // 初始化频率轴
    for (size_t i = 0; i < freqLen; ++i) {
        state._omegaAxis[i] = static_cast<double>(i) / static_cast<double>(fftSize);
    }

    // 执行 SoA 布局 + 循环融合的核心迭代
    IterStats stats = decomposeSoA(state, cfg, fftSize);

    // ---- IFFT: 频域 û_k → 时域 IMF ----
    for (size_t k = 0; k < K; ++k) {
        // 将 SoA 合并回 Vector<complex_t>
        Vector<complex_t> halfSpectrum(freqLen);
        for (size_t i = 0; i < freqLen; ++i) {
            halfSpectrum[i] = complex_t(state._uHat_re[k][i], state._uHat_im[k][i]);
        }

        // 构造完整频谱 (共轭对称, 保证 IFFT 输出实数)
        Vector<complex_t> fullSpectrum(fftSize);
        for (size_t i = 0; i < freqLen; ++i) {
            fullSpectrum[i] = halfSpectrum[i];
        }
        // 共轭对称填充后半部分
        for (size_t i = 1; i < fftSize / 2; ++i) {
            fullSpectrum[fftSize - i] = std::conj(halfSpectrum[i]);
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
    result.centerFreqs = state._omega;

    // ---- 按中心频率升序排列 IMF + centerFreqs ----
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
    result.iterations = stats._iterations;
    result.convergenceError = std::sqrt(stats._eps);
    result.converged = stats._converged;

    _summary = fmt::format("VMD: {} IMFs from {} pts, α={}, τ={}, iter={}, eps={:.2e}{}",
                           K, origN, cfg.alpha, cfg.tau, stats._iterations,
                           result.convergenceError,
                           stats._converged ? " (converged)" : " (max_iter)");

    return result;
}

String VMD::getSummary() const {
    return _summary;
}
