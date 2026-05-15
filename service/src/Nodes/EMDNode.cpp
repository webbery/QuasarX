#include "Nodes/EMDNode.h"
#include "Algorithms/EMD.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"

EMDNode::EMDNode(Server* server)
    : _server(server), _numIMFs(5) {}

bool EMDNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];
    _numIMFs = config["params"]["numIMFs"]["value"].get<int>();
    if (_numIMFs < 1 || _numIMFs > 20) {
        throw std::runtime_error("EMD numIMFs must be between 1 and 20, got: " + std::to_string(_numIMFs));
    }

    // 从输入节点获取输入数据名
    for (auto& item : _ins) {
        auto input_names = item.second->out_elements();
        _params.merge(input_names);
    }

    // 构建输出: label.IMF_0, label.IMF_1, ...
    for (auto& item : _params) {
        for (int i = 0; i < _numIMFs; ++i) {
            String outKey = _label + ".IMF_" + std::to_string(i);
            _outputs[outKey] = ArgType::Double_TimeSeries;
        }
    }

    INFO("EMDNode initialized: label={}, numIMFs={}, inputs={}", _label, _numIMFs, _params.size());
    return true;
}

NodeProcessResult EMDNode::Process(const String& strategy, DataContext& context) {
    EMD emd_algo;

    for (auto& [inputKey, argType] : _params) {
        // 从 DataContext 获取输入时间序列
        auto& value = context.get(inputKey);
        Vector<double> input_data;
        if (auto* p = std::get_if<Vector<double>>(&value)) {
            input_data = *p;
        } else {
            WARN("EMDNode input {} is not a time series", inputKey);
            return NodeProcessResult::Error;
        }

        if (input_data.size() < 10) {
            WARN("EMDNode input {} too short ({} points), need at least 10", inputKey, input_data.size());
            return NodeProcessResult::Skip;
        }

        // 执行 EMD 分解
        auto imfs = emd_algo.emd(input_data, _numIMFs);

        // 写入输出
        int idx = 0;
        for (auto& imf : imfs) {
            if (idx >= _numIMFs) break;
            String outKey = _label + ".IMF_" + std::to_string(idx);
            context.set(outKey, imf);
            ++idx;
        }
    }

    return NodeProcessResult::Success;
}

Map<String, ArgType> EMDNode::out_elements() {
    return _outputs;
}

void EMDNode::UpdateLabel(const String& label) {
    if (_label != label) {
        Map<String, ArgType> new_outputs;
        for (auto& item : _outputs) {
            String name = item.first;
            boost::algorithm::replace_all(name, _label, label);
            new_outputs[name] = item.second;
        }
        _outputs.swap(new_outputs);
        _label = label;
    }
}

const nlohmann::json EMDNode::getParams() {
    return {
        {"numIMFs", {{"type", "number"}, {"default", 5}, {"min", 1}, {"max", 20}}}
    };
}
