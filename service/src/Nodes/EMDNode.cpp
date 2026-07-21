#include "Nodes/EMDNode.h"
#include "Algorithms/EMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Algorithms/VMD.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"
#include "std_header.h"

EMDNode::EMDNode(Server* server)
    : _server(server), _method(EMDMethod::EMD), _numIMFs(5), _ensembles(50), _noiseStd(0.2),
      _alpha(2000.0), _tau(0.0), _tol(1e-6), _windowSize(0),
      _computeEnergyVelocity(false), _computeVolumeRegime(false) {}

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

    // 滚动窗口大小：0 = 全序列一次分解，>0 = 滚动窗口 EMD
    _windowSize = config["params"]["windowSize"]["value"].get<int>();
    if (_windowSize < 0 || (_windowSize > 0 && _windowSize < 30)) {
        throw std::runtime_error("EMD windowSize must be 0 (global) or >= 30, got: " + std::to_string(_windowSize));
    }

    // 衍生特征开关（由前端 JSON 控制，key 为中文 label）
    _computeEnergyVelocity = config["params"].contains("能量变化率") &&
                             !config["params"]["能量变化率"]["value"].get<String>().empty();
    _computeVolumeRegime = config["params"].contains("成交量体制") &&
                           !config["params"]["成交量体制"]["value"].get<String>().empty();

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

    // 构建基础输出: label.IMF_0, label.IMF_1, ...
    for (auto& item : _params) {
        for (int i = 0; i < _numIMFs; ++i) {
            String outKey = _label + ".IMF_" + std::to_string(i);
            _outputs[outKey] = ArgType::Double_TimeSeries;
        }
    }

    // 根据开关注册衍生特征输出
    if (_computeEnergyVelocity) {
        _outputs[_label + ".energy_velocity"] = ArgType::Double_TimeSeries;
    }
    if (_computeVolumeRegime) {
        _outputs[_label + ".volume_regime"] = ArgType::Double_TimeSeries;
    }

    String methodName;
    switch (_method) {
        case EMDMethod::EMD:     methodName = "EMD"; break;
        case EMDMethod::CEEMDAN: methodName = "CEEMDAN"; break;
        case EMDMethod::VMD:     methodName = "VMD"; break;
    }
    INFO("EMDNode initialized: label={}, method={}, numIMFs={}, window={}, inputs={}",
         _label, methodName, _numIMFs, _windowSize > 0 ? std::to_string(_windowSize) : "global",
         _params.size());
    return true;
}

// 对单个输入序列执行 EMD（全局或滚动）
bool EMDNode::decomposeOne(const Vector<double>& input_data,
                           Vector<Vector<double>>& out_imfs) const {
    const int n = static_cast<int>(input_data.size());
    if (n < 10) return false;

    // 全局模式：一次性对整个序列做 EMD
    if (_windowSize == 0) {
        if (_method == EMDMethod::VMD) {
            VMD vmd;
            VMD::Config cfg;
            cfg.K = _numIMFs;
            cfg.alpha = _alpha;
            cfg.tau = _tau;
            cfg.tol = _tol;
            auto vresult = vmd.decompose(input_data, cfg);
            out_imfs = vresult.imfs;
        } else if (_method == EMDMethod::CEEMDAN) {
            CEEMDAN ceemdan;
            CEEMDAN::Config cfg;
            cfg.numIMFs = _numIMFs;
            cfg.ensembles = _ensembles;
            cfg.noiseStd = _noiseStd;
            cfg.seed = 42;
            auto result = ceemdan.decompose(input_data, cfg);
            out_imfs = result.imfs;
        } else {
            EMD emd_algo;
            out_imfs = emd_algo.emd(input_data, _numIMFs);
        }
        return true;
    }

    // 滚动窗口模式：每次取 window 天数据做 EMD，只取最后一个 bar 的 IMF 值
    const int w = _windowSize;
    out_imfs.resize(_numIMFs, Vector<double>(n, 0.0));

    for (int i = w - 1; i < n; ++i) {
        // 取窗口内数据
        Vector<double> window_data(w);
        for (int j = 0; j < w; ++j) {
            window_data[j] = input_data[i - w + 1 + j];
        }

        // 对窗口数据做 EMD
        Vector<Vector<double>> win_imfs;
        if (_method == EMDMethod::VMD) {
            VMD vmd;
            VMD::Config cfg;
            cfg.K = _numIMFs;
            cfg.alpha = _alpha;
            cfg.tau = _tau;
            cfg.tol = _tol;
            win_imfs = vmd.decompose(window_data, cfg).imfs;
        } else if (_method == EMDMethod::CEEMDAN) {
            CEEMDAN ceemdan;
            CEEMDAN::Config cfg;
            cfg.numIMFs = _numIMFs;
            cfg.ensembles = _ensembles;
            cfg.noiseStd = _noiseStd;
            cfg.seed = 42;
            win_imfs = ceemdan.decompose(window_data, cfg).imfs;
        } else {
            EMD emd_algo;
            win_imfs = emd_algo.emd(window_data, _numIMFs);
        }

        // 只取窗口最后一个 bar 的 IMF 值（即当前 bar）
        for (int k = 0; k < _numIMFs && k < static_cast<int>(win_imfs.size()); ++k) {
            if (!win_imfs[k].empty()) {
                out_imfs[k][i] = win_imfs[k].back();
            }
        }
    }

    return true;
}

