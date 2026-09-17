#pragma once
#include "std_header.h"
#include <complex>
#include <cmath>
#include <unordered_map>
#include <mutex>

/**
 * FFT — FFTW3 后端
 *
 * 通过 fftw_plan 缓存机制复用 FFTW3 planner,避免每次调用都重新创建 plan
 * (planner 创建开销 O(N log N) + Wisdom 搜索,缓存后单次调用退化为纯 memcpy+execute)。
 *
 * 线程安全:PlanPool::getOrCreate 内部 std::mutex 保护,多线程并发调用安全。
 *
 * 接口(rfft/irfft)签名与旧 radix-2 inline 实现完全一致 → 下游 VMD.cpp / CEEMDAN.cpp
 * 无需任何改动即可切换到 FFTW3 后端。
 *
 * 适用条件: FFTW3 r2c/c2r 支持任意 2/3/5/7 因子分解长度 (不再强制 power-of-2).
 *                        对于偶数 N = 2n 等 2:2*3:3:5:7 最佳.
 */
#include <fftw3.h>

namespace fft {

using complex_t = std::complex<double>;

namespace detail {

/// FFTW3 plan + buffer 持有者
struct R2CPlan {
    size_t n = 0;              // 输入长度(必须为 2 的幂)
    fftw_plan plan = nullptr;
    double* in = nullptr;
    fftw_complex* out = nullptr;
};

struct C2RPlan {
    size_t n = 0;
    fftw_plan plan = nullptr;
    fftw_complex* in = nullptr;
    double* out = nullptr;
};

/// 全局 plan 池(懒创建)
class PlanPool {
public:
    static PlanPool& instance() {
        static PlanPool p;
        return p;
    }

    R2CPlan& getR2C(size_t n) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = r2c_.find(n);
        if (it != r2c_.end()) return it->second;

        R2CPlan p;
        p.n = n;
        p.in = (double*)fftw_malloc(sizeof(double) * n);
        p.out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (n / 2 + 1));
        // FFTW_ESTIMATE:跳过 wisdom 搜索,plan 创建 O(N),运行时仍使用 SIMD 加速(自动 SSE2/AVX)
        p.plan = fftw_plan_dft_r2c_1d(static_cast<int>(n), p.in, p.out, FFTW_ESTIMATE);

        auto [ins, _] = r2c_.emplace(n, std::move(p));
        return ins->second;
    }

    C2RPlan& getC2R(size_t n) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = c2r_.find(n);
        if (it != c2r_.end()) return it->second;

        C2RPlan p;
        p.n = n;
        p.in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (n / 2 + 1));
        p.out = (double*)fftw_malloc(sizeof(double) * n);
        p.plan = fftw_plan_dft_c2r_1d(static_cast<int>(n), p.in, p.out, FFTW_ESTIMATE);

        auto [ins, _] = c2r_.emplace(n, std::move(p));
        return ins->second;
    }

    ~PlanPool() {
        for (auto& [k, p] : r2c_) {
            if (p.plan) fftw_destroy_plan(p.plan);
            if (p.in) fftw_free(p.in);
            if (p.out) fftw_free(p.out);
        }
        for (auto& [k, p] : c2r_) {
            if (p.plan) fftw_destroy_plan(p.plan);
            if (p.in) fftw_free(p.in);
            if (p.out) fftw_free(p.out);
        }
    }

private:
    PlanPool() = default;
    std::mutex mtx_;
    std::unordered_map<size_t, R2CPlan> r2c_;
    std::unordered_map<size_t, C2RPlan> c2r_;
};

/// 计算不小于 n 的最小 2 的幂(对齐 FFTW3 radix-2 路径要求)
inline size_t next_pow2(size_t n) {
    size_t s = 1;
    while (s < n) s <<= 1;
    return s;
}

} // namespace detail

/**
 * @brief 实数序列 FFT (FFTW3 r2c 后端)
 * @param data  实数输入
 * @param n     输入长度 (FFTW3 自动按 2/3/5/7 因子分解,不再强制 power-of-2)
 * @return      复数半谱 (长度 = n/2 + 1)
 *
 * 注: 由 VMD 需求驱动,支持任意 n (≥ 4). FFTW3 r2c 原生支持任意因子分解长度.
 */
inline Vector<complex_t> rfft(const double* data, size_t n) {
    // 直接使用 n,不再扩展到 next_pow2 (FFTW3 支持任意 2/3/5/7 因子分解)
    size_t fftSize = n;
    auto& p = detail::PlanPool::instance().getR2C(fftSize);

    // 输入 memcpy 到 FFTW buffer (无 zero-pad,因 fftSize = n)
    std::memcpy(p.in, data, sizeof(double) * n);

    fftw_execute(p.plan);

    size_t halfLen = fftSize / 2 + 1;
    Vector<complex_t> result(halfLen);
    // fftw_complex == double[2] 在所有支持的平台;用 reinterpret_cast 而非 memcpy 即可
    std::memcpy(result.data(), p.out, sizeof(fftw_complex) * halfLen);
    return result;
}

/**
 * @brief 复数半谱 IFFT (FFTW3 c2r 后端)
 * @param spectrum  半谱(长度 = n/2 + 1)
 * @param n         输出实数序列长度 (必须等于 IFFT 时的 fftSize)
 * @return          时域实数序列 (长度 = n)
 *
 * 注意:FFT c2r 不归一化,需手动除以 N。
 */
inline Vector<double> irfft(const Vector<complex_t>& spectrum, size_t n) {
    // 直接使用 n,不再扩展到 next_pow2
    size_t fftSize = n;
    auto& p = detail::PlanPool::instance().getC2R(fftSize);

    size_t halfLen = fftSize / 2 + 1;
    std::memcpy(p.in, spectrum.data(), sizeof(fftw_complex) * std::min(halfLen, spectrum.size()));

    fftw_execute(p.plan);

    double scale = 1.0 / static_cast<double>(fftSize);
    Vector<double> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = p.out[i] * scale;
    return out;
}

} // namespace fft
