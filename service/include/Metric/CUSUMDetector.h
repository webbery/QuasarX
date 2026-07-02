#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>

/**
 * @brief CUSUM（累积和）变点检测配置
 *
 * 默认参数基于文献推荐值（Hinkley 1971, Page 1954）：
 *   lambda = 0.5  →  k = 0.5 * sigma（中等敏感度）
 *   threshold_multiplier = 4.0 → h = 4 * sigma * sqrt(N)
 *   min_obs = 30  →  前 30 个交易日不触发检测（避免初期噪声）
 */
struct CUSUMConfig {
    double _mu = 0.0;                    // 样本外预期均值（通常取训练期均值或 0）
    double _sigma = 1.0;                 // 样本外预期波动率（用于计算 k 和 h）
    double _lambda = 0.5;                // 容许偏差倍数 (k = lambda * sigma)
    double _threshold_multiplier = 4.0;  // 阈值倍数 (h = threshold_multiplier * sigma * sqrt(N))
    size_t _min_obs = 30;                // 最少观测数，低于此值不触发变点
};

/**
 * @brief 单步 CUSUM 检测结果
 */
struct CUSUMStepResult {
    bool _change_point = false;          // 是否检测到变点
    size_t _step_index = 0;              // 当前步索引
    double _cusum_positive = 0.0;        // 正向累积和 S+
    double _cusum_negative = 0.0;        // 负向累积和 S-
    double _current_drift = 0.0;         // 当前净漂移量 (S+ - S-)
};

/**
 * @brief 批量 CUSUM 检测结果
 */
struct CUSUMResult {
    std::vector<CUSUMStepResult> _steps; // 每步检测结果
    size_t _total_change_points = 0;     // 总变点次数
    double _max_drift = 0.0;             // 最大漂移量
    size_t _last_change_index = 0;       // 最后变点索引（0 表示未触发）
};

/**
 * @brief 双侧 CUSUM 变点检测器
 *
 * 算法：
 *   S+_t = max(0, S+_{t-1} + (r_t - mu) - k)
 *   S-_t = max(0, S-_{t-1} - (r_t - mu) - k)
 *   触发条件：max(S+_t, S-_t) > h
 *
 * 其中 k = lambda * sigma, h = threshold_multiplier * sigma * sqrt(N)
 *
 * 用途：
 *   1. 检测收益率分布的结构性断裂（市场 regime 变化）
 *   2. 触发 VaR 模型自适应调整（缩短窗口、提高置信度）
 *   3. 回测结束后输出风险诊断指标
 */
class CUSUMDetector {
public:
    explicit CUSUMDetector(CUSUMConfig config = {});

    /**
     * @brief 样本外单步更新（每日收益率推送）
     * @param new_return 当日收益率
     * @return 当前步检测结果
     */
    CUSUMStepResult update(double new_return);

    /**
     * @brief 批量检测（回测/历史数据分析用）
     * @param returns 收益率序列
     * @return 完整检测结果
     */
    CUSUMResult detect_batch(const std::vector<double>& returns);

    /**
     * @brief 重置检测器状态（清空历史）
     */
    void reset();

    // === 只读访问器 ===
    bool has_change_point() const { return _last_result._change_point; }
    size_t get_total_change_points() const { return _total_change_points; }
    double get_max_drift() const { return _max_drift; }
    size_t get_last_change_index() const { return _last_change_index; }
    size_t get_observation_count() const { return _count; }

    const CUSUMConfig& get_config() const { return _config; }
    void set_config(CUSUMConfig config) { _config = config; reset(); }

private:
    double compute_threshold() const;

    CUSUMConfig _config;
    CUSUMStepResult _last_result;
    double _s_pos = 0.0;
    double _s_neg = 0.0;
    size_t _count = 0;

    // 累计统计
    size_t _total_change_points = 0;
    double _max_drift = 0.0;
    size_t _last_change_index = 0;
};
