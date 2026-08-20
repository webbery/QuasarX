#include "Nodes/CUSUMNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"

CUSUMNode::CUSUMNode(Server* server)
    : _server(server), _mode(CUSUMMode::ChangePoint),
      _config{}, _cooldownCounter(0) {}

bool CUSUMNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    // 解析模式
    if (config.contains("params")) {
        auto& p = config["params"];
        String mode_str = p.contains("mode") ? (String)p["mode"]["value"] : "changepoint";
        boost::algorithm::to_lower(mode_str);

        if (mode_str == "momentum") _mode = CUSUMMode::Momentum;
        else if (mode_str == "meanrevert") _mode = CUSUMMode::MeanRevert;
        else if (mode_str == "asset") _mode = CUSUMMode::Asset;
        else if (mode_str == "consensus") _mode = CUSUMMode::Consensus;

        if (p.contains("lambda")) _config._lambda = (double)p["lambda"]["value"];
        if (p.contains("threshold_multiplier")) _config._threshold_multiplier = (double)p["threshold_multiplier"]["value"];
        if (p.contains("min_obs")) _config._min_obs = (size_t)(int)p["min_obs"]["value"];
        if (p.contains("mu")) _config._mu = (double)p["mu"]["value"];
        if (p.contains("sigma")) _config._sigma = (double)p["sigma"]["value"];
        if (p.contains("cooldown")) _cooldownDays = (int)p["cooldown"]["value"];
        if (p.contains("consensus_threshold")) _consensusThreshold = (int)p["consensus_threshold"]["value"];
    }

    // 从输入节点获取输入数据名
    for (auto& item : _ins) {
        auto input_names = item.second->out_elements();
        _params.merge(input_names);
    }

    // BFS 上游发现 symbol 列表
    auto symbolSet = discoverUpstreamSymbols();
    if (symbolSet.empty()) {
        WARN("[CUSUM:{}] No upstream symbols found", _id);
        return false;
    }

    // 为每个 symbol 创建独立 detector
    for (auto& sym : symbolSet) {
        String symStr = get_symbol(sym);
        _assetSymbols.insert(symStr);
        _assetDetectors[symStr] = std::make_unique<CUSUMDetector>(_config);
        _assetLastSignals[symStr] = 0.0;
    }

    INFO("[CUSUM:{}] {} symbols: {}", _id, _assetSymbols.size(),
         boost::algorithm::join(_assetSymbols, ", "));

    // 注册 per-symbol 输出: {symbol}.signalLabel.field
    for (auto& symStr : _assetSymbols) {
        _outputs[symStr + "." + _label + ".signal"] = ArgType::Double_TimeSeries;
        _outputs[symStr + "." + _label + ".drift"] = ArgType::Double_TimeSeries;
        _outputs[symStr + "." + _label + ".s_pos"] = ArgType::Double_TimeSeries;
        _outputs[symStr + "." + _label + ".s_neg"] = ArgType::Double_TimeSeries;
    }

    if (_mode == CUSUMMode::Asset || _mode == CUSUMMode::Consensus) {
        _outputs[_label + ".asset_results"] = ArgType::Double_TimeSeries;
        _outputs[_label + ".consensus_count"] = ArgType::Integer_Scalar;
    }

    String mode_name;
    switch (_mode) {
        case CUSUMMode::ChangePoint: mode_name = "ChangePoint"; break;
        case CUSUMMode::Momentum:    mode_name = "Momentum"; break;
        case CUSUMMode::MeanRevert:  mode_name = "MeanRevert"; break;
        case CUSUMMode::Asset:       mode_name = "Asset"; break;
        case CUSUMMode::Consensus:   mode_name = "Consensus"; break;
    }

    INFO("[CUSUM] Initialized: label={}, mode={}, lambda={}, threshold={}, min_obs={}",
         _label, mode_name, _config._lambda, _config._threshold_multiplier, _config._min_obs);
    return true;
}

