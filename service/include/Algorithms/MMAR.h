#pragma once
#include "std_header.h"
#include <cmath>
#include <algorithm>
#include <numeric>

/**
 * MMAR — Multifractal Model of Asset Returns
 *
 * 基于 MF-DFA (Multifractal Detrended Fluctuation Analysis) 的多分形分析。
 * 用于检测资产收益率的多重分形特征和标度行为。
 *
 * 参考文献: Kantelhardt et al. (2002), Physica A 316, 87-114
 */

struct MMARResult {
    Vector<double> q_values;       // q 值序列
    Vector<double> hq;             // 广义 Hurst 指数 h(q)
    Vector<double> tau_q;          // 质量指数 τ(q)
    Vector<double> alpha;          // 奇异性指数 α
    Vector<double> f_alpha;        // 多分形谱 f(α)
    double hurst = 0.5;            // 标准 Hurst 指数 h(2)
    double spectrum_width = 0.0;   // 多分形谱宽度 Δα = α_max - α_min
};

class MMAR {
public:
    /**
     * @brief MF-DFA 多分形分析
     * @param returns 收益率序列
     * @param q_min q 值下界（默认 -5）
     * @param q_max q 值上界（默认 5）
     * @param q_step q 值步长（默认 0.5）
     * @param min_window 最小窗口长度（默认 10）
     * @param max_scales 最大尺度数（默认自动生成，不超过 N/4）
     * @return MMARResult 多分形分析结果
     */
    static MMARResult analyze(
        const Vector<double>& returns,
        double q_min = -5.0,
        double q_max = 5.0,
        double q_step = 0.5,
        int min_window = 10,
        int max_scales = 0)
    {
        MMARResult result;
        int N = static_cast<int>(returns.size());
        if (N < 50) return result;

        // 1. 累积偏差 Y(t) = Σ[r(i) - mean(r)]
        double mean_r = 0;
        for (auto v : returns) mean_r += v;
        mean_r /= N;

        Vector<double> Y(N);
        double cumsum = 0;
        for (int i = 0; i < N; ++i) {
            cumsum += returns[i] - mean_r;
            Y[i] = cumsum;
        }

        // 2. 生成尺度序列（对数等间距）
        int max_s = (max_scales > 0) ? max_scales : N / 4;
        if (max_s < min_window * 2) max_s = min_window * 2;
        if (max_s > N / 2) max_s = N / 2;

        int num_scales = 30;
        Vector<int> scales(num_scales);
        double log_min = std::log(static_cast<double>(min_window));
        double log_max = std::log(static_cast<double>(max_s));
        for (int i = 0; i < num_scales; ++i) {
            double log_s = log_min + (log_max - log_min) * i / (num_scales - 1);
            scales[i] = static_cast<int>(std::round(std::exp(log_s)));
        }
        // 去重
        auto last = std::unique(scales.begin(), scales.end());
        scales.erase(last, scales.end());
        int actual_scales = static_cast<int>(scales.size());

        // 3. 生成 q 值序列
        Vector<double> q_vals;
        for (double q = q_min; q <= q_max + q_step * 0.1; q += q_step) {
            if (std::abs(q) < 0.01) continue; // 跳过 q=0
            q_vals.push_back(q);
        }
        result.q_values = q_vals;

        int nq = static_cast<int>(q_vals.size());
        result.hq.resize(nq);
        result.tau_q.resize(nq);

        // 4. 对每个 q 计算 Fq(s) ~ s^h(q)
        for (int qi = 0; qi < nq; ++qi) {
            double q = q_vals[qi];
            Vector<double> log_s(actual_scales);
            Vector<double> log_Fq(actual_scales);
            int valid_count = 0;

            for (int si = 0; si < actual_scales; ++si) {
                int s = scales[si];
                int Ns = N / s; // 区间数
                if (Ns < 2) continue;

                double Fq_sum = 0;
                int valid_segments = 0;

                for (int v = 0; v < Ns; ++v) {
                    int start = v * s;
                    // 线性去趋势
                    double sx = 0, sy = 0, sxx = 0, sxy = 0;
                    for (int j = 0; j < s; ++j) {
                        double x = static_cast<double>(j);
                        double y = Y[start + j];
                        sx += x;
                        sy += y;
                        sxx += x * x;
                        sxy += x * y;
                    }
                    double denom = s * sxx - sx * sx;
                    double slope = 0, intercept = 0;
                    if (std::abs(denom) > 1e-15) {
                        slope = (s * sxy - sx * sy) / denom;
                        intercept = (sy - slope * sx) / s;
                    }

                    // 计算去趋势方差
                    double variance = 0;
                    for (int j = 0; j < s; ++j) {
                        double trend = slope * j + intercept;
                        double residual = Y[start + j] - trend;
                        variance += residual * residual;
                    }
                    variance /= s;

                    if (variance > 1e-30) {
                        if (q > 0) {
                            Fq_sum += std::pow(variance, q / 2.0);
                        } else {
                            Fq_sum += std::pow(variance, q / 2.0);
                        }
                        ++valid_segments;
                    }
                }

                if (valid_segments > 0 && Fq_sum > 1e-30) {
                    double Fq = std::pow(Fq_sum / valid_segments, 1.0 / q);
                    log_s[valid_count] = std::log(static_cast<double>(s));
                    log_Fq[valid_count] = std::log(Fq);
                    ++valid_count;
                }
            }

            // 5. 线性回归求 h(q)
            if (valid_count >= 3) {
                result.hq[qi] = linearRegressionSlope(
                    log_s.data(), log_Fq.data(), valid_count);
            } else {
                result.hq[qi] = 0.5;
            }
        }

        // 6. 计算 τ(q) = q * h(q) - 1
        for (int i = 0; i < nq; ++i) {
            result.tau_q[i] = q_vals[i] * result.hq[i] - 1.0;
        }

        // 7. 计算多分形谱 f(α)
        // α(q) = dτ/dq, f(α) = q * α - τ(q)
        result.alpha.resize(nq);
        result.f_alpha.resize(nq);

        for (int i = 0; i < nq; ++i) {
            // 数值微分 dτ/dq
            double dtau_dq;
            if (i == 0) {
                dtau_dq = (result.tau_q[1] - result.tau_q[0]) / (q_vals[1] - q_vals[0]);
            } else if (i == nq - 1) {
                dtau_dq = (result.tau_q[nq - 1] - result.tau_q[nq - 2]) / (q_vals[nq - 1] - q_vals[nq - 2]);
            } else {
                dtau_dq = (result.tau_q[i + 1] - result.tau_q[i - 1]) / (q_vals[i + 1] - q_vals[i - 1]);
            }
            result.alpha[i] = dtau_dq;
            result.f_alpha[i] = q_vals[i] * dtau_dq - result.tau_q[i];
        }

        // 8. 提取关键指标
        // Hurst = h(2): 找 q 最接近 2 的值
        int h2_idx = 0;
        double min_dist = 1e9;
        for (int i = 0; i < nq; ++i) {
            double d = std::abs(q_vals[i] - 2.0);
            if (d < min_dist) { min_dist = d; h2_idx = i; }
        }
        result.hurst = result.hq[h2_idx];

        // 多分形谱宽度 Δα
        double alpha_min = *std::min_element(result.alpha.begin(), result.alpha.end());
        double alpha_max = *std::max_element(result.alpha.begin(), result.alpha.end());
        result.spectrum_width = alpha_max - alpha_min;

        return result;
    }

private:
    static double linearRegressionSlope(const double* x, const double* y, int n) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = 0; i < n; ++i) {
            sx += x[i];
            sy += y[i];
            sxx += x[i] * x[i];
            sxy += x[i] * y[i];
        }
        double denom = n * sxx - sx * sx;
        if (std::abs(denom) < 1e-15) return 0.5;
        return (n * sxy - sx * sy) / denom;
    }
};
