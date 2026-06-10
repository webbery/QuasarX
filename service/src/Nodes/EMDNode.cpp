#include "Nodes/EMDNode.h"
#include "Algorithms/EMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Algorithms/VMD.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"

EMDNode::EMDNode(Server* server)
    : _server(server), _method(EMDMethod::EMD), _numIMFs(5), _ensembles(50), _noiseStd(0.2),
      _alpha(2000.0), _tau(0.0), _tol(1e-6) {}

bool EMDNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    // 解析算法类型
    String methodStr = config["params"]["method"]["value"].get<String>();
    boost::algorithm::to_lower(methodStr);
    if (methodStr == "ceemdan") {
        _method = EMDMethod::CEEMDAN;
    } else {
        _method = EMDMethod::EMD;
    }

    _numIMFs = config["params"]["numIMFs"]["value"].get<int>();
    if (_numIMFs < 1 || _numIMFs > 20) {
        throw std::runtime_error("EMD numIMFs must be between 1 and 20, got: " + std::to_string(_numIMFs));
    }

    // CEEMDAN 专属参数
    if (_method == EMDMethod::CEEMDAN) {
        _ensembles = config["params"]["ensembles"]["value"].get<int>();
        if (_ensembles < 10 || _ensembles > 200) {
            throw std::runtime_error("CEEMDAN ensembles must be between 10 and 200, got: " + std::to_string(_ensembles));
        }
        _noiseStd = config["params"]["noiseStd"]["value"].get<double>();
        if (_noiseStd < 0.01 || _noiseStd > 1.0) {
            throw std::runtime_error("CEEMDAN noiseStd must be between 0.01 and 1.0, got: " + std::to_string(_noiseStd));
        }
    }

    // VMD 专属参数
    if (_method == EMDMethod::VMD) {
        _alpha = config["params"]["alpha"]["value"].get<double>();
        if (_alpha < 100 || _alpha > 10000) {
            throw std::runtime_error("VMD alpha must be between 100 and 10000, got: " + std::to_string(_alpha));
        }
        _tau = config["params"]["tau"]["value"].get<double>();
        if (_tau < 0.0 || _tau > 1.0) {
            throw std::runtime_error("VMD tau must be between 0 and 1, got: " + std::to_string(_tau));
        }
        _tol = config["params"]["tol"]["value"].get<double>();
        if (_tol < 1e-9 || _tol > 1e-3) {
            throw std::runtime_error("VMD tol must be between 1e-9 and 1e-3, got: " + std::to_string(_tol));
        }
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

    String methodName;
    switch (_method) {
        case EMDMethod::EMD:     methodName = "EMD"; break;
        case EMDMethod::CEEMDAN: methodName = "CEEMDAN"; break;
        case EMDMethod::VMD:     methodName = "VMD"; break;
    }
    INFO("EMDNode initialized: label={}, method={}, numIMFs={}, inputs={}",
         _label, methodName, _numIMFs, _params.size());
    return true;
}

NodeProcessResult EMDNode::Process(const String& strategy, DataContext& context) {
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

        List<Vector<double>> imfs;

        if (_method == EMDMethod::VMD) {
            // VMD 分解
            VMD vmd;
            VMD::Config cfg;
            cfg.K = _numIMFs;
            cfg.alpha = _alpha;
            cfg.tau = _tau;
            cfg.tol = _tol;

            auto vresult = vmd.decompose(input_data, cfg);
            imfs = vresult.imfs;

            INFO("VMD: input={} {} IMFs, iter={}, eps={:.2e} converged={}",
                 inputKey, vresult.actualK, vresult.iterations,
                 vresult.convergenceError, vresult.converged);
        } else if (_method == EMDMethod::CEEMDAN) {
            // CEEMDAN 分解
            CEEMDAN ceemdan;
            CEEMDAN::Config cfg;
            cfg.numIMFs = _numIMFs;
            cfg.ensembles = _ensembles;
            cfg.noiseStd = _noiseStd;
            cfg.seed = 42;  // 固定种子保证可复现

            auto result = ceemdan.decompose(input_data, cfg);
            imfs = result.imfs;

            INFO("CEEMDAN: input={} {} IMFs, recon_err={:.2e}",
                 inputKey, result.actualIMFs, result.reconstructionError);
        } else {
            // 标准 EMD 分解
            EMD emd_algo;
            imfs = emd_algo.emd(input_data, _numIMFs);
        }

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
        {"method", {
            {"type", "select"},
            {"default", "emd"},
            {"options", {
                {{"label", "EMD (标准)"}},
                {{"label", "CEEMDAN (完备集合)"}},
                {{"label", "VMD (变分)"}}
            }}
        }},
        {"numIMFs", {{"type", "number"}, {"default", 5}, {"min", 1}, {"max", 20}}},
        {"ensembles", {{"type", "number"}, {"default", 50}, {"min", 10}, {"max", 200},
                       {"dependsOn", "method"}, {"dependsValue", "ceemdan"}}},
        {"noiseStd", {{"type", "number"}, {"default", 0.2}, {"min", 0.01}, {"max", 1.0},
                      {"dependsOn", "method"}, {"dependsValue", "ceemdan"}}},
        {"alpha", {{"type", "number"}, {"default", 2000}, {"min", 100}, {"max", 10000},
                   {"dependsOn", "method"}, {"dependsValue", "vmd"},
                   {"description", "带宽惩罚参数: 大=窄带, 小=宽带"}}},
        {"tau", {{"type", "number"}, {"default", 0}, {"min", 0}, {"max", 1}, "step", 0.1,
                 {"dependsOn", "method"}, {"dependsValue", "vmd"},
                 {"description", "对偶上升步长: 0=严格重构, 大=容忍噪声"}}},
        {"tol", {{"type", "number"}, {"default", 0.000001}, {"min", 1e-9}, {"max", 1e-3},
                 {"dependsOn", "method"}, {"dependsValue", "vmd"},
                 {"description", "收敛阈值: 越小越精确但迭代越多"}}}
    };
}