NodeProcessResult CUSUMNode::Process(const String& strategy, DataContext& context) {
    if (_mode == CUSUMMode::Asset || _mode == CUSUMMode::Consensus) {
        return ProcessMultiAsset(strategy, context);
    }
    return ProcessSingleAsset(strategy, context);
}

NodeProcessResult CUSUMNode::ProcessSingleAsset(const String& strategy, DataContext& context) {
    bool anySuccess = false;

    for (auto& sym : _assetSymbols) {
        // 从 _params 中找到该 symbol 的输入 key（如 sz.000423.ret1）
        String retKey;
        for (auto& kv : _params) {
            if (kv.first.find(sym + ".") == 0) {
                retKey = kv.first;
                break;
            }
        }
        if (retKey.empty()) continue;

        // 读取该 symbol 的收益率
        double ret = 0.0;
        bool has_return = false;
        try {
            const auto& vec = context.get<Vector<double>>(retKey);
            if (!vec.empty()) {
                ret = vec.back();
                has_return = true;
            }
        } catch (...) {
            continue;
        }
        if (!has_return) continue;

        // 冷却期检查
        if (_cooldownCounter > 0) continue;

        auto it = _assetDetectors.find(sym);
        if (it == _assetDetectors.end()) continue;

        // 更新该 symbol 的 CUSUM detector
        auto result = it->second->update(ret);

        bool triggered = result._change_point;
        bool s_pos_triggered = triggered && (result._cusum_positive > 0);
        bool s_neg_triggered = triggered && (result._cusum_negative > 0);
        double signal = InterpretSignal(triggered, s_pos_triggered, s_neg_triggered);

        // 触发后启动冷却
        if (triggered && _cooldownDays > 0) {
            _cooldownCounter = _cooldownDays;
        }

        // 写入 per-symbol 输出（时间序列）
        String prefix = sym + "." + _label + ".";

        // 首次 set 创建 Vector<double>，后续 add 追加
        if (!context.exist(prefix + "signal")) {
            context.set<Vector<double>>(prefix + "signal", {signal});
            context.set<Vector<double>>(prefix + "s_pos", {result._cusum_positive});
            context.set<Vector<double>>(prefix + "s_neg", {result._cusum_negative});
            context.set<Vector<double>>(prefix + "drift", {result._current_drift});
        } else {
            context.add(prefix + "signal", signal);
            context.add(prefix + "s_pos", result._cusum_positive);
            context.add(prefix + "s_neg", result._cusum_negative);
            context.add(prefix + "drift", result._current_drift);
        }

        _assetLastSignals[sym] = signal;
        anySuccess = true;
    }

    return anySuccess ? NodeProcessResult::Success : NodeProcessResult::Skip;
}

NodeProcessResult CUSUMNode::ProcessMultiAsset(const String& strategy, DataContext& context) {
    int consensus_count = 0;
    nlohmann::json asset_results = nlohmann::json::array();

    for (auto& sym : _assetSymbols) {
        // 从 DataContext 读取特定标的的收益率：{symbol}.return
        String ret_key = sym + ".return";
        double ret = 0.0;
        bool has_return = false;

        auto it = _assetDetectors.find(sym);
        if (it == _assetDetectors.end()) continue;

        try {
            const auto& vec = context.get<Vector<double>>(ret_key);
            if (!vec.empty()) {
                ret = vec.back();
                has_return = true;
            }
        } catch (...) {
            continue;
        }

        if (!has_return) continue;

        auto result = it->second->update(ret);

        bool triggered = result._change_point;
        bool s_pos_triggered = triggered && (result._cusum_positive > 0);
        bool s_neg_triggered = triggered && (result._cusum_negative > 0);
        double signal = InterpretSignal(triggered, s_pos_triggered, s_neg_triggered);

        _assetLastSignals[sym] = signal;

        nlohmann::json asset_json;
        asset_json["symbol"] = sym;
        asset_json["signal"] = signal;
        asset_json["triggered"] = triggered;
        asset_json["s_pos"] = result._cusum_positive;
        asset_json["s_neg"] = result._cusum_negative;
        asset_json["change_points"] = it->second->get_total_change_points();
        asset_results.push_back(asset_json);

        if (triggered) {
            ++consensus_count;
        }
    }

    if (asset_results.empty()) {
        return NodeProcessResult::Skip;
    }

    // Consensus 模式：判断是否达到阈值
    bool global_triggered = (consensus_count >= _consensusThreshold);
    double global_signal = global_triggered ? 1.0 : 0.0;

    context.set<double>(_label + ".signal", global_signal);
    context.set<bool>(_label + ".triggered", global_triggered);
    context.set<double>(_label + ".s_pos", 0.0);
    context.set<double>(_label + ".s_neg", 0.0);
    context.set<double>(_label + ".drift", 0.0);
    context.set<uint64_t>(_label + ".change_points", (uint64_t)consensus_count);
    context.set<String>(_label + ".asset_results", asset_results.dump());
    context.set<uint64_t>(_label + ".consensus_count", (uint64_t)consensus_count);

    return NodeProcessResult::Success;
}

