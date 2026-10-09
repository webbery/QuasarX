#include "Nodes/HMMNode.h"
#include "StrategyNode.h"
#include "server.h"
#include "Util/log.h"
#include "boost/algorithm/string.hpp"
#include <fstream>
#include "json.hpp"

HMMNode::HMMNode(Server* server) : _server(server) {}

HMMNode::~HMMNode() {}

bool HMMNode::Init(const nlohmann::json& config) {
    _label = (String)config["label"];

    // 解析参数
    if (config.contains("params")) {
        auto& p = config["params"];
        if (p.contains("n_states")) _n_states = (int)p["n_states"]["value"];
        if (p.contains("features")) {
            String feat_str = (String)p["features"]["value"];
            boost::algorithm::split(_feature_keys, feat_str, boost::is_any_of(","));
            for (auto& k : _feature_keys) boost::algorithm::trim(k);
        }
        if (p.contains("train_window")) _train_window = (int)p["train_window"]["value"];
        if (p.contains("retrain_interval")) _retrain_interval = (int)p["retrain_interval"]["value"];
        if (p.contains("warmup_period")) _warmup_period = (int)p["warmup_period"]["value"];
        if (p.contains("max_iter")) _max_iter = (int)p["max_iter"]["value"];
        if (p.contains("tol")) _tol = (double)p["tol"]["value"];
        if (p.contains("regularization")) _regularization = (double)p["regularization"]["value"];
        if (p.contains("random_seed")) _random_seed = (uint32_t)(int)p["random_seed"]["value"];
        if (p.contains("state_order")) _state_order = (String)p["state_order"]["value"];
        if (p.contains("skip_while_warming")) _skip_while_warming = (bool)p["skip_while_warming"]["value"];
    }

    if (_feature_keys.empty()) {
        // 默认从输入节点获取
        for (auto& item : _ins) {
            auto out_names = item.second->out_elements();
            for (auto& kv : out_names) {
                _feature_keys.push_back(kv.first);
            }
        }
    }

    _n_features = static_cast<int>(_feature_keys.size());
    if (_n_features <= 0) {
        WARN("[HMM] No input features found for node {}", _label);
        return false;
    }

    // 特征 key 形如 {symbol}.{label}，据此推出 symbol 列表。
    // 下游公式按 {symbol}.{name} 取值，所以带前缀的输出必须覆盖到每个 symbol。
    // symbol 格式为 "exchange.code"（如 "sh.600176"），所以 key 有 3 段：
    // "sh.600176.Return(1)" → symbol 是 "sh.600176"（前两段）
    for (auto& key : _feature_keys) {
        auto firstDot = key.find('.');
        if (firstDot == String::npos) continue;
        auto secondDot = key.find('.', firstDot + 1);
        if (secondDot == String::npos) continue;
        String symStr = key.substr(0, secondDot);
        symbol_t sym = to_symbol(symStr);
        if (std::find(_symbols.begin(), _symbols.end(), sym) == _symbols.end()) {
            _symbols.push_back(sym);
        }
    }
    if (_symbols.empty()) {
        WARN("[HMM] features '{}' carry no {{symbol}} prefix, downstream formulas "
             "cannot read the outputs", boost::algorithm::join(_feature_keys, ", "));
    }

    // 初始化观测缓冲区
    _obs_buffer.resize(_train_window, _n_features);
    _obs_buffer.setZero();
    _obs_count = 0;

    // 注册输出
    _outputs["hmm_state"] = ArgType::Double_TimeSeries;
    _outputs["hmm_probs"] = ArgType::Double_TimeSeries;
    _outputs["hmm_transition"] = ArgType::Double_TimeSeries;
    _outputs["hmm_duration"] = ArgType::Double_TimeSeries;

    // 带 symbol 前缀的逐 bar 时间序列，供 FormulaNode / SignalNode 消费
    for (const auto& sym : _symbols) {
        _outputs[get_symbol(sym) + ".hmm_state"] = ArgType::Double_TimeSeries;
        for (int j = 0; j < _n_states; j++) {
            _outputs[get_symbol(sym) + ".hmm_probs_" + std::to_string(j)] =
                ArgType::Double_TimeSeries;
        }
    }

    // 如果指定了 modelFile，从文件加载模型
    if (config.contains("modelFile")) {
        String modelPath = config["modelFile"];
        _model_path = modelPath;
        try {
            std::ifstream ifs(modelPath);
            if (ifs.is_open()) {
                nlohmann::json modelJson;
                ifs >> modelJson;
                GaussianHMM loaded = GaussianHMM::from_json(modelJson);

                // 维度必须与节点配置一致，否则下面写 context 时会按 _n_states/_n_features
                // 遍历模型实际维度更小的数组，release 构建下静默读越界内存。
                if (loaded.config().n_states != _n_states ||
                    loaded.config().n_features != _n_features) {
                    WARN("[HMM] model dimension mismatch: file has states={} features={}, "
                         "node configured states={} features={}. Reusing node config.",
                         loaded.config().n_states, loaded.config().n_features,
                         _n_states, _n_features);
                    return false;
                }

                _hmm = std::move(loaded);
                _pretrained = true;
                _trained = true;
                applyStateOrder();
                INFO("[HMM] Loaded model from {} (states={}, features={})",
                     modelPath, _hmm.config().n_states, _hmm.config().n_features);
            } else {
                WARN("[HMM] Failed to open model file: {}", modelPath);
            }
        } catch (const std::exception& e) {
            WARN("[HMM] Failed to load model from {}: {}", modelPath, e.what());
        }
    }

    INFO("[HMM] Initialized: n_states={}, features={}, window={}, interval={}, warmup={}",
         _n_states, _n_features, _train_window, _retrain_interval, _warmup_period);
    return true;
}

