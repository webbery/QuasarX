#pragma once
#include "StrategyNode.h"
#include "Metric/CUSUMDetector.h"
#include "std_header.h"

/**
 * CUSUM 累积和变点检测节点
 *
 * 5 种模式：
 *   - ChangePoint: 结构变化检测（触发=1，否则=0）
 *   - Momentum: 趋势跟踪（S+触发=+1，S-触发=-1）
 *   - MeanRevert: 均值反转（S+触发=-1，S-触发=+1）
 *   - Asset: 逐资产独立检测
 *   - Consensus: 多资产共识（≥K 资产同向触发 → 系统性信号）
 *
 * 输入：从 FunctionNode(Return) 读取收益率
 *   - 单资产：return
 *   - 多资产：{symbol}.return
 *
 * 输出（嵌套格式）：
 *   {label}.signal, {label}.triggered, {label}.s_pos, {label}.s_neg,
 *   {label}.drift, {label}.change_points, {label}.asset_results, {label}.consensus_count
 */

enum class CUSUMMode {
    ChangePoint,
    Momentum,
    MeanRevert,
    Asset,
    Consensus,
};

class CUSUMNode : public QNode {
public:
    RegistClassName(CUSUMNode);
    CUSUMNode(Server* server);

    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

private:
    // 单资产模式处理
    NodeProcessResult ProcessSingleAsset(const String& strategy, DataContext& context);
    // 多资产模式处理
    NodeProcessResult ProcessMultiAsset(const String& strategy, DataContext& context);

    // 根据模式解释信号
    double InterpretSignal(bool triggered, bool s_pos_triggered, bool s_neg_triggered);

    Server* _server;
    String _label;
    CUSUMMode _mode = CUSUMMode::ChangePoint;
    CUSUMConfig _config;
    String _signalLabel = "cusum_signal";
    int _cooldownDays = 0;
    int _consensusThreshold = 2;

    // Detector 管理
    std::unique_ptr<CUSUMDetector> _singleDetector;
    Map<String, std::unique_ptr<CUSUMDetector>> _assetDetectors;

    // 状态管理
    int _cooldownCounter = 0;
    Set<String> _assetSymbols;
    Map<String, double> _assetLastSignals;

    Map<String, ArgType> _params;
    Map<String, ArgType> _outputs;
};