double CUSUMNode::InterpretSignal(bool triggered, bool s_pos_triggered, bool s_neg_triggered) {
    switch (_mode) {
        case CUSUMMode::ChangePoint:
            return triggered ? 1.0 : 0.0;

        case CUSUMMode::Momentum:
            if (s_pos_triggered) return +1.0;  // 上涨趋势
            if (s_neg_triggered) return -1.0;  // 下跌趋势
            return 0.0;

        case CUSUMMode::MeanRevert:
            if (s_pos_triggered) return -1.0;  // 超买→卖
            if (s_neg_triggered) return +1.0;  // 超卖→买
            return 0.0;

        case CUSUMMode::Asset:
            if (s_pos_triggered) return +1.0;
            if (s_neg_triggered) return -1.0;
            return 0.0;

        case CUSUMMode::Consensus:
            return triggered ? 1.0 : 0.0;

        default:
            return 0.0;
    }
}

Map<String, ArgType> CUSUMNode::out_elements() {
    return _outputs;
}

void CUSUMNode::UpdateLabel(const String& label) {
    _label = label;
}

const nlohmann::json CUSUMNode::getParams() {
    return {
        {"mode", {
            {"type", "select"},
            {"default", "changepoint"},
            {"options", {
                {{"label", "ChangePoint (变点检测)"}},
                {{"label", "Momentum (趋势跟踪)"}},
                {{"label", "MeanRevert (均值反转)"}},
                {{"label", "Asset (逐资产)"}},
                {{"label", "Consensus (多资产共识)"}}
            }}
        }},
        {"lambda", {{"type", "number"}, {"default", 0.5}, {"min", 0.0}, {"max", 2.0}, "step", 0.1,
                     {"description", "容许偏差倍数 k = λ × σ"}}},
        {"threshold_multiplier", {{"type", "number"}, {"default", 4.0}, {"min", 0.5}, {"max", 10.0}, "step", 0.5,
                                   {"description", "阈值倍数 h = threshold × σ × √N"}}},
        {"min_obs", {{"type", "number"}, {"default", 30}, {"min", 5}, {"max", 252},
                      {"description", "最少观测数，低于此值不触发变点"}}},
        {"mu", {{"type", "number"}, {"default", 0.0}, "step", 0.01,
                 {"description", "预期均值（通常取训练期均值或 0）"}}},
        {"sigma", {{"type", "number"}, {"default", 1.0}, {"min", 0.01}, "step", 0.01,
                    {"description", "预期波动率（用于计算 k 和 h）"}}},
        {"cooldown", {{"type", "number"}, {"default", 0}, {"min", 0}, {"max", 60},
                       {"description", "触发后冷却天数（0 表示不冷却）"}}},
        {"consensus_threshold", {{"type", "number"}, {"default", 2}, {"min", 1}, {"max", 20},
                                  {"dependsOn", "mode"}, {"dependsValue", "consensus"},
                                  {"description", "共识触发最少资产数"}}},
    };
}