NodeProcessResult HMMNode::Process(const String& strategy, DataContext& context) {
    if (_n_features <= 0) return NodeProcessResult::Skip;

    _days_since_train++;

    // 1. 收集当前时刻的特征值。
    //    取不到特征时若 skip_while_warming=false，输出「未知」状态并让整轮继续跑完：
    //    Skip 会让 RunGraph 直接 break，HMM 之后的 Signal/Execute/Portfolio 当轮全部不执行。
    Eigen::VectorXd obs(_n_features);
    for (int d = 0; d < _n_features; d++) {
        const String& key = _feature_keys[d];
        try {
            const auto& vec = context.get<Vector<double>>(key);
            if (vec.empty()) {
                if (!_skip_while_warming) {
                    emitNotReady(context);
                    return NodeProcessResult::Success;
                }
                return NodeProcessResult::Skip;
            }
            obs(d) = vec.back();
        } catch (const std::exception& e) {
            if (!_skip_while_warming) {
                emitNotReady(context);
                return NodeProcessResult::Success;
            }
            // 不记日志的话特征取不到与「取到了但为空」表现完全一样，
            // 节点静默不产出任何数据，从外部无从判断原因
            WARN("[HMM] Failed to read feature '{}': {}", key, e.what());
            return NodeProcessResult::Skip;
        }
    }

    // 2. 预热期：仍累积观测，不输出有效状态。
    //    只在「从未训练成功」时才走这里——训练成功后 _days_since_train 会被归零，
    //    下一根 bar 自增为 1 又满足 <= warmup_period，会被重新判成预热期。
    //    预热的本意是「攒够观测再开始预测」，_obs_count 才是它的完成条件。
    if (!_trained && _days_since_train <= _warmup_period) {
        if (_obs_count < _train_window) {
            _obs_buffer.row(_obs_count) = obs;
            _obs_count++;
        }
        if (_skip_while_warming) return NodeProcessResult::Skip;
        emitNotReady(context);
        return NodeProcessResult::Success;
    }

    // 3. 累积观测到缓冲区
    if (_obs_count < _train_window) {
        _obs_buffer.row(_obs_count) = obs;
        _obs_count++;
        if (_obs_count < _train_window) {
            // 数据还不够训练。观测已经存进去了，这里只决定「本轮输出什么」。
            if (_skip_while_warming) return NodeProcessResult::Skip;
            emitNotReady(context);
            return NodeProcessResult::Success;
        }
    } else {
        // 缓冲区满，滑动窗口：左移 + 追加新观测
        _obs_buffer.topRows(_train_window - 1) = _obs_buffer.bottomRows(_train_window - 1);
        _obs_buffer.row(_train_window - 1) = obs;
    }

    // 4. 检查是否需要重新训练
    bool need_train = !_trained || (_days_since_train >= _retrain_interval);
    if (need_train && _obs_count >= _train_window) {
        GaussianHMM::Config cfg;
        cfg.n_states = _n_states;
        cfg.n_features = _n_features;
        cfg.max_iter = _max_iter;
        cfg.tol = _tol;
        cfg.regularization = _regularization;
        cfg.random_seed = _random_seed;

        GaussianHMM hmm(cfg);
        Eigen::MatrixXd train_data = _obs_buffer.topRows(_train_window);

        // train() 返回的是「是否收敛」，不是「是否训练成功」。用返回值判断会把
        // 「训完了但没收敛」的模型当成失败丢弃，于是 _hmm 永远停在第一个模型上，
        // 而 _days_since_train 照常归零，冷却期内没有任何重试迹象。
        // 成功与否看 is_trained()，收敛与否只作为日志信息。
        const bool converged = hmm.train(train_data);
        if (hmm.is_trained()) {
            _hmm = std::move(hmm);
            _trained = true;
            // 排序在切换点之前做：离线模型与在线模型的状态编号各自训练而来，
            // 排序后两段里的同一编号才指向同一种行情，下游按编号分支才有意义。
            applyStateOrder();
            // 离线模型与在线模型的状态编号各自独立训练而来，同一个编号在两段里
            // 含义可能完全不同。下游按 state 编号分支的策略会看到无法解释的跳变，
            // 所以切换点必须留日志。
            if (_pretrained) {
                INFO("[HMM] Switching from pretrained model {} to online-trained model. "
                     "State ids are NOT comparable across this boundary.",
                     _model_path);
                _pretrained = false;
            }
            INFO("[HMM] Retrained: ll={:.2f}, converged={}, state={}",
                 _hmm.log_likelihood(), converged, _hmm.current_state());
        } else {
            WARN("[HMM] Retrain failed: {} observations < {} required (n_states={})",
                 train_data.rows(), _n_states * 2, _n_states);
        }
        _days_since_train = 0;
    }

    // 5. 推理当前状态概率
    if (!_trained) {
        if (_skip_while_warming) return NodeProcessResult::Skip;
        emitNotReady(context);
        return NodeProcessResult::Success;
    }

    _current_probs = _hmm.predict_proba(obs);
    _current_state = _hmm.current_state();

    // 6. 写入 context
    // hmm_state: 当前状态编号
    if (context.exist("hmm_state")) {
        context.add("hmm_state", static_cast<double>(_current_state));
    } else {
        Vector<double> ts;
        ts.push_back(static_cast<double>(_current_state));
        context.set("hmm_state", ts);
    }

    // 带 symbol 前缀的逐 bar 序列：FormulaNode / SignalNode 只认这个约定
    for (const auto& sym : _symbols) {
        const String prefix = get_symbol(sym) + ".hmm_state";
        if (context.exist(prefix)) {
            context.add(prefix, static_cast<double>(_current_state));
        } else {
            context.set(prefix, Vector<double>{static_cast<double>(_current_state)});
        }
    }

    // hmm_probs: 概率分布
    {
        Vector<double> probs_vec;
        for (int j = 0; j < _n_states; j++) {
            probs_vec.push_back(_current_probs(j));
        }
        if (context.exist("hmm_probs")) {
            context.add("hmm_probs", probs_vec);
        } else {
            context.set("hmm_probs", probs_vec);
        }
    }

    // hmm_transition: 转移矩阵展平
    {
        const auto& A = _hmm.transition_matrix();
        Vector<double> trans_vec;
        for (int i = 0; i < _n_states; i++) {
            for (int j = 0; j < _n_states; j++) {
                trans_vec.push_back(A(i, j));
            }
        }
        if (context.exist("hmm_transition")) {
            context.add("hmm_transition", trans_vec);
        } else {
            context.set("hmm_transition", trans_vec);
        }
    }

    // hmm_duration: 期望持续时间
    {
        auto dur = _hmm.state_duration();
        Vector<double> dur_vec;
        for (int j = 0; j < _n_states; j++) {
            dur_vec.push_back(dur(j));
        }
        if (context.exist("hmm_duration")) {
            context.add("hmm_duration", dur_vec);
        } else {
            context.set("hmm_duration", dur_vec);
        }
    }

    // 带前缀的概率分量：逐 bar 的 double 序列，公式里 hmm_probs[1] 会展开成
    // {symbol}.hmm_probs_1。向量型的 hmm_probs 每轮 append 整个数组、只有最后一根
    // bar 的值有效，没法当时间序列用。
    for (const auto& sym : _symbols) {
        const String base = get_symbol(sym) + ".hmm_probs_";
        for (int j = 0; j < _n_states; j++) {
            const String key = base + std::to_string(j);
            if (context.exist(key)) {
                context.add(key, _current_probs(j));
            } else {
                context.set(key, Vector<double>{_current_probs(j)});
            }
        }
    }

    return NodeProcessResult::Success;
}

