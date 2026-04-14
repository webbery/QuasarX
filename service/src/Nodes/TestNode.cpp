#include "Nodes/TestNode.h"
#include "DataContext.h"
#include "Util/datetime.h"
#include "Util/log.h"

TestNode::TestNode(Server* server)
{
}

TestNode::~TestNode() {
}

const nlohmann::json TestNode::getParams() {
    nlohmann::json params;
    params["shortPeriod"] = {
        {"value", 5},
        {"type", "number"},
        {"desc", "短期均线周期"}
    };
    params["longPeriod"] = {
        {"value", 15},
        {"type", "number"},
        {"desc", "长期均线周期"}
    };
    return params;
}

bool TestNode::Init(const nlohmann::json& config) {
    // 获取参数
    if (config.contains("params")) {
        auto& params = config["params"];
        if (params.contains("shortPeriod")) {
            _shortPeriod = params["shortPeriod"].value("value", 5);
        }
        if (params.contains("longPeriod")) {
            _longPeriod = params["longPeriod"].value("value", 15);
        }
    }

    // 收集输入数据键，使用 Set 避免重复
    Set<String> codeSet;
    for (auto& item: _ins) {
        auto& in_name = item.first;
        auto in_node = item.second;
        auto outs = in_node->out_elements();
        for (auto& out: outs) {
            INFO("Out {}", out.first);
            _input_keys.insert(out.first);

            // 从 key 中提取 symbol 前缀，例如 "sz.000001.close" -> "sz.000001"
            size_t last_dot = out.first.find_last_of('.');
            String symbol_prefix = (last_dot != String::npos) ? out.first.substr(0, last_dot) : out.first;
            codeSet.insert(symbol_prefix);
        }
    }

    // 转换为 Vector
    _codes.assign(codeSet.begin(), codeSet.end());

    // 初始化每个标的的均线状态
    for (const auto& code : _codes) {
        MaState state;
        _maStates[code] = std::move(state);
    }

    return true;
}

double TestNode::calcMa(double value, Vector<double>& buffer, double& sum, int period) {
    // 维持一个固定大小的buffer，模拟滑动窗口
    if (buffer.size() < period) {
        buffer.push_back(value);
        sum += value;
        if (buffer.size() == period) {
            return sum / period;
        }
        return std::nan("nan");  // 数据不足时返回 NaN
    }

    // 替换最旧的值
    sum -= buffer[0];
    buffer.erase(buffer.begin());
    buffer.push_back(value);
    sum += value;

    return sum / period;
}

NodeProcessResult TestNode::Process(const String& strategy, DataContext& context) {
    // 遍历所有输入键（收盘价序列）
    for (auto& key: _input_keys) {
        auto& closes = context.get<Vector<double>>(key);

        // 提取 symbol 前缀
        size_t last_dot = key.find_last_of('.');
        String symbol_prefix = (last_dot != String::npos) ? key.substr(0, last_dot) : key;

        auto& state = _maStates[symbol_prefix];

        // 清空之前的数据，重新计算
        state.shortMa.clear();
        state.longMa.clear();
        state.signals.clear();

        // 滑动窗口计算均线
        Vector<double> shortBuffer;
        Vector<double> longBuffer;
        double shortSum = 0.0;
        double longSum = 0.0;

        // 第一步：计算每一时刻的均线值
        for (size_t i = 0; i < closes.size(); ++i) {
            double price = closes[i];

            // 短期均线
            double shortMa = std::nan("nan");
            if (shortBuffer.size() < (size_t)_shortPeriod) {
                shortBuffer.push_back(price);
                shortSum += price;
                if (shortBuffer.size() == (size_t)_shortPeriod) {
                    shortMa = shortSum / _shortPeriod;
                }
            } else {
                shortSum -= shortBuffer[0];
                shortBuffer.erase(shortBuffer.begin());
                shortBuffer.push_back(price);
                shortSum += price;
                shortMa = shortSum / _shortPeriod;
            }

            // 长期均线
            double longMa = std::nan("nan");
            if (longBuffer.size() < (size_t)_longPeriod) {
                longBuffer.push_back(price);
                longSum += price;
                if (longBuffer.size() == (size_t)_longPeriod) {
                    longMa = longSum / _longPeriod;
                }
            } else {
                longSum -= longBuffer[0];
                longBuffer.erase(longBuffer.begin());
                longBuffer.push_back(price);
                longSum += price;
                longMa = longSum / _longPeriod;
            }

            state.shortMa.push_back(shortMa);
            state.longMa.push_back(longMa);
        }

        // 第二步：根据均线序列判断金叉死叉
        for (size_t i = 0; i < state.shortMa.size(); ++i) {
            int signal = 0;
            if (i > 0) {
                double currShortMa = state.shortMa[i];
                double prevShortMa = state.shortMa[i-1];
                double currLongMa = state.longMa[i];
                double prevLongMa = state.longMa[i-1];

                // 检查是否有效值（非 NaN）
                if (!std::isnan(currShortMa) && !std::isnan(prevShortMa) &&
                    !std::isnan(currLongMa) && !std::isnan(prevLongMa)) {
                    // 金叉：短期均线上穿长期均线
                    if (currShortMa > currLongMa && prevShortMa <= prevLongMa) {
                        signal = 1;  // 买入
                    }
                    // 死叉：短期均线下穿长期均线
                    else if (currShortMa < currLongMa && prevShortMa >= prevLongMa) {
                        signal = -1;  // 卖出
                    }
                }
            }
            state.signals.push_back(signal);
        }

        // 将均线数据存入 context
        String shortMaKey = symbol_prefix + ".ma_short";
        String longMaKey = symbol_prefix + ".ma_long";
        String signalKey = symbol_prefix + ".signal";

        context.set(shortMaKey, state.shortMa);
        context.set(longMaKey, state.longMa);
        context.set(signalKey, state.signals);

        // 打印最后几个信号用于调试
        if (state.signals.size() > 0) {
            int lastSignal = state.signals.back();
            double lastShortMa = state.shortMa.back();
            double lastLongMa = state.longMa.back();
            INFO("{} signal={}, short_ma={:.2f}, long_ma={:.2f}",
                 symbol_prefix, lastSignal, lastShortMa, lastLongMa);
        }
    }
    return NodeProcessResult::Success;
}

Map<String, ArgType> TestNode::out_elements() {
    Map<String, ArgType> elems;
    // 输出均线和信号数据
    for (const auto& code : _codes) {
        // 均线是时间序列
        elems[code + ".ma_short"] = ArgType::Double_TimeSeries;
        elems[code + ".ma_long"] = ArgType::Double_TimeSeries;
        // 信号是整数时间序列
        elems[code + ".signal"] = ArgType::Integer_TimeSeries;
    }
    return elems;
}