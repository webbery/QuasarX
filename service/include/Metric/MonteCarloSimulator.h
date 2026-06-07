#pragma once
#include <vector>
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include "Bridge/exchange.h"

/*
 * @brief Bootstrap 方法选择
 */
enum class BootstrapMethod : uint8_t {
    Auto,       // 根据自相关自动选择
    Standard,   // 强制标准 Bootstrap
    Block       // 强制 Block Bootstrap
};

/*
 * @brief 蒙特卡洛模拟配置
 */
struct McSimConfig {
    int n_simulations = 20000;       // 模拟次数
    unsigned seed = 0;               // 随机种子（0 = 使用系统随机）
    double initial_capital = 1.0;    // 初始资金（归一化）
    TradingMode trading_mode = TradingMode::T1;  // 交易模式（元信息，不改变抽样逻辑）
    int bars_per_year = 252;         // 年化基准：日级=252，分钟级=60480
    BootstrapMethod bootstrap_method = BootstrapMethod::Auto;

    // 压力测试
    bool enable_stress_test = true;
    double stress_vol_factor = 1.5;  // 基础压力：波动率放大倍数

    // 流动性压力：尾部收益率折扣
    bool stress_liquidity = true;
    double liquidity_factor = 0.8;   // 尾部最差 10% 收益率 × 0.8
    double liquidity_tail_pct = 0.10;

    // 波动率聚集压力：使用更大 block 模拟持续亏损
    bool stress_vol_clustering = true;
    double vol_cluster_block_multiplier = 2.0;  // Block Size × 2

    // 双向极端路径保存
    int save_worst_paths_count = 50;  // 保存最差路径数（0 = 不保存）
    int save_best_paths_count = 50;   // 保存最好路径数（0 = 不保存）
    bool save_percentile_paths = true; // 是否保存 P10/P50/P90 基准路径

    // 压力测试路径（默认只保存最差）
    bool save_stress_worst_paths = false;
    int save_stress_paths_count = 50;
};

/*
 * @brief 单条路径的详细数据（用于策略归因分析）
 */
struct PathDetail {
    double total_return;              // 总收益率
    double max_drawdown;              // 最大回撤
    double win_rate;                  // 路径胜率（正收益 bar 占比）
    int longest_win_streak;           // 最长连胜天数
    int longest_loss_streak;          // 最长连亏天数
    size_t max_dd_bar_index;          // 最大回撤发生的 bar 索引
    size_t peak_bar_index;            // 净值峰值位置
    size_t trough_bar_index;          // 净值谷值位置
    double vol_ratio;                 // 波动率比率（该路径 std / 原始序列 std）
    std::vector<double> equity_curve; // 完整资金曲线（归一化，equity_curve[0] = 1.0）
};

/*
 * @brief 蒙特卡洛模拟结果
 */
struct McResult {
    // === 基础统计指标 ===
    float ruin_prob_high;     // 跌破高位阈值（默认 50%）
    float ruin_prob_low;      // 跌破低位阈值（默认 30%）
    float return_p5;          // 5% 分位数（最差 5% 场景）
    float return_p50;         // 中位数收益率
    float return_p95;         // 95% 分位数
    float max_dd_p50;         // 中位数最大回撤
    float max_dd_p95;         // 95% 分位数最大回撤
    float median_annual_ret;  // 中位数年化收益
    float tail_1pct_avg_dd;   // 最差 1% 路径的平均最大回撤
    int method;               // 0=Standard, 1=Block
    int block_size;           // Block 大小
    float autocorrelation;    // 1 阶自相关系数
    int n_simulations;        // 模拟次数

    // === 双向极端路径 ===

    // 最差 N 条（失效场景分析）
    std::vector<PathDetail> worst_paths;

    // 最好 N 条（有效场景分析）
    std::vector<PathDetail> best_paths;

    // 分位数基准路径
    PathDetail p10_path;
    PathDetail median_path;
    PathDetail p90_path;

    // === 压力测试指标（基础：波动率 × N） ===
    float stress_ruin_prob_high;
    float stress_ruin_prob_low;
    float stress_return_p5;
    float stress_return_p50;
    float stress_max_dd_p50;
    std::vector<PathDetail> stress_worst_paths;  // 压力最差路径

    // === 流动性压力测试（尾部收益率折扣） ===
    float liq_stress_ruin_prob_high;
    float liq_stress_return_p5;
    float liq_stress_max_dd_p50;
    std::vector<PathDetail> liq_stress_worst_paths;

