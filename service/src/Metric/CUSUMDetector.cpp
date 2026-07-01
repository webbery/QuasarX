#include "Metric/CUSUMDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>

CUSUMDetector::CUSUMDetector(CUSUMConfig config)
    : _config(config), _last_result{} {
    reset();
}

CUSUMStepResult CUSUMDetector::update(double new_return) {
    ++_count;

    // 最少观测数保护：初期不触发变点
    if (_count < _config.min_obs) {
        _last_result = {
            false,
            _count - 1,
            _s_pos,
            _s_neg,
            _s_pos - _s_neg
        };
        return _last_result;
    }

    double k = _config.lambda * _config.sigma;
    double drift = new_return - _config.mu;

    // 双侧 CUSUM 更新
    _s_pos = std::max(0.0, _s_pos + drift - k);
    _s_neg = std::max(0.0, _s_neg - drift - k);

    double h = compute_threshold();
    bool change_point = (std::max(_s_pos, _s_neg) > h);

    if (change_point) {
        ++_total_change_points;
        _last_change_index = _count - 1;
        // 触发后重置累积和（避免连续触发）
        _s_pos = 0.0;
        _s_neg = 0.0;
    }

    _max_drift = std::max(_max_drift, std::abs(_s_pos - _s_neg));

    _last_result = {
        change_point,
        _count - 1,
        _s_pos,
        _s_neg,
        _s_pos - _s_neg
    };

    return _last_result;
}

CUSUMResult CUSUMDetector::detect_batch(const std::vector<double>& returns) {
    reset();

    CUSUMResult result;
    result.steps.reserve(returns.size());

    for (double ret : returns) {
        auto step = update(ret);
        result.steps.push_back(step);
        if (step.change_point) {
            ++result.total_change_points;
            result.last_change_index = step.step_index;
        }
        result.max_drift = std::max(result.max_drift, std::abs(step.current_drift));
    }

    return result;
}

void CUSUMDetector::reset() {
    _s_pos = 0.0;
    _s_neg = 0.0;
    _count = 0;
    _total_change_points = 0;
    _max_drift = 0.0;
    _last_change_index = 0;
    _last_result = {};
}

double CUSUMDetector::compute_threshold() const {
    // h = threshold_multiplier * sigma * sqrt(N)
    // N 使用当前观测数，动态调整阈值
    return _config.threshold_multiplier * _config.sigma * std::sqrt(static_cast<double>(_count));
}
