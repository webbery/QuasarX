#pragma once
#include "std_header.h"
#include <cstring>
#include <cmath>

/**
 * EMD_SIMD — SIMD 加速的向量运算内联函数
 *
 * 平台自适应：AVX2 (4路 double) > SSE2 (2路) > 标量回退
 * 用于 EMD/CEEMDAN 中的热点循环：
 *   - 包络插值、均值计算、筛选减法、集合平均
 *
 * 所有函数接受 aligned 或 unaligned 指针，尾部自动标量补齐。
 */

// ======================== 运行时平台检测 ========================

/// SIMD 宽度（以 double 为单位），编译时确定
#if defined(__AVX2__)
    #include <immintrin.h>
    inline constexpr size_t kSimdWidth = 4;
#elif defined(__SSE2__)
    #include <emmintrin.h>
    inline constexpr size_t kSimdWidth = 2;
#else
    inline constexpr size_t kSimdWidth = 1;
#endif

// ======================== 基础向量运算 ========================

/// out[i] = a[i] + b[i]
inline void simd_add(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        _mm256_storeu_pd(out + i, _mm256_add_pd(va, vb));
    }
#elif defined(__SSE2__)
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vb = _mm_loadu_pd(b + i);
        _mm_storeu_pd(out + i, _mm_add_pd(va, vb));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] + b[i];
}

/// out[i] = a[i] - b[i]
inline void simd_sub(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        _mm256_storeu_pd(out + i, _mm256_sub_pd(va, vb));
    }
#elif defined(__SSE2__)
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vb = _mm_loadu_pd(b + i);
        _mm_storeu_pd(out + i, _mm_sub_pd(va, vb));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] - b[i];
}

/// out[i] = a[i] * b[i]
inline void simd_mul(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        _mm256_storeu_pd(out + i, _mm256_mul_pd(va, vb));
    }
#elif defined(__SSE2__)
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vb = _mm_loadu_pd(b + i);
        _mm_storeu_pd(out + i, _mm_mul_pd(va, vb));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] * b[i];
}

/// out[i] = a[i] * scalar
inline void simd_mul_scalar(const double* a, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    __m256d vs = _mm256_set1_pd(scalar);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        _mm256_storeu_pd(out + i, _mm256_mul_pd(va, vs));
    }
#elif defined(__SSE2__)
    __m128d vs = _mm_set1_pd(scalar);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        _mm_storeu_pd(out + i, _mm_mul_pd(va, vs));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] * scalar;
}

/// out[i] = a[i] + b[i] * scalar   (FMA: fused multiply-add)
inline void simd_fma(const double* a, const double* b, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__) && defined(__FMA__)
    __m256d vs = _mm256_set1_pd(scalar);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        _mm256_storeu_pd(out + i, _mm256_fmadd_pd(vb, vs, va));
    }
#elif defined(__AVX2__)
    __m256d vs = _mm256_set1_pd(scalar);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vb = _mm256_loadu_pd(b + i);
        _mm256_storeu_pd(out + i, _mm256_add_pd(va, _mm256_mul_pd(vb, vs)));
    }
#elif defined(__SSE2__)
    __m128d vs = _mm_set1_pd(scalar);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vb = _mm_loadu_pd(b + i);
        _mm_storeu_pd(out + i, _mm_add_pd(va, _mm_mul_pd(vb, vs)));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] + b[i] * scalar;
}

// ======================== EMD 专用热点函数 ========================

/// 均值包络: out[i] = (upper[i] + lower[i]) * 0.5
inline void simd_mean_envelope(const double* upper, const double* lower, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    __m256d half = _mm256_set1_pd(0.5);
    for (; i + 3 < n; i += 4) {
        __m256d u = _mm256_loadu_pd(upper + i);
        __m256d l = _mm256_loadu_pd(lower + i);
        _mm256_storeu_pd(out + i, _mm256_mul_pd(_mm256_add_pd(u, l), half));
    }
#elif defined(__SSE2__)
    __m128d half = _mm_set1_pd(0.5);
    for (; i + 1 < n; i += 2) {
        __m128d u = _mm_loadu_pd(upper + i);
        __m128d l = _mm_loadu_pd(lower + i);
        _mm_storeu_pd(out + i, _mm_mul_pd(_mm_add_pd(u, l), half));
    }
#endif
    for (; i < n; ++i) out[i] = (upper[i] + lower[i]) * 0.5;
}

/// 筛选减法: h[i] -= meanEnv[i]  (就地)
inline void simd_sifting_subtract(double* h, const double* meanEnv, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 3 < n; i += 4) {
        __m256d vh = _mm256_loadu_pd(h + i);
        __m256d vm = _mm256_loadu_pd(meanEnv + i);
        _mm256_storeu_pd(h + i, _mm256_sub_pd(vh, vm));
    }