void HMMNode::applyStateOrder() {
    if (_state_order != "drift" || !_trained) return;

    const auto& mu = _hmm.means();
    int n = _hmm.config().n_states;
    if (mu.rows() != n || mu.cols() == 0) {
        WARN("[HMM] state_order=drift skipped: means is {}x{}, expected {}xD",
             mu.rows(), mu.cols(), n);
        return;
    }

    // 判据：每个状态在全部特征上的观测均值。对 33 只股票 × 日收益的面板 HMM 来说，
    // 它就是「该状态下全市场的平均漂移」，升序排完 0=下跌 / 1=震荡 / 2=上涨。
    // 用绝对量而非相对旧模型的距离，重训之间不需要额外对齐就天然可比。
    Vector<int> order(n);
    for (int i = 0; i < n; i++) order[i] = i;
    auto score = [&mu](int j) { return mu.row(j).mean(); };
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return score(a) < score(b); });

    // order[j] = 新编号 j 对应的旧编号
    Vector<int> perm(n);
    for (int j = 0; j < n; j++) perm[j] = order[j];

    try {
        _hmm.reorder_states(perm);
        Vector<String> parts;
        for (int j = 0; j < n; j++) parts.push_back(fmt::format("{:.5f}", _hmm.means().row(j).mean()));
        INFO("[HMM] state_order=drift applied: state means now [{}]", boost::algorithm::join(parts, ", "));
    } catch (const std::exception& e) {
        WARN("[HMM] state_order=drift failed: {}", e.what());
    }
}

void HMMNode::emitNotReady(DataContext& context) {
    // 未就绪时也要写满所有 handle：下游公式取不到 key 只会拿到 NaN，
    // 而 NaN 参与比较一律为 false，看起来与「状态=未知」效果相同却更难排查。
    const double NOT_READY = -1.0;
    if (context.exist("hmm_state")) {
        context.add("hmm_state", NOT_READY);
    } else {
        context.set("hmm_state", Vector<double>{NOT_READY});
    }
    for (const auto& sym : _symbols) {
        const String prefix = get_symbol(sym) + ".hmm_state";
        if (context.exist(prefix)) {
            context.add(prefix, NOT_READY);
        } else {
            context.set(prefix, Vector<double>{NOT_READY});
        }
        for (int j = 0; j < _n_states; j++) {
            const String key = get_symbol(sym) + ".hmm_probs_" + std::to_string(j);
            if (context.exist(key)) {
                context.add(key, 0.0);
            } else {
                context.set(key, Vector<double>{0.0});
            }
        }
    }
}

Map<String, ArgType> HMMNode::out_elements() {
    return _outputs;
}

void HMMNode::UpdateLabel(const String& label) {
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

const nlohmann::json HMMNode::getParams() {
    return nlohmann::json::object();
}
