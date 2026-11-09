#include "Nodes/Portfolio/KellyModel.h"
#include "Util/system.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace portfolio {

namespace {

/**
 * @brief 组级交易统计（凯利的输入）
 *
 * win_rate = 盈利笔数 / 总笔数；avg_win / avg_loss = 平均盈利 / 平均亏损幅度。
 * 统计的**单位是组**（配对组就是"一次配对交易"），不是单条腿 —— 配对的两条腿
 * 方向相反，各自的胜率天然 ≈ 50%，拿腿级胜率算凯利没有任何信息量。
 *
 * 这是 KellyModel 的私有类型，不再出现在共享头文件里。
 */
struct GroupStats {
    float _win_rate = 0.0;
    float _avg_win = 0.0;
    float _avg_loss = 0.0;
    unsigned short _samples = 0;      ///< 已平仓笔数 —— 样本太少时凯利估计不可信
    bool   _valid = false;
};

/// 读 context 里某个标量序列的最新值
bool readLast(DataContext& context, const String& key, double& out) {
    if (!context.exist(key)) {
        return false;
    }
    const auto& var = context.get(key);
    const auto* vec = std::get_if<Vector<double>>(&var);
    if (vec == nullptr || vec->empty()) {
        return false;
    }
    out = vec->back();
    return std::isfinite(out);
}

/// 读组级统计：{key}.win_rate / {key}.avg_win / {key}.avg_loss / {key}.samples
GroupStats readGroupStats(DataContext& context, const String& key) {
    GroupStats stats;

    double win_rate = 0.0;
    if (!readLast(context, key + ".win_rate", win_rate)) {
        return stats;
    }
    stats._win_rate = std::clamp(win_rate, 0.0, 1.0);

    // avg_win / avg_loss 缺失时按对称赔率 1:1 处理 —— 硬编码 2%/2% 是错的：
    // 配对的单笔盈亏幅度差一个数量级（实测 +5.9% / −7.6%），赔率直接决定
    // 凯利比例，猜错等于把仓位算错。
    double avg_win = 0.0;
    double avg_loss = 0.0;
    const bool has_win = readLast(context, key + ".avg_win", avg_win);
    const bool has_loss = readLast(context, key + ".avg_loss", avg_loss);
    stats._avg_win = has_win ? std::max(avg_win, 0.0) : 0.02;
    stats._avg_loss = has_loss ? std::max(avg_loss, 0.0) : 0.02;

    double samples = 0.0;
    if (readLast(context, key + ".samples", samples)) {
        stats._samples = static_cast<int>(std::max(samples, 0.0));
    } else {
        // 没有样本数时按 1 笔处理 —— 调用方会用最小样本门槛再挡一次
        stats._samples = 1;
    }
    stats._valid = true;
    return stats;
}

}  // namespace

bool KellyModel::onInit(const nlohmann::json& config) {
    if (config.contains("params")) {
        _min_samples = readInt(config["params"], "kelly_min_samples", _min_samples);
        if (_min_samples < 1) {
            _min_samples = 1;
        }
    }
    return true;
}

void KellyModel::Prepare() {
    // 告警限频状态按运行复位 —— 否则上一轮回测的告警集合会带到这一轮
    _warned_groups.clear();
}

Vector<double> KellyModel::computeScales(PortfolioContext& ctx) {
    const auto offsets = groupOffsets(ctx._legs);
    const size_t groups = offsets.empty() ? 0 : offsets.size() - 1;
    if (groups == 0) {
        return {};
    }

    // 没有组级统计时：每组均分预算（与"回退 1/N"同语义，分母从"买入标的数"
    // 变成"定仓单元数"—— 配对的两条腿是**一个**单元）。
    const double fallback = 1.0 / static_cast<double>(groups);

    Vector<double> scales(groups, 0.0);
    for (size_t g = 0; g < groups; ++g) {
        const auto& anchor = ctx._legs[offsets[g]];
        const String key = groupKeyOf(ctx, anchor);
        const GroupStats stats = readGroupStats(ctx._data, key);

        double f = fallback;
        if (stats._valid && stats._samples >= _min_samples && stats._avg_loss > 0.0) {
            // f* = (b·p − q)/b
            const double b = stats._avg_win / stats._avg_loss;
            const double p = stats._win_rate;
            const double q = 1.0 - p;
            f = (b * p - q) / b;
            if (f < 0.0) {
                // 负的优势 → 不下注。凯利在这里的正确用法是"告诉你别做"，
                // 而不是硬凑一个正数继续下注。
                f = 0.0;
            }
        } else if (stats._valid && stats._samples > 0 && stats._samples < _min_samples) {
            // 每根 bar 都会命中 → 限频，否则实盘会刷屏
            if (_warned_groups.insert(key).second) {
                ctx.warn(fmt::format(
                    "Kelly skipped for group '{}': only {} closed trade(s) < min {}; "
                    "fallback to equal deployment",
                    key, stats._samples, _min_samples));
            }
        }

        scales[g] = f;
    }

    // 组内按 share（配对 = 1 : β）拆分由基类 distribute 完成。
    return scales;
}

}  // namespace portfolio