#elif defined(__SSE2__)
    for (; i + 1 < n; i += 2) {
        __m128d vh = _mm_loadu_pd(h + i);
        __m128d vm = _mm_loadu_pd(meanEnv + i);
        _mm_storeu_pd(h + i, _mm_sub_pd(vh, vm));
    }
#endif
    for (; i < n; ++i) h[i] -= meanEnv[i];
}

/// 线性插值: out[j] = v0 + (j-i0)/(i1-i0) * (v1 - v0),  j ∈ [i0, i1]
/// 用于包络线生成（替代原 cubicSplineEnvelope 中的线性插值）
inline void simd_linear_interp(int i0, int i1, double v0, double v1, double* out) {
    double range = static_cast<double>(i1 - i0);
    if (range < 1e-12) {
        // 极值点重合，直接填充
        for (int j = i0; j <= i1; ++j) out[j] = v0;
        return;
    }
    double invRange = 1.0 / range;
    double dv = v1 - v0;

    size_t i = static_cast<size_t>(i0);
    size_t end = static_cast<size_t>(i1);

#if defined(__AVX2__)
    __m256d vv0 = _mm256_set1_pd(v0);
    __m256d vdv = _mm256_set1_pd(dv);
    __m256d vInv = _mm256_set1_pd(invRange);
    // 对齐到 4 的倍数
    size_t aligned = (i + 3) & ~size_t(3);
    if (aligned <= end) {
        for (; i < aligned; ++i) {
            double t = static_cast<double>(static_cast<int>(i) - i0) * invRange;
            out[i] = v0 + t * dv;
        }
        for (; i + 3 <= end; i += 4) {
            __m256d idx = _mm256_set_pd(
                static_cast<double>(static_cast<int>(i + 3) - i0),
                static_cast<double>(static_cast<int>(i + 2) - i0),
                static_cast<double>(static_cast<int>(i + 1) - i0),
                static_cast<double>(static_cast<int>(i) - i0));
            __m256d t = _mm256_mul_pd(idx, vInv);
            __m256d result = _mm256_fmadd_pd(t, vdv, vv0);
            _mm256_storeu_pd(out + i, result);
        }
    }
#elif defined(__SSE2__)
    __m128d vv0 = _mm_set1_pd(v0);
    __m128d vdv = _mm_set1_pd(dv);
    __m128d vInv = _mm_set1_pd(invRange);
    size_t aligned = (i + 1) & ~size_t(1);
    if (aligned <= end) {
        for (; i < aligned; ++i) {
            double t = static_cast<double>(static_cast<int>(i) - i0) * invRange;
            out[i] = v0 + t * dv;
        }
        for (; i + 1 <= end; i += 2) {
            __m128d idx = _mm_set_pd(
                static_cast<double>(static_cast<int>(i + 1) - i0),
                static_cast<double>(static_cast<int>(i) - i0));
            __m128d t = _mm_mul_pd(idx, vInv);
            __m128d result = _mm_add_pd(vv0, _mm_mul_pd(t, vdv));
            _mm_storeu_pd(out + i, result);
        }
    }
#endif
    for (; i <= end; ++i) {
        double t = static_cast<double>(static_cast<int>(i) - i0) * invRange;
        out[i] = v0 + t * dv;
    }
}

/// 集合平均: out[i] = Σ buffers[j][i] / N
inline void simd_ensemble_average(const Vector<Vector<double>>& buffers,
                                   double* out, size_t n) {
    if (buffers.empty()) return;
    size_t N = buffers.size();
    double invN = 1.0 / static_cast<double>(N);

    size_t i = 0;
#if defined(__AVX2__)
    __m256d vInv = _mm256_set1_pd(invN);
    for (; i + 3 < n; i += 4) {
        __m256d acc = _mm256_setzero_pd();
        for (const auto& buf : buffers) {
            acc = _mm256_add_pd(acc, _mm256_loadu_pd(buf.data() + i));
        }
        _mm256_storeu_pd(out + i, _mm256_mul_pd(acc, vInv));
    }
#elif defined(__SSE2__)
    __m128d vInv = _mm_set1_pd(invN);
    for (; i + 1 < n; i += 2) {
        __m128d acc = _mm_setzero_pd();
        for (const auto& buf : buffers) {
            acc = _mm_add_pd(acc, _mm_loadu_pd(buf.data() + i));
        }
        _mm_storeu_pd(out + i, _mm_mul_pd(acc, vInv));
    }
#endif
    for (; i < n; ++i) {
        double sum = 0.0;
        for (const auto& buf : buffers) sum += buf[i];
        out[i] = sum * invN;
    }
}

