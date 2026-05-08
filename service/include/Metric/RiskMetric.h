#pragma once
#include <vector>
#include <string>

/*
 * @brief 计算 VaR (Value at Risk，风险价值)
 * @param returns 日收益率序列
 * @param confidence_level 置信度，如 0.95 表示 95% VaR
 * @return VaR 值（正值，表示在该置信度下的最大日损失）
 */
float compute_var(const std::vector<double>& returns, double confidence_level = 0.95);

/*
 * @brief 计算 CVaR (Conditional VaR，预期短缺)
 * @param returns 日收益率序列
 * @param confidence_level 置信度，如 0.95
 * @return CVaR 值（正值，表示超过 VaR 阈值的平均损失）
 */
float compute_cvar(const std::vector<double>& returns, double confidence_level = 0.95);

/*
 * @brief 计算收益率序列的滞后 k 阶自相关系数
 * @param returns 日收益率序列
 * @param lag 滞后阶数，默认 lag=1（检验相邻收益率的相关性）
 * @return 自相关系数 [-1, 1]
 *
 * 判断标准：
 *   |acf| < 0.1  → 无显著自相关，使用普通 Bootstrap
 *   |acf| >= 0.1 → 存在自相关，使用 Block Bootstrap
 */
double compute_autocorrelation(const std::vector<double>& returns, int lag = 1);

/*
 * @brief Bootstrap 蒙特卡洛风险分析结果
 */
struct BootstrapResult {
    // 爆仓概率
    float _ruin_prob_50;      // 净值跌破初始资金 50% 的概率
    float _ruin_prob_30;      // 净值跌破初始资金 30% 的概率

    // 收益率分布
    float _return_p5;         // 5% 分位数收益率（最差 5% 场景）
    float _return_p50;        // 中位数收益率
    float _return_p95;        // 95% 分位数收益率

    // 最大回撤分布
    float _max_dd_p50;        // 中位数最大回撤
    float _max_dd_p95;        // 95% 分位数最大回撤

    // 综合指标
    float _median_annual_ret; // 中位数年化收益
    float _tail_1pct_avg_dd;  // 最差 1% 路径的平均最大回撤

    // 元信息
    int   _method;            // 0=普通Bootstrap, 1=Block Bootstrap
    int   _block_size;        // Block Bootstrap 的块大小
    float _autocorrelation;   // 1阶自相关系数
    int   _n_simulations;     // 模拟次数

    // 压力测试（波动率 × 1.5）
    float _stress_ruin_prob_50;
    float _stress_ruin_prob_30;
    float _stress_return_p5;
    float _stress_return_p50;
    float _stress_max_dd_p50;
};

/*
 * @brief Bootstrap 蒙特卡洛风险分析
 * @param daily_returns 日收益率序列
 * @param initial_capital 初始资金（默认 1.0 表示归一化）
 * @param n_simulations 模拟次数
 * @param seed 随机种子（0 = 使用系统随机）
 * @return BootstrapResult
 *
 * 逻辑：
 * 1. 先计算 1 阶自相关系数
 * 2. |acf| < 0.1 → 普通 Bootstrap（独立抽样）
 * 3. |acf| >= 0.1 → Block Bootstrap（块大小 = max(5, 252/N*10)）
 * 4. 同时运行压力测试场景（收益率 × 1.5 波动率）
 */
BootstrapResult bootstrap_analysis(
    const std::vector<double>& daily_returns,
    double initial_capital = 1.0,
    int n_simulations = 10000,
    unsigned seed = 0
);

/*
 * @brief 格式化输出 BootstrapResult 的极端场景信息（用于日志/调试）
 */
std::string bootstrap_result_to_string(const BootstrapResult& result);
