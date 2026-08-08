#pragma once
#include "std_header.h"
#include <cstring>
#include <cmath>

// ── 跨平台 SIMD 函数属性宏 ──
// GCC/Clang: __attribute__((target("avx2,fma"))) 允许单个函数使用 AVX2 intrinsics
//            即使整个 TU 没有 -mavx2 标志（避免 CI 机器 SIGILL）
// MSVC:    不需要（/arch:AVX2 是 per-file 设置的）
#if defined(__GNUC__) || defined(__clang__)
    #define QUASARX_SIMD_TARGET __attribute__((target("avx2,fma")))
#else
    #define QUASARX_SIMD_TARGET
#endif

/**
 * EMD_SIMD — SIMD 加速的向量运算内联函数
 *
 * 平台自适应：AVX-512 (8路 double) > AVX2 (4路) > SSE2 (2路) > 标量回退
 * 用于 EMD/CEEMDAN 中的热点循环：
 *   - 包络插值、均值计算、筛选减法、集合平均
 *
 * 所有函数接受 aligned 或 unaligned 指针，尾部自动标量补齐。
 */

// ======================== 运行时平台检测 ========================

/// SIMD 宽度（以 double 为单位），编译时确定
#if defined(__AVX512F__)
    #include <immintrin.h>
    inline constexpr size_t kSimdWidth = 8;
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_add(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vb = _mm512_loadu_pd(b + i);
        _mm512_storeu_pd(out + i, _mm512_add_pd(va, vb));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_sub(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vb = _mm512_loadu_pd(b + i);
        _mm512_storeu_pd(out + i, _mm512_sub_pd(va, vb));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_mul(const double* a, const double* b, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vb = _mm512_loadu_pd(b + i);
        _mm512_storeu_pd(out + i, _mm512_mul_pd(va, vb));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_mul_scalar(const double* a, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    __m512d vs = _mm512_set1_pd(scalar);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        _mm512_storeu_pd(out + i, _mm512_mul_pd(va, vs));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_fma(const double* a, const double* b, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__) && defined(__AVX512DQ__)
    // AVX-512 FMA (FMA3 is always available with AVX-512F on x86-64)
    __m512d vs = _mm512_set1_pd(scalar);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vb = _mm512_loadu_pd(b + i);
        _mm512_storeu_pd(out + i, _mm512_fmadd_pd(vb, vs, va));
    }
#elif defined(__AVX512F__)
    __m512d vs = _mm512_set1_pd(scalar);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vb = _mm512_loadu_pd(b + i);
        _mm512_storeu_pd(out + i, _mm512_add_pd(va, _mm512_mul_pd(vb, vs)));
    }
#elif defined(__AVX2__) && defined(__FMA__)
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
QUASARX_SIMD_TARGET
inline void simd_mean_envelope(const double* upper, const double* lower, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    __m512d half = _mm512_set1_pd(0.5);
    for (; i + 7 < n; i += 8) {
        __m512d u = _mm512_loadu_pd(upper + i);
        __m512d l = _mm512_loadu_pd(lower + i);
        _mm512_storeu_pd(out + i, _mm512_mul_pd(_mm512_add_pd(u, l), half));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_sifting_subtract(double* h, const double* meanEnv, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    for (; i + 7 < n; i += 8) {
        __m512d vh = _mm512_loadu_pd(h + i);
        __m512d vm = _mm512_loadu_pd(meanEnv + i);
        _mm512_storeu_pd(h + i, _mm512_sub_pd(vh, vm));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
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

/**
 * @brief 三次样条插值（自然边界条件）
 *
 * 对极值点序列 (x[i], y[i]) 构造自然三次样条，在区间 [x0, x1] 内插值。
 * 自然边界：端点二阶导数为零（与 pyEMD 默认行为一致）。
 *
 * @param x     极值点索引数组（单调递增）
 * @param y     极值点值数组
 * @param nPts  极值点数量
 * @param out   输出插值结果（大小由调用方保证）
 * @param outLo 插值起始索引（含）
 * @param outHi 插值结束索引（含）
 */
inline void natural_cubic_spline(const int* x, const double* y, size_t nPts,
                                  double* out, int outLo, int outHi) {
    if (nPts < 2) {
        double val = (nPts == 1) ? y[0] : 0.0;
        for (int j = outLo; j <= outHi; ++j) out[j] = val;
        return;
    }

    // 步骤 1: 计算步长 h[i] = x[i+1] - x[i]
    int n = static_cast<int>(nPts) - 1;  // 区间数
    Vector<double> h(n);
    for (int i = 0; i < n; ++i) {
        h[i] = static_cast<double>(x[i + 1] - x[i]);
    }

    // 步骤 2: 求解三对角方程组得到二阶导数 M[i]
    // 自然边界: M[0] = M[n] = 0
    Vector<double> alpha(n);    // 中间变量
    Vector<double> l(n + 1);    // 下对角
    Vector<double> mu(n + 1);   // 上对角
    Vector<double> z(n + 1);    // 辅助变量
    Vector<double> M(n + 1);    // 二阶导数

    l[0] = 1.0; mu[0] = 0.0; z[0] = 0.0;

    for (int i = 1; i < n; ++i) {
        alpha[i] = (3.0 / h[i]) * (y[i + 1] - y[i]) -
                    (3.0 / h[i - 1]) * (y[i] - y[i - 1]);
    }

    for (int i = 1; i < n; ++i) {
        l[i] = 2.0 * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
        if (std::abs(l[i]) < 1e-15) l[i] = 1e-15;  // 防止除零
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }

    l[n] = 1.0; z[n] = 0.0;
    M[n] = 0.0;  // 自然边界

    for (int j = n - 1; j >= 0; --j) {
        M[j] = z[j] - mu[j] * M[j + 1];
    }

    // 步骤 3: 对每个区间 [x[i], x[i+1]] 执行样条插值
    for (int i = 0; i < n; ++i) {
        int j0 = std::max(outLo, x[i]);
        int j1 = std::min(outHi, x[i + 1]);

        if (j0 > j1) continue;

        double hi = h[i];
        double hiInv = 1.0 / hi;
        double yi = y[i];
        double yi1 = y[i + 1];
        double Mi = M[i];
        double Mi1 = M[i + 1];

        // 标量循环（区间通常较短，SIMD 收益有限）
        for (int j = j0; j <= j1; ++j) {
            double t = static_cast<double>(j - x[i]) * hiInv;  // t ∈ [0, 1]
            double t2 = t * t;
            double t3 = t2 * t;

            // 三次样条公式（Hermite 形式）:
            // S(t) = (1-t)^3*y[i] + 3*(1-t)^2*t*y[i+1]
            //      + (1-t)^3*hi^2*M[i]/6 + t*(1-t)*(2-t)*hi^2*M[i]/6
            //      + t^3*hi^2*M[i+1]/6 - t^2*(1-t)*hi^2*M[i+1]/6
            // 简化为:
            // S(t) = (1-t)*y[i] + t*y[i+1]
            //      + hi^2/6 * [(1-t)^3 - (1-t)]*M[i] + hi^2/6 * [t^3 - t]*M[i+1]

            double a = (1.0 - t);
            double b = t;
            double a3_minus_a = a * a * a - a;  // (1-t)^3 - (1-t)
            double b3_minus_b = b * b * b - b;   // t^3 - t

            out[j] = a * yi + b * yi1 +
                     (hi * hi / 6.0) * (a3_minus_a * Mi + b3_minus_b * Mi1);
        }
    }
}

/**
 * @brief Akima 样条插值（局部、无全局求解、SIMD + OpenMP）
 *
 * 与自然三次样条的关键区别：
 *   - 无三对角方程组求解（消除顺序依赖瓶颈）
 *   - 每个切线仅由局部 4 个斜率决定 → OpenMP 并行
 *   - 每段 Hermite 插值独立 → OpenMP 并行 + SIMD 内层循环
 *   - 抗 overshoot：数据不均匀时比自然三次更稳定
 *
 * @param x       极值点索引数组（单调递增）
 * @param y       极值点值数组
 * @param nPts    极值点数量
 * @param out     输出插值结果
 * @param outLo   插值起始索引（含）
 * @param outHi   插值结束索引（含）
 * @param work_s  工作缓冲区 [nPts-1]，调用方预分配
 * @param work_t  工作缓冲区 [nPts]，调用方预分配
 */
QUASARX_SIMD_TARGET
inline void akima_spline(const int* x, const double* y, size_t nPts,
                          double* out, int outLo, int outHi,
                          double* work_s, double* work_t) {
    if (nPts < 2) {
        double val = (nPts == 1) ? y[0] : 0.0;
        for (int j = outLo; j <= outHi; ++j) out[j] = val;
        return;
    }
    if (nPts == 2) {
        double dx = static_cast<double>(x[1] - x[0]);
        double dy = y[1] - y[0];
        for (int j = outLo; j <= outHi; ++j) {
            double t = (std::abs(dx) > 1e-15) ? (j - x[0]) / dx : 0.0;
            out[j] = y[0] + t * dy;
        }
        return;
    }

    const int n = static_cast<int>(nPts) - 1;

    // ── 步骤 1: 段斜率（写入 work_s，无分配） ──
    for (int i = 0; i < n; ++i) {
        double dx = static_cast<double>(x[i + 1] - x[i]);
        work_s[i] = (std::abs(dx) > 1e-15) ? (y[i + 1] - y[i]) / dx : 0.0;
    }

    // ── 步骤 2+3+4: 镜像扩展 + 权重 + 切线 ──
    // 内联扩展斜率，不分配 ext 数组：
    //   s[-2] = s[1],  s[-1] = 2s[0]-s[1]
    //   s[n]  = 2s[n-1]-s[n-2],  s[n+1] = s[n-1]+(s[n-1]-s[n-2])
    //
    // 对原始点 i，扩展索引 j=i+2：
    //   ext[j-2]=s[i-2], ext[j-1]=s[i-1], ext[j]=s[i], ext[j+1]=s[i+1]
    //   其中 s[k] 对 k<0 或 k>=n 使用镜像值

    // 辅助 lambda：获取扩展斜率 s[idx]，idx ∈ [-2, n+1]
    // 注意：n 是运行时值，不能用 switch case（MSVC 要求编译期常量）
    auto getS = [&](int idx) -> double {
        if (idx >= 0 && idx < n) return work_s[idx];
        if (idx == -2) return work_s[1];
        if (idx == -1) return 2.0 * work_s[0] - work_s[1];
        if (idx == n)  return 2.0 * work_s[n - 1] - work_s[n - 2];
        return work_s[n - 1] + (work_s[n - 1] - work_s[n - 2]); // idx == n+1
    };

    // 切线计算（每点独立 → OpenMP 并行）
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (int i = 0; i < static_cast<int>(nPts); ++i) {
        double sm2 = getS(i - 2);  // s[i-2]
        double sm1 = getS(i - 1);  // s[i-1]
        double s0  = getS(i);      // s[i]
        double sp1 = getS(i + 1);  // s[i+1]
        double w1 = std::abs(sp1 - sm2);
        double w2 = std::abs(sm1 - s0);
        double denom = w1 + w2;
        work_t[i] = (denom > 1e-15)
            ? (w2 * sm1 + w1 * s0) / denom
            : 0.5 * (sm1 + s0);
    }

    // ── 步骤 5: Hermite 三次插值（每段独立 → OpenMP 并行） ──
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (int i = 0; i < n; ++i) {
        int j0 = std::max(outLo, x[i]);
        int j1 = std::min(outHi, x[i + 1]);
        if (j0 > j1) continue;

        double hi = static_cast<double>(x[i + 1] - x[i]);
        double hiInv = (std::abs(hi) > 1e-15) ? 1.0 / hi : 0.0;
        double yi = y[i], yi1 = y[i + 1];
        double ti = work_t[i], ti1 = work_t[i + 1];
        double hti = hi * ti, hti1 = hi * ti1;

        // SIMD 内层循环：每个 j 独立计算 Hermite 基函数
        int count = j1 - j0 + 1;
        if (count <= 0) continue;

        int k = 0;
#if defined(__AVX2__)
        __m256d vyi  = _mm256_set1_pd(yi);
        __m256d vyi1 = _mm256_set1_pd(yi1);
        __m256d vhti = _mm256_set1_pd(hti);
        __m256d vhti1= _mm256_set1_pd(hti1);
        __m256d vxi  = _mm256_set1_pd(static_cast<double>(x[i]));
        __m256d vhiInv = _mm256_set1_pd(hiInv);
        // 基准索引 [j0, j0+1, j0+2, j0+3]
        __m256d vbase = _mm256_set_pd(j0 + 3.0, j0 + 2.0, j0 + 1.0, j0 + 0.0);
        __m256d vstep = _mm256_set1_pd(4.0);

        for (; k + 3 < count; k += 4) {
            // t = (j - x[i]) * hiInv
            __m256d vj = _mm256_add_pd(vbase, _mm256_set1_pd(static_cast<double>(k)));
            // 每轮 vbase 需递增 4 — 改用显式索引
            __m256d vt = _mm256_mul_pd(_mm256_sub_pd(vj, vxi), vhiInv);
            __m256d vt2 = _mm256_mul_pd(vt, vt);
            __m256d vomt = _mm256_sub_pd(_mm256_set1_pd(1.0), vt);
            __m256d vomt2 = _mm256_mul_pd(vomt, vomt);

            // H00 = (1+2t)(1-t)², H01 = t²(3-2t), H10 = t(1-t)², H11 = -t²(1-t)
            __m256d vh00 = _mm256_mul_pd(_mm256_add_pd(_mm256_set1_pd(1.0),
                                _mm256_add_pd(vt, vt)), vomt2);
            __m256d vh01 = _mm256_mul_pd(vt2, _mm256_sub_pd(_mm256_set1_pd(3.0),
                                _mm256_add_pd(vt, vt)));
            __m256d vh10 = _mm256_mul_pd(vt, vomt2);
            __m256d vh11 = _mm256_mul_pd(_mm256_sub_pd(_mm256_setzero_pd(), vt2), vomt);

            __m256d vresult = _mm256_add_pd(
                _mm256_add_pd(_mm256_mul_pd(vh00, vyi), _mm256_mul_pd(vh01, vyi1)),
                _mm256_add_pd(_mm256_mul_pd(vh10, vhti), _mm256_mul_pd(vh11, vhti1)));

            _mm256_storeu_pd(out + j0 + k, vresult);
        }
#elif defined(__SSE2__)
        __m128d vyi  = _mm_set1_pd(yi);
        __m128d vyi1 = _mm_set1_pd(yi1);
        __m128d vhti = _mm_set1_pd(hti);
        __m128d vhti1= _mm_set1_pd(hti1);
        __m128d vxi  = _mm_set1_pd(static_cast<double>(x[i]));
        __m128d vhiInv = _mm_set1_pd(hiInv);

        for (; k + 1 < count; k += 2) {
            __m128d vj = _mm_set_pd(j0 + k + 1.0, j0 + k + 0.0);
            __m128d vt = _mm_mul_pd(_mm_sub_pd(vj, vxi), vhiInv);
            __m128d vt2 = _mm_mul_pd(vt, vt);
            __m128d vomt = _mm_sub_pd(_mm_set1_pd(1.0), vt);
            __m128d vomt2 = _mm_mul_pd(vomt, vomt);

            __m128d vh00 = _mm_mul_pd(_mm_add_pd(_mm_set1_pd(1.0),
                                _mm_add_pd(vt, vt)), vomt2);
            __m128d vh01 = _mm_mul_pd(vt2, _mm_sub_pd(_mm_set1_pd(3.0),
                                _mm_add_pd(vt, vt)));
            __m128d vh10 = _mm_mul_pd(vt, vomt2);
            __m128d vh11 = _mm_mul_pd(_mm_sub_pd(_mm_setzero_pd(), vt2), vomt);

            __m128d vresult = _mm_add_pd(
                _mm_add_pd(_mm_mul_pd(vh00, vyi), _mm_mul_pd(vh01, vyi1)),
                _mm_add_pd(_mm_mul_pd(vh10, vhti), _mm_mul_pd(vh11, vhti1)));

            _mm_storeu_pd(out + j0 + k, vresult);
        }
#endif
        // 标量尾部
        for (; k < count; ++k) {
            int j = j0 + k;
            double t = static_cast<double>(j - x[i]) * hiInv;
            double t2 = t * t;
            double omt = 1.0 - t;
            double omt2 = omt * omt;
            out[j] = (1.0 + 2.0 * t) * omt2 * yi
                   + t2 * (3.0 - 2.0 * t) * yi1
                   + t * omt2 * hti
                   - t2 * omt * hti1;
        }
    }
}

/// 集合平均: out[i] = Σ buffers[j][i] / N
QUASARX_SIMD_TARGET
inline void simd_ensemble_average(const Vector<Vector<double>>& buffers,
                                   double* out, size_t n) {
    if (buffers.empty()) return;
    size_t N = buffers.size();
    double invN = 1.0 / static_cast<double>(N);

    size_t i = 0;
#if defined(__AVX512F__)
    __m512d vInv = _mm512_set1_pd(invN);
    for (; i + 7 < n; i += 8) {
        __m512d acc = _mm512_setzero_pd();
        for (const auto& buf : buffers) {
            acc = _mm512_add_pd(acc, _mm512_loadu_pd(buf.data() + i));
        }
        _mm512_storeu_pd(out + i, _mm512_mul_pd(acc, vInv));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline double simd_abs_max(const double* a, size_t n) {
    double maxVal = 0.0;
    size_t i = 0;
#if defined(__AVX512F__)
    __m512d vmax = _mm512_setzero_pd();
    __m512d signMask = _mm512_set1_pd(-0.0);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vabs = _mm512_andnot_pd(signMask, va);
        vmax = _mm512_max_pd(vmax, vabs);
    }
    alignas(64) double buf[8];
    _mm512_store_pd(buf, vmax);
    for (int j = 0; j < 8; ++j) if (buf[j] > maxVal) maxVal = buf[j];
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline double simd_range(const double* a, size_t n) {
    if (n == 0) return 0.0;
    double minVal = a[0], maxVal = a[0];
    size_t i = 1;
#if defined(__AVX512F__)
    __m512d vmin = _mm512_set1_pd(a[0]);
    __m512d vmax = _mm512_set1_pd(a[0]);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        vmin = _mm512_min_pd(vmin, va);
        vmax = _mm512_max_pd(vmax, va);
    }
    alignas(64) double bufMin[8], bufMax[8];
    _mm512_store_pd(bufMin, vmin);
    _mm512_store_pd(bufMax, vmax);
    for (int j = 0; j < 8; ++j) {
        if (bufMin[j] < minVal) minVal = bufMin[j];
        if (bufMax[j] > maxVal) maxVal = bufMax[j];
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_add_scalar(const double* a, double scalar, double* out, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    __m512d vs = _mm512_set1_pd(scalar);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        _mm512_storeu_pd(out + i, _mm512_add_pd(va, vs));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_add_scalar_inplace(double* a, double scalar, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    __m512d vs = _mm512_set1_pd(scalar);
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        _mm512_storeu_pd(a + i, _mm512_add_pd(va, vs));
    }
#elif defined(__AVX2__)
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
QUASARX_SIMD_TARGET
inline void simd_accumulate(const double* a, double* acc, size_t n) {
    size_t i = 0;
#if defined(__AVX512F__)
    for (; i + 7 < n; i += 8) {
        __m512d va = _mm512_loadu_pd(a + i);
        __m512d vacc = _mm512_loadu_pd(acc + i);
        _mm512_storeu_pd(acc + i, _mm512_add_pd(vacc, va));
    }
#elif defined(__AVX2__)
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
Vector<Vector<double>> simd_emd(const Vector<double>& data,
                                int numIMFs,
                                int maxSiftingIter = 10,
                                double sdThreshold = 0.02);