/// 绝对值最大值: max_i |a[i]|
inline double simd_abs_max(const double* a, size_t n) {
    double maxVal = 0.0;
    size_t i = 0;
#if defined(__AVX2__)
    __m256d vmax = _mm256_setzero_pd();
    __m256d signMask = _mm256_set1_pd(-0.0);  // 符号位掩码
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vabs = _mm256_andnot_pd(signMask, va);  // 清除符号位 = abs
        vmax = _mm256_max_pd(vmax, vabs);
    }
    // 水平归约
    alignas(32) double buf[4];
    _mm256_store_pd(buf, vmax);
    for (double v : buf) if (v > maxVal) maxVal = v;
#elif defined(__SSE2__)
    __m128d vmax = _mm_setzero_pd();
    __m128d signMask = _mm_set1_pd(-0.0);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vabs = _mm_andnot_pd(signMask, va);
        vmax = _mm_max_pd(vmax, vabs);
    }
    alignas(16) double buf[2];
    _mm_store_pd(buf, vmax);
    for (double v : buf) if (v > maxVal) maxVal = v;
#endif
    for (; i < n; ++i) {
        double absV = std::abs(a[i]);
        if (absV > maxVal) maxVal = absV;
    }
    return maxVal;
}

/// 信号范围: max - min
inline double simd_range(const double* a, size_t n) {
    if (n == 0) return 0.0;
    double minVal = a[0], maxVal = a[0];
    size_t i = 1;
#if defined(__AVX2__)
    __m256d vmin = _mm256_set1_pd(a[0]);
    __m256d vmax = _mm256_set1_pd(a[0]);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        vmin = _mm256_min_pd(vmin, va);
        vmax = _mm256_max_pd(vmax, va);
    }
    alignas(32) double bufMin[4], bufMax[4];
    _mm256_store_pd(bufMin, vmin);
    _mm256_store_pd(bufMax, vmax);
    for (int j = 0; j < 4; ++j) {
        if (bufMin[j] < minVal) minVal = bufMin[j];
        if (bufMax[j] > maxVal) maxVal = bufMax[j];
    }
#elif defined(__SSE2__)
    __m128d vmin = _mm_set1_pd(a[0]);
    __m128d vmax = _mm_set1_pd(a[0]);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        vmin = _mm_min_pd(vmin, va);
        vmax = _mm_max_pd(vmax, va);
    }
    alignas(16) double bufMin[2], bufMax[2];
    _mm_store_pd(bufMin, vmin);
    _mm_store_pd(bufMax, vmax);
    for (int j = 0; j < 2; ++j) {
        if (bufMin[j] < minVal) minVal = bufMin[j];
        if (bufMax[j] > maxVal) maxVal = bufMax[j];
    }
#endif
    for (; i < n; ++i) {
        if (a[i] < minVal) minVal = a[i];
        if (a[i] > maxVal) maxVal = a[i];
    }
    return maxVal - minVal;
}

/// 向量加标量: out[i] = a[i] + scalar
inline void simd_add_scalar(const double* a, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    __m256d vs = _mm256_set1_pd(scalar);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        _mm256_storeu_pd(out + i, _mm256_add_pd(va, vs));
    }
#elif defined(__SSE2__)
    __m128d vs = _mm_set1_pd(scalar);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        _mm_storeu_pd(out + i, _mm_add_pd(va, vs));
    }
#endif
    for (; i < n; ++i) out[i] = a[i] + scalar;
}

/// 就地加标量: a[i] += scalar
inline void simd_add_scalar_inplace(double* a, double scalar, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    __m256d vs = _mm256_set1_pd(scalar);
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        _mm256_storeu_pd(a + i, _mm256_add_pd(va, vs));
    }
#elif defined(__SSE2__)
    __m128d vs = _mm_set1_pd(scalar);
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        _mm_storeu_pd(a + i, _mm_add_pd(va, vs));
    }
#endif
    for (; i < n; ++i) a[i] += scalar;
}

/// 向量累加到目标: acc[i] += a[i]
inline void simd_accumulate(const double* a, double* acc, size_t n) {
    size_t i = 0;
#if defined(__AVX2__)
    for (; i + 3 < n; i += 4) {
        __m256d va = _mm256_loadu_pd(a + i);
        __m256d vacc = _mm256_loadu_pd(acc + i);
        _mm256_storeu_pd(acc + i, _mm256_add_pd(vacc, va));
    }
#elif defined(__SSE2__)
    for (; i + 1 < n; i += 2) {
        __m128d va = _mm_loadu_pd(a + i);
        __m128d vacc = _mm_loadu_pd(acc + i);
        _mm_storeu_pd(acc + i, _mm_add_pd(vacc, va));
    }
#endif
    for (; i < n; ++i) acc[i] += a[i];
}

// ======================== EMD 完整流程 ========================

/// SIMD 加速的 EMD 分解
List<Vector<double>> simd_emd(const Vector<double>& data,
                                int numIMFs,
                                int maxSiftingIter = 10,
                                double sdThreshold = 0.02);