    // === 波动率聚集压力测试（更大 Block Size） ===
    float vol_cluster_stress_ruin_prob_high;
    float vol_cluster_stress_return_p5;
    float vol_cluster_stress_max_dd_p50;
    std::vector<PathDetail> vol_cluster_worst_paths;

    /*
     * @brief 格式化为可读字符串
     */
    std::string toString() const;
};

/*
 * @brief Bootstrap 蒙特卡洛风险分析器
 *
 * 原理：从历史净收益率序列中有放回地随机抽样，重构数万条可能的未来资金曲线，
 *       评估策略在不同市场环境下的风险特征。
 *
 * 抽样策略：
 *   - 标准 Bootstrap：独立抽样（适用于无自相关的收益率序列）
 *   - Block Bootstrap：抽取连续块（适用于存在动量/反转效应的序列）
 *
 * 压力测试：
 *   - 基础压力：波动率 × N
 *   - 流动性压力：尾部最差收益率额外折扣（模拟流动性枯竭）
 *   - 波动率聚集：更大 Block Size（模拟亏损持续更久）
 *
 * 注意：传入的收益率序列应已经是扣除佣金、印花税、滑点后的净收益率。
 */
class MonteCarloSimulator {
public:
    MonteCarloSimulator() = default;

    /*
     * @brief 初始化配置
     */
    bool Init(const McSimConfig& config);

    /*
     * @brief 喂入净收益率序列（已扣除佣金、印花税、滑点）
     */
    void FeedReturns(const std::vector<double>& net_returns);

    /*
     * @brief 执行蒙特卡洛模拟
     * @param n_simulations 模拟次数（-1 使用配置中的默认值）
     */
    McResult Run(int n_simulations = -1);

private:
    struct PathResult {
        double total_return;
        double max_drawdown;
        bool ruined_high;
        bool ruined_low;
        double win_rate;
        int longest_win_streak;
        int longest_loss_streak;
        size_t max_dd_bar_index;
        size_t peak_bar_index;
        size_t trough_bar_index;
        std::vector<double> equity_curve;
    };

    // 单条路径模拟（记录完整资金曲线和特征）
    PathResult simulatePath(const std::vector<double>& sampled_returns);

    // 从 PathResult 构建 PathDetail
    PathDetail buildDetail(const PathResult& p) const;

    // 标准 Bootstrap 抽样
    std::vector<double> sampleStandardBootstrap();

    // Block Bootstrap 抽样
    std::vector<double> sampleBlockBootstrap(int block_size);

    // === 预分配版本（写入外部缓冲区，避免循环内分配）===
    void sampleStandardBootstrapInto(std::vector<double>& out);
    void sampleStandardBootstrapFromInto(const std::vector<double>& source, std::vector<double>& out);
    void sampleBlockBootstrapInto(std::vector<double>& out, int block_size);
    void sampleBlockBootstrapFromInto(const std::vector<double>& source, std::vector<double>& out, int block_size);

    // 波动率压力：构建压力收益率池
    std::vector<double> buildStressReturns(double vol_factor) const;
    void buildStressReturnsInto(std::vector<double>& out, double vol_factor) const;

    // 汇总结果（含双向极端路径）
    McResult aggregateResults(
        std::vector<PathResult>& paths,
        int method, int block_size, float acf
    );

    // 压力测试汇总（含最差路径）
    void aggregateStressResults(
        std::vector<PathResult>& paths,
        float& out_ruin_prob_high,
        float& out_ruin_prob_low,
        float& out_return_p5,
        float& out_return_p50,
        float& out_max_dd_p50,
        std::vector<PathDetail>& out_worst_paths,
        int worst_count
    );

    // 自相关检验
    static double computeAutocorrelation(const std::vector<double>& returns, int lag = 1);

    // 自适应 Block 大小
    int computeBlockSize(int n_bars) const;

    // 计算原始收益率序列的标准差
    double computeOriginalStd() const;

private:
    McSimConfig _config;
    std::vector<double> _returns;
    int _nBars = 0;
    bool _useBlockBootstrap = false;
    int _blockSize = 0;
    double _acf = 0.0;
    double _originalStd = 0.0;  // 原始序列标准差
    std::mt19937 _rng;
    double _ruinThresholdHigh = 0.50;
    double _ruinThresholdLow = 0.30;

    // === 预分配缓冲区（避免循环内重复分配）===
    std::vector<double> _sampledBuf;         // Bootstrap 采样输出缓冲区
    std::vector<double> _stressReturnsBuf;   // 压力测试收益率池
    std::uniform_int_distribution<size_t> _distIdx;       // 复用 distribution
    std::uniform_int_distribution<size_t> _distStart;     // Block Bootstrap 起始位置
};
