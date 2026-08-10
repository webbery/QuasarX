#pragma once
#include "std_header.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

/**
 * PhaseSpace — 相空间重构与吸引子检测
 *
 * 通过延迟嵌入重构相空间，计算关联维数 D2 和最大 Lyapunov 指数，
 * 判断系统是否存在混沌吸引子。
 *
 * 判定逻辑:
 *   - D2 < embed_dim (非整数) + λ > 0 → 存在混沌吸引子
 *   - D2 ≈ embed_dim → 随机噪声
 *   - λ ≈ 0 → 准周期/噪声
 */

struct PhaseSpaceResult {
    int embed_dim = 3;
    int time_delay = 1;
    String delay_method = "mi";           // "mi" = 互信息法, "fixed" = 固定值

    // 相空间轨迹（降采样后用于前端可视化）
    Vector<Vector<double>> trajectory;    // [N][embed_dim]
    Vector<double> trajectory_time;       // 对应时间索引

    // 关联维数分析
    Vector<double> corr_r_values;         // 距离阈值 r 序列 (log 空间)
    Vector<double> corr_c_values;         // C(r) 关联积分
    double correlation_dimension = 0.0;   // D2

    // Lyapunov 指数
    Vector<double> lyap_divergence;       // 平均发散曲线
    double max_lyapunov = 0.0;            // 最大 Lyapunov 指数

    // 诊断
    bool is_deterministic = false;        // 是否存在确定性结构
    String diagnosis;                     // 诊断文字
};

class PhaseSpace {
public:
    /**
     * @brief 相空间重构与分析
     * @param series 时间序列（收益率）
     * @param embed_dim 嵌入维度（默认 3）
     * @param time_delay 时间延迟（0 = 自动用互信息法选择）
     * @param lyapunov_horizon Lyapunov 估计最大步长（默认 50）
     * @param max_trajectory_points 轨迹降采样上限（默认 2000）
     */
    static PhaseSpaceResult analyze(
        const Vector<double>& series,
        int embed_dim = 3,
        int time_delay = 0,
        int lyapunov_horizon = 50,
        int max_trajectory_points = 2000)
    {
        PhaseSpaceResult result;
        int N = static_cast<int>(series.size());
        if (N < 100) {
            result.diagnosis = "数据不足（需至少 100 点）";
            return result;
        }

        result.embed_dim = embed_dim;

        // 1. 确定时间延迟
        if (time_delay <= 0) {
            time_delay = estimateTimeDelayMI(series);
            result.delay_method = "mi";
        } else {
            result.delay_method = "fixed";
        }
        if (time_delay < 1) time_delay = 1;
        result.time_delay = time_delay;

        // 2. 延迟嵌入
        int M = N - (embed_dim - 1) * time_delay;
        if (M < 50) {
            result.diagnosis = "嵌入后有效点数不足";
            return result;
        }

        // 构建嵌入矩阵
        Vector<Vector<double>> embedded(M, Vector<double>(embed_dim));
        for (int i = 0; i < M; ++i) {
            for (int d = 0; d < embed_dim; ++d) {
                embedded[i][d] = series[i + d * time_delay];
            }
        }

        // 3. 降采样轨迹用于前端可视化
        int step = std::max(1, M / max_trajectory_points);
        for (int i = 0; i < M; i += step) {
            result.trajectory.push_back(embedded[i]);
            result.trajectory_time.push_back(static_cast<double>(i));
        }

        // 4. 关联维数 D2
        computeCorrelationDimension(embedded, M, embed_dim, result);

        // 5. 最大 Lyapunov 指数
        computeMaxLyapunov(embedded, M, embed_dim, lyapunov_horizon, result);

        // 6. 诊断
        diagnose(result);

        return result;
    }

private:
    /**
     * 互信息法估计最优时间延迟
     * 使用直方图法估计互信息 I(τ)，取第一个极小值
     */
    static int estimateTimeDelayMI(const Vector<double>& series, int max_tau = 100) {
        int N = static_cast<int>(series.size());
        if (N < 200) return 1;

        int max_lag = std::min(max_tau, N / 4);

        // 离散化为 64 个 bin
        int num_bins = 64;
        double vmin = *std::min_element(series.begin(), series.end());
        double vmax = *std::max_element(series.begin(), series.end());
        double range = vmax - vmin;
        if (range < 1e-15) return 1;
        double bin_width = range / num_bins;

        auto to_bin = [&](double v) -> int {
            int b = static_cast<int>((v - vmin) / bin_width);
            return std::min(b, num_bins - 1);
        };

        // 边缘概率 P(x_t)
        Vector<double> px(num_bins, 0.0);
        for (int i = 0; i < N; ++i) {
            px[to_bin(series[i])] += 1.0;
        }
        for (auto& p : px) p /= N;

        double prev_mi = 1e9;
        for (int tau = 1; tau <= max_lag; ++tau) {
            // 联合概率 P(x_t, x_{t+τ})
            Vector<Vector<double>> pxy(num_bins, Vector<double>(num_bins, 0.0));
            int count = 0;
            for (int i = 0; i < N - tau; ++i) {
                pxy[to_bin(series[i])][to_bin(series[i + tau])] += 1.0;
                ++count;
            }
            for (auto& row : pxy)
                for (auto& p : row)
                    p /= count;

            // 互信息 I = Σ p(x,y) * log[p(x,y) / (p(x)*p(y))]
            double mi = 0;
            for (int xi = 0; xi < num_bins; ++xi) {
                for (int yi = 0; yi < num_bins; ++yi) {
                    if (pxy[xi][yi] > 1e-15 && px[xi] > 1e-15 && px[yi] > 1e-15) {
                        mi += pxy[xi][yi] * std::log(pxy[xi][yi] / (px[xi] * px[yi]));
                    }
                }
            }

            // 第一个极小值
            if (tau > 1 && mi > prev_mi) {
                return tau - 1;
            }
            prev_mi = mi;
        }

        return 1;
    }