// 计算 energy_velocity: rolling change of total IMF energy
Vector<double> EMDNode::computeEnergyVelocity(const Vector<Vector<double>>& imfs, int window) const {
    const int n = static_cast<int>(imfs[0].size());
    Vector<double> result(n, 0.0);

    // 计算总能量: E[t] = Σ IMF_k[t]^2
    Vector<double> total_energy(n, 0.0);
    for (auto& imf : imfs) {
        for (int i = 0; i < n; ++i) {
            total_energy[i] += imf[i] * imf[i];
        }
    }

    // rolling mean of energy
    for (int i = window - 1; i < n; ++i) {
        double sum = 0.0;
        for (int j = i - window + 1; j <= i; ++j) sum += total_energy[j];
        double mean = sum / window;

        // velocity = diff(mean) / mean (即滚动变化率)
        if (i > window - 1) {
            double prev_sum = 0.0;
            for (int j = i - window; j < i; ++j) prev_sum += total_energy[j];
            double prev_mean = prev_sum / window;
            if (prev_mean > 0) {
                result[i] = (mean - prev_mean) / prev_mean;
            }
        }
    }

    return result;
}

// 计算 volume_regime: |IMF_low| / volume (最低频 IMF 能量占比)
Vector<double> EMDNode::computeVolumeRegime(const Vector<Vector<double>>& imfs,
                                             const Vector<double>& volume, int window) const {
    const int n = static_cast<int>(imfs[0].size());
    Vector<double> result(n, 0.0);

    if (imfs.empty()) return result;

    // 最低频 IMF 是最后一个
    const auto& imf_low = imfs.back();

    for (int i = window - 1; i < n; ++i) {
        double sum_imf = 0.0, sum_vol = 0.0;
        for (int j = i - window + 1; j <= i; ++j) {
            sum_imf += std::abs(imf_low[j]);
            sum_vol += volume[j];
        }
        if (sum_vol > 0) {
            result[i] = sum_imf / sum_vol;
        }
    }

    return result;
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

        const int minLen = _windowSize > 0 ? _windowSize : 10;
        if (static_cast<int>(input_data.size()) < minLen) {
            WARN("EMDNode input {} too short ({} points), need at least {}",
                 inputKey, input_data.size(), minLen);
            return NodeProcessResult::Skip;
        }

        Vector<Vector<double>> imfs;
        if (!decomposeOne(input_data, imfs)) {
            WARN("EMDNode decomposition failed for input {}", inputKey);
            return NodeProcessResult::Skip;
        }

        // 写入 IMF 输出
        int idx = 0;
        for (auto& imf : imfs) {
            if (idx >= _numIMFs) break;
            String outKey = _label + ".IMF_" + std::to_string(idx);
            context.set(outKey, imf);
            ++idx;
        }

        // 计算衍生特征（如果启用）
        if (_computeEnergyVelocity) {
            auto energy_vel = computeEnergyVelocity(imfs, _windowSize > 0 ? _windowSize : 20);
            context.set(_label + ".energy_velocity", energy_vel);
        }
        if (_computeVolumeRegime) {
            // 需要 volume 输入
            try {
                const auto& vol = context.get<Vector<double>>("volume");
                auto vol_regime = computeVolumeRegime(imfs, vol, _windowSize > 0 ? _windowSize : 20);
                context.set(_label + ".volume_regime", vol_regime);
            } catch (...) {
                WARN("EMDNode: volume_regime requested but no volume input found");
            }
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
        {"windowSize", {{"type", "number"}, {"default", 0}, {"min", 0}, {"max", 500},
                        {"description", "滚动窗口大小: 0=全序列一次分解, >0=滚动窗口EMD (建议120)"}}},
        {"能量变化率", {{"type", "label"}, {"default", ""},
                        {"description", "能量变化率输出标签（连接下游时自动填充）"}}},
        {"成交量体制", {{"type", "label"}, {"default", ""},
                        {"description", "成交量体制输出标签（连接下游时自动填充）"}}},
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
