#include "Nodes/ResampleNode.h"
#include "StrategyNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"

ResampleNode::ResampleNode(Server* server) : _server(server) {}

ResampleNode::~ResampleNode() {}

bool ResampleNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    // 解析目标频率
    if (config.contains("params") && config["params"].contains("target_freq")) {
        String freq_str = (String)config["params"]["target_freq"]["value"];
        _target_freq = stringToFreq(freq_str);
    }

    // 从输入节点获取输出属性列表
    for (auto& item : _ins) {
        auto out_names = item.second->out_elements();
        for (auto& kv : out_names) {
            _input_keys.push_back(kv.first);
        }
    }

    if (_input_keys.empty()) {
        WARN("[Resample] No input features found for node {}", _label);
        return false;
    }

    // 注册输出属性（输入 key + "_resampled"）
    for (auto& key : _input_keys) {
        String out_key = key + "_resampled";
        _output_keys[out_key] = ArgType::Double_TimeSeries;
    }

    INFO("[Resample] Initialized: target_freq={}, inputs={}", 
         freqToString(_target_freq), _input_keys.size());
    return true;
}

NodeProcessResult ResampleNode::Process(const String& strategy, DataContext& context) {
    if (_input_keys.empty()) return NodeProcessResult::Skip;

    // 获取当前时间戳
    const auto& times = context.GetTime();
    if (times.empty()) return NodeProcessResult::Skip;
    
    time_t currentTime = times.back();

    // 计算当前 bar 起始时间
    time_t currentBarStart = calcBarStart(currentTime);

    // 检测是否进入新 bar
    bool newBar = isNewBar(currentTime);

    // 收集当前时刻所有输入属性的值
    Map<String, double> currentValues;
    for (auto& key : _input_keys) {
        try {
            const auto& vec = context.get<Vector<double>>(key);
            if (vec.empty()) return NodeProcessResult::Skip;
            currentValues[key] = vec.back();
        } catch (...) {
            return NodeProcessResult::Skip;
        }
    }

    // 如果是新 bar 且上一个 bar 有数据，先输出上一个 bar
    if (newBar && _accumulator.has_data && _initialized) {
        emitBar(context);
        resetAccumulator(currentBarStart);
    }

    // 初始化第一个 bar
    if (!_initialized) {
        resetAccumulator(currentBarStart);
        _initialized = true;
    }

    // 更新累加器
    updateAccumulator(currentValues);

    // 如果是新 bar 的开始（但不是第一个 bar），返回 Success 表示有输出
    if (newBar && _accumulator.tick_count == 1) {
        return NodeProcessResult::Success;
    }

    // 否则仍在累积中，返回 Skip
    return NodeProcessResult::Skip;
}

Map<String, ArgType> ResampleNode::out_elements() {
    return _output_keys;
}

void ResampleNode::UpdateLabel(const String& label) {
    if (_label != label) {
        Map<String, ArgType> new_outputs;
        for (auto& item : _output_keys) {
            String name = item.first;
            boost::algorithm::replace_all(name, _label, label);
            new_outputs[name] = item.second;
        }
        _output_keys.swap(new_outputs);
        _label = label;
    }
}

// === 私有方法实现 ===

time_t ResampleNode::calcBarStart(time_t tickTime) const {
    int64_t interval = barInterval();
    return tickTime - (tickTime % interval);
}

int64_t ResampleNode::barInterval() const {
    switch (_target_freq) {
        case BarFreq::Min1:   return 60;
        case BarFreq::Min5:   return 300;
        case BarFreq::Min15:  return 900;
        case BarFreq::Min30:  return 1800;
        case BarFreq::Hour1:  return 3600;
        case BarFreq::Hour2:  return 7200;
        case BarFreq::Hour4:  return 14400;
        case BarFreq::Day:    return 86400;
        default:              return 3600;
    }
}

bool ResampleNode::isNewBar(time_t tickTime) const {
    if (!_initialized) return false;
    time_t currentBarStart = calcBarStart(tickTime);
    return currentBarStart > _lastBarStart;
}

void ResampleNode::resetAccumulator(time_t barStart) {
    _accumulator.bar_start = barStart;
    _accumulator.first_values.clear();
    _accumulator.max_values.clear();
    _accumulator.min_values.clear();
    _accumulator.last_values.clear();
    _accumulator.sum_values.clear();
    _accumulator.tick_count = 0;
    _accumulator.has_data = false;
    _lastBarStart = barStart;
}

void ResampleNode::updateAccumulator(const Map<String, double>& values) {
    if (_accumulator.tick_count == 0) {
        // 第一个 tick：初始化所有值
        for (auto& [key, val] : values) {
            _accumulator.first_values[key] = val;
            _accumulator.max_values[key] = val;
            _accumulator.min_values[key] = val;
            _accumulator.last_values[key] = val;
            _accumulator.sum_values[key] = val;
        }
    } else {
        // 后续 tick：更新 OHLCV
        for (auto& [key, val] : values) {
            // 更新 high
            if (val > _accumulator.max_values[key]) {
                _accumulator.max_values[key] = val;
            }
            // 更新 low
            if (val < _accumulator.min_values[key]) {
                _accumulator.min_values[key] = val;
            }
            // 更新 close
            _accumulator.last_values[key] = val;
            // 累加 volume
            _accumulator.sum_values[key] += val;
        }
    }
    _accumulator.tick_count++;
    _accumulator.has_data = true;
}

void ResampleNode::emitBar(DataContext& context) {
    if (!_accumulator.has_data || _accumulator.tick_count == 0) return;

    for (auto& key : _input_keys) {
        String out_key = key + "_resampled";

        // 根据属性类型选择聚合方式
        double value;
        if (isVolumeLike(key)) {
            // volume 类：求和
            value = _accumulator.sum_values[key];
        } else {
            // price 类：使用 close（最后一个值）
            // 也可以根据需要改为 open/high/low
            value = _accumulator.last_values[key];
        }

        // 写入 context
        if (context.exist(out_key)) {
            context.add(out_key, value);
        } else {
            Vector<double> ts;
            ts.push_back(value);
            context.set(out_key, ts);
        }
    }
}

bool ResampleNode::isVolumeLike(const String& key) const {
    // 判断是否为 volume 类属性（包含 volume 关键字）
    return key.find("volume") != String::npos || 
           key.find("vol") != String::npos;
}

const char* ResampleNode::freqToString(BarFreq freq) {
    switch (freq) {
        case BarFreq::Min1:   return "1m";
        case BarFreq::Min5:   return "5m";
        case BarFreq::Min15:  return "15m";
        case BarFreq::Min30:  return "30m";
        case BarFreq::Hour1:  return "1h";
        case BarFreq::Hour2:  return "2h";
        case BarFreq::Hour4:  return "4h";
        case BarFreq::Day:    return "1d";
        default:              return "unknown";
    }
}

BarFreq ResampleNode::stringToFreq(const String& str) {
    if (str == "1m")   return BarFreq::Min1;
    if (str == "5m")   return BarFreq::Min5;
    if (str == "15m")  return BarFreq::Min15;
    if (str == "30m")  return BarFreq::Min30;
    if (str == "1h")   return BarFreq::Hour1;
    if (str == "2h")   return BarFreq::Hour2;
    if (str == "4h")   return BarFreq::Hour4;
    if (str == "1d")   return BarFreq::Day;
    
    WARN("[Resample] Unknown frequency: {}, default to 1h", str);
    return BarFreq::Hour1;
}

const nlohmann::json ResampleNode::getParams() {
    return nlohmann::json::object();
}
