#pragma once
#include "std_header.h"
#include <complex>
#include <cmath>

/**
 * FFT — 轻量级 radix-2 Cooley-Tukey FFT
 *
 * 支持正变换和逆变换，输入长度自动填充到 2 的幂。
 * 使用 std::complex<double>，适合 VMD 频域计算。
 */

namespace fft {

using complex_t = std::complex<double>;

/// 位反转置换
inline size_t bit_reverse(size_t x, int bits) {
    size_t r = 0;
    for (int i = 0; i < bits; ++i) {
        r = (r << 1) | (x & 1);
        x >>= 1;
    }
    return r;
}

/// 计算不小于 n 的最小 2 的幂及其 log2
inline void next_pow2(size_t n, size_t& out_size, int& out_bits) {
    out_bits = 0;
    out_size = 1;
    while (out_size < n) {
        out_size <<= 1;
        out_bits++;
    }
}

/**
 * @brief 原地 radix-2 FFT (Cooley-Tukey DIT)
 * @param data  复数数组 (长度必须为 2 的幂)
 * @param n     数组长度
 * @param inv   true = 逆变换, false = 正变换
 */
inline void transform(complex_t* data, size_t n, bool inv = false) {
    int bits = 0;
    size_t tmp = n;
    while (tmp > 1) { tmp >>= 1; bits++; }

    // 位反转置换
    for (size_t i = 0; i < n; ++i) {
        size_t j = bit_reverse(i, bits);
        if (j > i) std::swap(data[i], data[j]);
    }

    // 蝶形运算
    double sign = inv ? 1.0 : -1.0;
    for (size_t len = 2; len <= n; len <<= 1) {
        size_t half = len >> 1;
        double angle = sign * 2.0 * M_PI / static_cast<double>(len);
        complex_t wn(std::cos(angle), std::sin(angle));

        for (size_t i = 0; i < n; i += len) {
            complex_t w(1.0, 0.0);
            for (size_t j = 0; j < half; ++j) {
                complex_t t = w * data[i + j + half];
                complex_t u = data[i + j];
                data[i + j] = u + t;
                data[i + j + half] = u - t;
                w *= wn;
            }
        }
    }

    // 逆变换归一化
    if (inv) {
        double scale = 1.0 / static_cast<double>(n);
        for (size_t i = 0; i < n; ++i) data[i] *= scale;
    }
}

/**
 * @brief 实数序列 FFT
 * @param data  实数输入
 * @param n     输入长度
 * @return      复数频谱 (长度 = next_pow2(n))
 */
inline Vector<complex_t> rfft(const double* data, size_t n) {
    size_t fft_size;
    int bits;
    next_pow2(n, fft_size, bits);

    Vector<complex_t> buf(fft_size);
    for (size_t i = 0; i < n; ++i) buf[i] = complex_t(data[i], 0.0);
    for (size_t i = n; i < fft_size; ++i) buf[i] = complex_t(0.0, 0.0);

    transform(buf.data(), fft_size, false);
    return buf;
}

/**
 * @brief 复数序列 IFFT
 * @param spectrum  频域数据
 * @param n         取前 n 个实数输出
 * @return          时域实数序列 (长度 = n)
 */
inline Vector<double> irfft(const Vector<complex_t>& spectrum, size_t n) {
    Vector<complex_t> buf = spectrum;
    transform(buf.data(), buf.size(), true);

    Vector<double> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = buf[i].real();
    return out;
}

} // namespace fft
