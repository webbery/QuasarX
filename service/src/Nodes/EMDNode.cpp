#include "std_header.h"
#include "Nodes/EMDNode.h"
#include "Algorithms/EMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Algorithms/VMD.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"

EMDNode::EMDNode(Server* server)
    : _server(server), _method(EMDMethod::EMD), _numIMFs(5), _ensembles(50), _noiseStd(0.2),
      _alpha(2000.0), _tau(0.0), _tol(1e-6), _windowSize(0),
      _computeEnergyVelocity(false), _computeVolumeRegime(false), _fillmode(WarmupFillType::FillNan) {}

bool EMDNode::Init(const nlohmann::json& config) {
    _rollingIMFs.clear();
    // (旧 _rollingInitialized 已删除: stored.empty() 检查已足够)

    _label = (String)config["label"];

    // 解析算法类型
    String methodStr = config["params"]["method"]["value"].get<String>();
    boost::algorithm::to_lower(methodStr);
    if (methodStr == "ceemdan") {
        _method = EMDMethod::CEEMDAN;
    } else if (methodStr == "vmd") {
        _method = EMDMethod::VMD;
    } else {
        _method = EMDMethod::EMD;
    }

    _numIMFs = config["params"]["numIMFs"]["value"].get<int>();
    if (_numIMFs < 1 || _numIMFs > 20) {
        throw std::runtime_error("EMD numIMFs must be between 1 and 20, got: " + std::to_string(_numIMFs));
    }

    // 滚动窗口大小：0 = 全序列一次分解，>0 = 滚动窗口 EMD
    if (config["params"].contains("windowSize")) {
        _windowSize = config["params"]["windowSize"]["value"].get<int>();
    }
    if (_windowSize < 0 || (_windowSize > 0 && _windowSize < 30)) {
        throw std::runtime_error("EMD windowSize must be 0 (global) or >= 30, got: " + std::to_string(_windowSize));
    }

    // 衍生特征开关（由前端 JSON 控制）
    _computeEnergyVelocity = config["params"].contains("energyVelocityLabel") &&
                             !config["params"]["energyVelocityLabel"]["value"].get<String>().empty();
    _computeVolumeRegime = config["params"].contains("volumeRegimeLabel") &&
                           !config["params"]["volumeRegimeLabel"]["value"].get<String>().empty();

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

    // BFS 上游发现 symbol 列表，用于 per-symbol 输出注册
    auto symbolSet = discoverUpstreamSymbols();
    if (symbolSet.empty()) {
        WARN("EMDNode: no upstream symbols found");
        return false;
    }
    // 缓存 "sz.000423." 形式的 prefix 集合，Process 中按 inputKey 前缀匹配
    _symbolPrefixes.clear();
    for (auto sym : symbolSet) {
        _symbolPrefixes.insert(get_symbol(sym) + ".");
    }

    // 解析实际 edge 连接的输入，确定需要分解的数据字段（如 "volume"）
    // resolveInputConnections() 从 sourceHandle 提取数据名（如 "1-volume" → "volume"）
    auto resolvedInputs = resolveInputConnections();
    Set<String> connectedFields;
    for (auto& [dataName, contextKey] : resolvedInputs) {
        connectedFields.insert(dataName);
    }

    // 从上游节点的全部输出中，筛选出已连接字段的所有 symbol 的 context key
    // 例：连接了 "volume" → 收集 sz.800001.volume, sh.600519.volume, ...
    for (auto& item : _ins) {
        auto input_names = item.second->out_elements();
        for (auto& [key, type] : input_names) {
            for (auto& field : connectedFields) {
                String suffix = "." + field;
                if (key.size() > suffix.size() &&
                    key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    _params[key] = type;
                    break;
                }
            }
        }
    }

    // 从实际连接中找出 volume 数据的 context key
    if (_computeVolumeRegime) {
        for (auto& [dataName, contextKey] : resolvedInputs) {
            if (dataName == "volume") {
                _volumeContextKey = contextKey;
                break;
            }
        }
        if (_volumeContextKey.empty()) {
            WARN("EMDNode: volume_regime requested but no volume input connected (sourceHandle must carry 'volume')");
        }
    }

    // 构建 per-symbol 输出: {symbol}.label.nimf_0, ...
    for (auto& sym : symbolSet) {
        String prefix = get_symbol(sym);
        for (int i = 0; i < _numIMFs; ++i) {
            String outKey = prefix + "." + _label + ".nimf_" + std::to_string(i);
            _outputs[outKey] = ArgType::Double_TimeSeries;
        }
        if (_computeEnergyVelocity) {
            _outputs[prefix + "." + _label + ".energy_velocity"] = ArgType::Double_TimeSeries;
        }
        if (_computeVolumeRegime) {
            _outputs[prefix + "." + _label + ".volume_regime"] = ArgType::Double_TimeSeries;
        }
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

    // 预填 _rollingIMFs 所有 key (空 Vector 即可): Process 在 OMP 中并发写
    // 不同 inputKey 对应的 entry, 关键不变量是"不再有 operator[] 的隐式 insert",
    // 否则 std::unordered_map 的 rehash 会破坏并发安全.
    // 注: Map = std::map (USE_PMR 未定义), std::map 没有 reserve(), 直接 emplace.
    for (const auto& [k, _] : _params) {
        _rollingIMFs.emplace(k, Vector<Vector<double>>{});
    }
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

    // 记录每个 IMF 上一个有效值（用于窗口产出不足时延续前值，避免虚假零值）
    Vector<double> lastValid(_numIMFs, 0.0);

    for (int i = w - 1; i < n; ++i) {
        // 取窗口内数据
        Vector<double> window_data(w);
        for (int j = 0; j < w; ++j) {
            window_data[j] = input_data[i - w + 1 + j];
        }

        // 对窗口数据做 EMD（zeroPad=false：不补零，返回实际产出的 IMF）
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
            cfg.zeroPad = false;
            win_imfs = ceemdan.decompose(window_data, cfg).imfs;
        } else {
            EMD emd_algo;
            win_imfs = emd_algo.emd(window_data, _numIMFs, /*zeroPad=*/false);
        }

        // 只取窗口最后一个 bar 的 IMF 值（即当前 bar）
        // 不足 _numIMFs 的 IMF 延续前一窗口的值，避免虚假零值触发异常检测
        const int actual = static_cast<int>(win_imfs.size());
        for (int k = 0; k < _numIMFs; ++k) {
            if (k < actual && !win_imfs[k].empty()) {
                lastValid[k] = win_imfs[k].back();
            }
            out_imfs[k][i] = lastValid[k];
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
    // 滚动和改写: 维护窗口和, 每步只做一次加一次减, O(n) 而非每个 i 重算整窗的 O(n×window)
    double sum = 0.0;
    for (int j = 0; j < window && j < n; ++j) sum += total_energy[j];
    for (int i = window; i < n; ++i) {
        double prev_mean = sum / window;  // 上一窗口 [i-window, i-1]
        sum += total_energy[i] - total_energy[i - window];
        double mean = sum / window;

        // velocity = diff(mean) / mean (即滚动变化率)
        if (prev_mean > 0) {
            result[i] = (mean - prev_mean) / prev_mean;
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

    // rolling sum of |IMF_low| / volume (滚动和改写, O(n))
    double sum_imf = 0.0, sum_vol = 0.0;
    for (int j = 0; j < window && j < n; ++j) {
        sum_imf += std::abs(imf_low[j]);
        sum_vol += volume[j];
    }
    if (window - 1 < n && sum_vol > 0) {
        result[window - 1] = sum_imf / sum_vol;
    }
    for (int i = window; i < n; ++i) {
        sum_imf += std::abs(imf_low[i]) - std::abs(imf_low[i - window]);
        sum_vol += volume[i] - volume[i - window];
        if (sum_vol > 0) {
            result[i] = sum_imf / sum_vol;
        }
    }

    return result;
}

NodeProcessResult EMDNode::Process(const String& strategy, DataContext& context) {
    // OMP 并行遍历 _params 的 inputKey (每个 inputKey = 1 个 symbol).
    // 并发安全前提:
    //   1. 每个 inputKey 唯一对应一个 symbol, 写出的 context key 也是 symbol-prefixed,
    //      不同线程只触碰各自的 key, 不会写同一个 key (没有真正的 map 冲突).
    //   2. _rollingIMFs 在 Init 时已预填所有 inputKey (空 entry), Process 内不再 insert,
    //      因此 std::unordered_map 不会因 rehash 而破坏并发安全.
    //   3. 依赖的实际行为: 对已有 key 的 operator[]/find() + 赋值不会触发 rehash.
    //      (标准未做硬性保证, 但 libstdc++/libc++ 都是这样; 若未来换成别的 STL 实现
    //      出现并发 bug, 应在 DataContext 的 set/add 加 shared_mutex.)
    // schedule(dynamic, 1) 而非 static: 不同 window 数据收敛迭代数差异较大 (实测
    // 38 ~ 421 次), 静态分块会导致线程空闲不均.
    const size_t N = _params.size();
    Vector<String> inputKeys;
    inputKeys.reserve(N);
    for (const auto& [k, _] : _params) inputKeys.push_back(k);

    // ---- 预填 DataContext._outputs 中所有 per-symbol 输出 key ----
    // 必须先于 OMP 并行: warmup 阶段是这些 key 的首次写入时刻,
    // 多线程并发 insert 到 std::unordered_map 会触发 rehash, 进而破坏其它线程
    // 正在进行的 find/contains 访问 (崩溃根因).
    // 一次性预填空 Vector 占位, 后续 set/add 都不再 insert, 也就不会再 rehash.
    // 该步骤每 epoch 重复, 但已存在 key 的 set 退化为值赋值 (~50 ns/op),
    // 总开销约 210 keys × 50 ns = 10 µs/epoch, 对全回测影响可忽略.
    for (const auto& [inputKey, _] : _params) {
        String prefix;
        for (const auto& p : _symbolPrefixes) {
            if (inputKey.size() > p.size() &&
                inputKey.compare(0, p.size(), p) == 0) {
                prefix = p;
                break;
            }
        }
        if (prefix.empty()) continue;
        for (int i = 0; i < _numIMFs; ++i) {
            String outKey = prefix + _label + ".nimf_" + std::to_string(i);
            context.set(outKey, Vector<double>{});
        }
        if (_computeEnergyVelocity) {
            context.set(prefix + _label + ".energy_velocity", Vector<double>{});
        }
        if (_computeVolumeRegime) {
            context.set(prefix + _label + ".volume_regime", Vector<double>{});
        }
    }

    NodeProcessResult finalResult = NodeProcessResult::Success;
    #pragma omp parallel for schedule(dynamic, 1)
    for (int idx = 0; idx < N; ++idx) {
        NodeProcessResult r = processSymbol(inputKeys[idx], context);
        if (r != NodeProcessResult::Success) {
            #pragma omp critical
            {
                if (finalResult == NodeProcessResult::Success) finalResult = r;
            }
        }
    }
    return finalResult;
}

NodeProcessResult EMDNode::processSymbol(const String& inputKey, DataContext& context) {
    // 从 DataContext 获取输入时间序列（避免拷贝，用 const 引用）
    auto& value = context.get(inputKey);
    const Vector<double>* pInput = std::get_if<Vector<double>>(&value);
    if (!pInput) {
        WARN("EMDNode input {} is not a time series", inputKey);
        return NodeProcessResult::Error;
    }
    const auto& input_data = *pInput;
    const int n = static_cast<int>(input_data.size());

    const int minLen = _windowSize > 0 ? _windowSize : 10;
    if (n < minLen) {
        // warmup 期：输出 NaN 占位，保持向量长度与输入同步。
        // 下游节点（FunctionNode/XGBoostNode）可读到 key，
        // 由 XGBoostNode 的 80% 有效性规则决定是否跳过推理。
        String prefix;
        for (auto& p : _symbolPrefixes) {
            if (inputKey.size() > p.size() &&
                inputKey.compare(0, p.size(), p) == 0) {
                prefix = p;
                break;
            }
        }
        if (!prefix.empty()) {
            double nan = std::numeric_limits<double>::quiet_NaN();
            for (int i = 0; i < _numIMFs; ++i) {
                String outKey = prefix + _label + ".nimf_" + std::to_string(i);
                if (context.exist(outKey)) {
                    context.add(outKey, nan);
                } else {
                    context.set(outKey, Vector<double>(1, nan));
                }
            }
            if (_computeEnergyVelocity) {
                String k = prefix + _label + ".energy_velocity";
                if (context.exist(k)) context.add(k, nan);
                else context.set(k, Vector<double>(1, nan));
            }
            if (_computeVolumeRegime) {
                String k = prefix + _label + ".volume_regime";
                if (context.exist(k)) context.add(k, nan);
                else context.set(k, Vector<double>(1, nan));
            }
        }
        return NodeProcessResult::Success;
    }

    // 从 inputKey 匹配出正确的 symbol prefix
    // 修复 bug: 旧实现用 inputKey.find('.') 截取，对 "sz.000423.volume"
    // 会截成 "sz."（symbol 自身的 '.' 被吃掉）。改用 Init 时缓存的
    // _symbolPrefixes（"sz.000423."）按前缀匹配，避免被 symbol 中的 '.' 干扰
    String prefix;
    for (auto& p : _symbolPrefixes) {
        if (inputKey.size() > p.size() &&
            inputKey.compare(0, p.size(), p) == 0) {
            prefix = p;
            break;
        }
    }
    if (prefix.empty()) {
        WARN("EMDNode: cannot determine symbol prefix for inputKey '{}'", inputKey);
        return NodeProcessResult::Skip;
    }

    Vector<Vector<double>> imfs;

    if (_windowSize > 0) {
        // ===== 滚动模式：增量计算，只处理最新窗口 =====
        // _rollingIMFs 在 Init 已预填该 key (空 entry), 此处取到的 ref 不会触发 map insert
        auto& stored = _rollingIMFs.at(inputKey);

        if (stored.empty()) {
            // 首次：计算全部位置（bootstrap）
            if (!decomposeOne(input_data, imfs)) {
                WARN("EMDNode decomposition failed for input {}", inputKey);
                return NodeProcessResult::Skip;
            }
            stored = imfs;
        } else {
            // 后续 epoch：只计算最新一个窗口（O(1) 而非 O(n)）
            const int w = _windowSize;
            Vector<double> window_data(w);
            for (int j = 0; j < w; ++j) {
                window_data[j] = input_data[n - w + j];
            }

            Vector<Vector<double>> win_imfs;
            if (_method == EMDMethod::VMD) {
                VMD vmd;
                VMD::Config cfg;
                cfg.K = _numIMFs; cfg.alpha = _alpha;
                cfg.tau = _tau; cfg.tol = _tol;
                win_imfs = vmd.decompose(window_data, cfg).imfs;
            } else if (_method == EMDMethod::CEEMDAN) {
                CEEMDAN ceemdan;
                CEEMDAN::Config cfg;
                cfg.numIMFs = _numIMFs; cfg.ensembles = _ensembles;
                cfg.noiseStd = _noiseStd; cfg.seed = 42;
                cfg.zeroPad = false;
                win_imfs = ceemdan.decompose(window_data, cfg).imfs;
            } else {
                EMD emd_algo;
                win_imfs = emd_algo.emd(window_data, _numIMFs, /*zeroPad=*/false);
            }

            // 追加最新值到持久存储
            // 不足 _numIMFs 的 IMF 延续前一值，避免虚假零值
            const int actual = static_cast<int>(win_imfs.size());
            for (int k = 0; k < _numIMFs; ++k) {
                if (k >= static_cast<int>(stored.size())) {
                    stored.emplace_back(Vector<double>(n - 1, 0.0));
                }
                double val = (k < actual && !win_imfs[k].empty())
                    ? win_imfs[k].back()
                    : (stored[k].empty() ? 0.0 : stored[k].back());
                stored[k].push_back(val);
            }
            imfs = stored;
        }
    } else {
        // ===== 全局模式：每次完整分解 =====
        if (!decomposeOne(input_data, imfs)) {
            WARN("EMDNode decomposition failed for input {}", inputKey);
            return NodeProcessResult::Skip;
        }
    }

    // 写入 per-symbol IMF 输出: {symbol}.label.nimf_N
    int idx = 0;
    for (auto& imf : imfs) {
        if (idx >= _numIMFs) break;
        String outKey = prefix + _label + ".nimf_" + std::to_string(idx);
        context.set(outKey, imf);
        ++idx;
    }

    // 计算衍生特征（如果启用）
    if (_computeEnergyVelocity) {
        auto energy_vel = computeEnergyVelocity(imfs, _windowSize > 0 ? _windowSize : 20);
        context.set(prefix + _label + ".energy_velocity", energy_vel);
    }
    if (_computeVolumeRegime) {
        // 从当前 inputKey 推导同 symbol 的 volume key
        String volKey = prefix + "volume";
        try {
            const auto& vol = context.get<Vector<double>>(volKey);
            auto vol_regime = computeVolumeRegime(imfs, vol, _windowSize > 0 ? _windowSize : 20);
            context.set(prefix + _label + ".volume_regime", vol_regime);
        } catch (...) {
            WARN("EMDNode: volume_regime requested but no volume input found for {}", prefix);
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