    /**
     * 关联维数 D2: Grassberger-Procaccia 算法
     * C(r) = (2/N(N-1)) * #{|Xi - Xj| < r}, i ≠ j
     * D2 = d ln C(r) / d ln r
     */
    static void computeCorrelationDimension(
        const Vector<Vector<double>>& embedded,
        int M, int embed_dim,
        PhaseSpaceResult& result)
    {
        // 采样点对（避免 O(N²) 全量计算）
        int sample_size = std::min(M, 1000);
        int step = std::max(1, M / sample_size);

        // 计算所有采样点对的距离
        Vector<double> distances;
        distances.reserve(sample_size * sample_size / 2);

        for (int i = 0; i < M; i += step) {
            for (int j = i + step; j < M; j += step) {
                double dist = 0;
                for (int d = 0; d < embed_dim; ++d) {
                    double diff = embedded[i][d] - embedded[j][d];
                    dist += diff * diff;
                }
                distances.push_back(std::sqrt(dist));
            }
        }

        if (distances.empty()) return;

        std::sort(distances.begin(), distances.end());

        // 选取 r 的范围（从第 5 百分位到第 95 百分位，对数等间距）
        int nd = static_cast<int>(distances.size());
        double d_min = distances[nd / 20];
        double d_max = distances[nd - nd / 20];
        if (d_min < 1e-15) d_min = 1e-15;
        if (d_max < d_min * 2) d_max = d_min * 10;

        int num_r = 30;
        result.corr_r_values.resize(num_r);
        result.corr_c_values.resize(num_r);

        double log_min = std::log(d_min);
        double log_max = std::log(d_max);

        for (int ri = 0; ri < num_r; ++ri) {
            double log_r = log_min + (log_max - log_min) * ri / (num_r - 1);
            double r = std::exp(log_r);
            result.corr_r_values[ri] = log_r; // 存 log(r)

            // C(r) = #{dist < r} / total
            int count = static_cast<int>(
                std::lower_bound(distances.begin(), distances.end(), r) - distances.begin());
            double cr = static_cast<double>(count) / nd;
            if (cr < 1e-15) cr = 1e-15;
            result.corr_c_values[ri] = std::log(cr); // 存 log(C(r))
        }

        // 线性拟合中间段求 D2
        // 取 20%-80% 的范围做线性回归
        int lo = num_r / 5;
        int hi = num_r * 4 / 5;
        int fit_n = hi - lo;
        if (fit_n < 3) fit_n = num_r;

        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = lo; i < hi; ++i) {
            sx += result.corr_r_values[i];
            sy += result.corr_c_values[i];
            sxx += result.corr_r_values[i] * result.corr_r_values[i];
            sxy += result.corr_r_values[i] * result.corr_c_values[i];
        }
        double denom = fit_n * sxx - sx * sx;
        if (std::abs(denom) > 1e-15) {
            result.correlation_dimension = (fit_n * sxy - sx * sy) / denom;
        }
    }

    /**
     * 最大 Lyapunov 指数 — Rosenstein 简化算法
     *
     * 对每个参考点找最近邻，跟踪平均发散距离随时间的演化。
     * λ ≈ 平均斜率 of ln(d(t)) vs t
     */
    static void computeMaxLyapunov(
        const Vector<Vector<double>>& embedded,
        int M, int embed_dim,
        int max_horizon,
        PhaseSpaceResult& result)
    {
        int horizon = std::min(max_horizon, M / 4);
        if (horizon < 5) return;

        // 限制采样点数量以控制计算量
        int sample_size = std::min(M, 500);
        int step = std::max(1, M / sample_size);

        // 对每个采样点找最近邻（排除时间相邻点，避免自匹配）
        Vector<int> nearest(M, -1);
        int exclude_window = embed_dim * result.time_delay;

        for (int i = 0; i < M; i += step) {
            double best_dist = std::numeric_limits<double>::max();
            int best_j = -1;
            for (int j = 0; j < M; ++j) {
                if (std::abs(i - j) <= exclude_window) continue;
                double dist = 0;
                for (int d = 0; d < embed_dim; ++d) {
                    double diff = embedded[i][d] - embedded[j][d];
                    dist += diff * diff;
                }
                if (dist < best_dist) {
                    best_dist = dist;
                    best_j = j;
                }
            }
            nearest[i] = best_j;
        }

        // 跟踪平均发散
        Vector<double> sum_ln_d(horizon, 0.0);
        Vector<int> counts(horizon, 0);

        for (int i = 0; i < M; i += step) {
            int j = nearest[i];
            if (j < 0) continue;

            for (int t = 0; t < horizon; ++t) {
                if (i + t >= M || j + t >= M) break;
                double dist = 0;
                for (int d = 0; d < embed_dim; ++d) {
                    double diff = embedded[i + t][d] - embedded[j + t][d];
                    dist += diff * diff;
                }
                dist = std::sqrt(dist);
                if (dist > 1e-15) {
                    sum_ln_d[t] += std::log(dist);
                    ++counts[t];
                }
            }
        }

        // 平均 ln(d(t))
        result.lyap_divergence.resize(horizon);
        for (int t = 0; t < horizon; ++t) {
            if (counts[t] > 0) {
                result.lyap_divergence[t] = sum_ln_d[t] / counts[t];
            }
        }

        // 线性拟合前 1/3 段求 λ
        int fit_end = std::max(5, horizon / 3);
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int fit_n = 0;
        for (int t = 1; t < fit_end; ++t) {
            if (counts[t] > 0) {
                double x = static_cast<double>(t);
                double y = result.lyap_divergence[t];
                sx += x; sy += y;
                sxx += x * x; sxy += x * y;
                ++fit_n;
            }
        }
        if (fit_n >= 3) {
            double denom = fit_n * sxx - sx * sx;
            if (std::abs(denom) > 1e-15) {
                result.max_lyapunov = (fit_n * sxy - sx * sy) / denom;
            }
        }
    }

    static void diagnose(PhaseSpaceResult& result) {
        double D2 = result.correlation_dimension;
        double lambda = result.max_lyapunov;
        int m = result.embed_dim;

        char buf[256];
        if (D2 > 0 && D2 < m * 0.9 && lambda > 0.001) {
            result.is_deterministic = true;
            snprintf(buf, sizeof(buf),
                "存在混沌吸引子: D2=%.2f < m=%d, λ=%.4f > 0", D2, m, lambda);
        } else if (D2 >= m * 0.9) {
            result.is_deterministic = false;
            snprintf(buf, sizeof(buf),
                "疑似随机噪声: D2=%.2f ≈ m=%d, λ=%.4f", D2, m, lambda);
        } else if (lambda <= 0.001 && lambda >= -0.001) {
            result.is_deterministic = false;
            snprintf(buf, sizeof(buf),
                "准周期或噪声: D2=%.2f, λ≈%.4f", D2, lambda);
        } else {
            result.is_deterministic = false;
            snprintf(buf, sizeof(buf),
                "D2=%.2f, λ=%.4f — 结构不明确，建议增加数据量或调整参数", D2, lambda);
        }
        result.diagnosis = buf;
    }
};
