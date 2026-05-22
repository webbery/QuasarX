#pragma once
#include "StrategyNode.h"
#include "Function/GaussianHMM.h"
#include "std_header.h"

/**
 * HMM 隐马尔可夫节点（因果推理 category）
 *
 * 用于市场状态识别（牛/熊/震荡...）。
 * - 离线批量训练：每 retrain_interval 天用过去 train_window 天数据重新训练
 * - 协方差类型：diag
 * - 状态数：用户自定义（n_states）
 *
 * 输入：从上游节点读取特征值（按 features 参数解析）
 * 输出（4 个独立 handles）：
 *   hmm_state    — 当前最可能状态编号 (int)
 *   hmm_probs    — 状态概率分布 (vector<double>)
 *   hmm_transition — 状态转移矩阵展平 (vector<double>, N² 个值)
 *   hmm_duration   — 各状态期望持续时间 (vector<double>, N 个值)
 */
class HMMNode : public QNode {
public:
    static const nlohmann::json getParams();
    RegistClassName(HMMNode);

public:
    HMMNode(Server* server);
    ~HMMNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

private:
    Server* _server;
    String _label;

    // 配置参数
    int _n_states = 3;
    Vector<String> _feature_keys;      // 输入特征 key 列表
    int _train_window = 252;           // 训练窗口(天)
    int _retrain_interval = 60;        // 重训间隔(天)
    int _warmup_period = 60;           // 预热期(天)
    int _max_iter = 100;
    double _tol = 1e-4;
    double _regularization = 1e-6;
    uint32_t _random_seed = 42;

    // 内部状态
    GaussianHMM _hmm;
    Eigen::MatrixXd _obs_buffer;       // 观测缓冲区 (window × D)
    int _obs_count = 0;                // 当前观测数
    int _days_since_train = 0;         // 距离上次训练的天数
    int _n_features = 0;
    bool _trained = false;

    // 当前推理结果
    Eigen::VectorXd _current_probs;    // 状态概率
    int _current_state = 0;

    Map<String, ArgType> _params;      // 输入参数
    Map<String, ArgType> _outputs;     // 输出元素声明
};
