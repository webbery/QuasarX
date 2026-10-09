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
 *
 * 除以上无前缀的 handle 外，还按「每个 symbol 一份」写出带前缀的时间序列：
 *   {symbol}.hmm_state、{symbol}.hmm_probs_{j}
 * 原因：FormulaNode / SignalNode 解析标识符时一律拼成 `{symbol}.{name}` 去
 * context 里取，无前缀的 handle 它们永远查不到，`hmm_state == 1` 这类判断
 * 会静默拿到 NaN。带前缀的写法与 FunctionNode / XGBoostNode 的约定一致，
 * 也是公式里 `hmm_probs[1]` 能展开成 `hmm_probs_1` 的前提。
 *
 * 状态编号语义（state_order = "drift" 时）：
 *   训练完成后按各状态观测均值升序重排，n_states=3 时
 *   0 = 下跌趋势、1 = 震荡、2 = 上涨趋势。
 *   不重排的话每次重训的编号含义都可能翻转，按固定编号分支的策略会失灵。
 *
 * 预热期（skip_while_warming = false 时）：
 *   输出 hmm_state = -1（未知）而不是 Skip。Skip 会让 RunGraph 直接 break，
 *   本节点之后的所有节点（含 Signal / Execute / Portfolio）当轮全部不执行，
 *   风控检查也被跳过——把 HMM 放在交易链路上时这是致命的。
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

    // 状态编号排序策略："none" = 保持 EM 原始编号；"drift" = 按观测均值升序重排
    String _state_order = "none";
    // 预热期是否 Skip（true = 旧行为；false = 输出 hmm_state=-1 并继续跑完整轮）
    bool _skip_while_warming = true;

    // 由 features 推出的 symbol 列表（特征 key 形如 {symbol}.{label}）
    Vector<symbol_t> _symbols;

    // 内部状态
    GaussianHMM _hmm;
    Eigen::MatrixXd _obs_buffer;       // 观测缓冲区 (window × D)
    int _obs_count = 0;                // 当前观测数
    int _days_since_train = 0;         // 距离上次训练的天数
    int _n_features = 0;
    bool _trained = false;
    bool _pretrained = false;       // 当前模型来自 modelFile（尚未被在线重训替换）
    String _model_path;             // modelFile 路径，仅用于日志

    // 当前推理结果
    Eigen::VectorXd _current_probs;    // 状态概率
    int _current_state = 0;

    Map<String, ArgType> _params;      // 输入参数
    Map<String, ArgType> _outputs;     // 输出元素声明

    /** @brief 按 _state_order 把当前模型的状态重排成固定语义 */
    void applyStateOrder();
    /** @brief 预热/未就绪时输出占位状态（hmm_state=-1）并返回 Success */
    void emitNotReady(DataContext& context);
};
