#pragma once
#include "std_header.h"
#include "StrategyNode.h"
#include "KBarBuilder.h"

/**
 * 数据重采样节点
 * 
 * 将高频数据（秒级/分钟级）重采样为低频数据（小时/日级）。
 * 支持 OHLCV 标准聚合：
 * - open: 第一个 bar 的值
 * - high: 所有 bar 的最大值
 * - low: 所有 bar 的最小值
 * - close: 最后一个 bar 的值
 * - volume: 所有 bar 的求和
 * 
 * 使用场景：
 * - 1min 数据 → 1h bar → HMM 趋势判断
 * - 5min 数据 → 4h bar → 多时间框架分析
 * 
 * 输出命名规则：输入 key + "_resampled"（避免与上游冲突）
 */
class ResampleNode : public QNode {
public:
    RegistClassName(ResampleNode);
    static const nlohmann::json getParams();

    ResampleNode(Server* server);
    ~ResampleNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

private:
    /**
     * @brief 根据目标频率计算 bar 起始时间
     */
    time_t calcBarStart(time_t tickTime) const;

    /**
     * @brief 获取 bar 间隔（秒）
     */
    int64_t barInterval() const;

    /**
     * @brief 判断当前 tick 是否进入新 bar
     */
    bool isNewBar(time_t tickTime) const;

    /**
     * @brief 重置 bar 累加器
     */
    void resetAccumulator(time_t barStart);

    /**
     * @brief 更新 OHLCV 累加器
     */
    void updateAccumulator(const Map<String, double>& values);

    /**
     * @brief 输出聚合后的 bar 数据到 context
     */
    void emitBar(DataContext& context);

    /**
     * @brief 频率枚举转字符串
     */
    static const char* freqToString(BarFreq freq);

    /**
     * @brief 字符串转频率枚举
     */
    static BarFreq stringToFreq(const String& str);

    /**
     * @brief 判断是否为 volume 类属性（需要求和而非 OHLC）
     */
    bool isVolumeLike(const String& key) const;

private:
    Server* _server;
    String _label;

    // 配置参数
    BarFreq _target_freq = BarFreq::Day;

    // 输入属性列表
    Vector<String> _input_keys;

    // 输出属性列表（key → ArgType）
    Map<String, ArgType> _output_keys;

    // Bar 累加器
    struct BarAccumulator {
        time_t bar_start = 0;              // bar 起始时间
        Map<String, double> first_values;  // 第一个 bar 的值（用于 open）
        Map<String, double> max_values;    // 最大值（用于 high）
        Map<String, double> min_values;    // 最小值（用于 low）
        Map<String, double> last_values;   // 最后一个 bar 的值（用于 close）
        Map<String, double> sum_values;    // 累加值（用于 volume）
        int tick_count = 0;                // 当前 bar 累积的 tick 数
        bool has_data = false;             // 是否有数据
    };

    BarAccumulator _accumulator;

    // 上一个 bar 的起始时间（用于检测新 bar）
    time_t _lastBarStart = 0;

    // 是否已初始化
    bool _initialized = false;
};
