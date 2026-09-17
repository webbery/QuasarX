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

    // 自适应校准：calibratePeriod > 0 时用前 max(calibratePeriod, min_obs) 个值校准 mu/sigma
    // calibratePeriod = 0 时不校准，直接使用 config 预设的 mu/sigma
    if (_config._calibratePeriod > 0 && !_calibrated) {
        size_t effectivePeriod = std::max(_config._calibratePeriod, _config._min_obs);
        _calibBuffer.push_back(new_return);
        if (_calibBuffer.size() >= effectivePeriod) {
            calibrate(_calibBuffer);
            for (double r : _calibBuffer) {
                _step(r);
            }
            _calibBuffer.clear();
        } else {
            _last_result = {false, _count - 1, 0.0, 0.0, 0.0};
            return _last_result;
        }
    } else {
        _step(new_return);
    }

    return _last_result;
}

void CUSUMDetector::calibrate(const std::vector<double>& returns) {
    if (returns.empty()) return;
    double sum = 0.0;
    for (double r : returns) sum += r;
    double mean = sum / returns.size();

    double sq_sum = 0.0;
    for (double r : returns) sq_sum += (r - mean) * (r - mean);
    double sigma = std::sqrt(sq_sum / returns.size());
    if (sigma < 1e-10) sigma = 1e-10;

    _config._mu = mean;
    _config._sigma = sigma;
    _calibrated = true;

    // 重置累积状态
    _s_pos = 0.0;
    _s_neg = 0.0;
    _max_drift = 0.0;
    _total_change_points = 0;
    _last_change_index = 0;
}

void CUSUMDetector::_step(double new_return) {
    // 最少观测数保护：初期不触发变点
    if (_count < _config._min_obs) {
        double k = _config._lambda * _config._sigma;
        double drift = new_return - _config._mu;
        _s_pos = std::max(0.0, _s_pos + drift - k);
        _s_neg = std::max(0.0, _s_neg - drift - k);
        _last_result = {false, _count - 1, _s_pos, _s_neg, _s_pos - _s_neg};
        return;
    }

    double k = _config._lambda * _config._sigma;
    double drift = new_return - _config._mu;

    // 双侧 CUSUM 更新
    _s_pos = std::max(0.0, _s_pos + drift - k);
    _s_neg = std::max(0.0, _s_neg - drift - k);

    // 在变点检测/重置之前记录峰值漂移（重置后 _s_pos/_s_neg 归零会丢失峰值）
    _max_drift = std::max(_max_drift, std::abs(_s_pos - _s_neg));

    double h = compute_threshold();
    bool change_point = (std::max(_s_pos, _s_neg) > h);

    if (change_point) {
        ++_total_change_points;
        _last_change_index = _count - 1;
        // 触发后重置累积和（避免连续触发）
        _s_pos = 0.0;
        _s_neg = 0.0;
    }

    _last_result = {
        change_point,
        _count - 1,
        _s_pos,
        _s_neg,
        _s_pos - _s_neg
    };
}

CUSUMResult CUSUMDetector::detect_batch(const std::vector<double>& returns) {
    reset();

    CUSUMResult result;
    result._steps.reserve(returns.size());

    for (double ret : returns) {
        auto step = update(ret);
        result._steps.push_back(step);
        if (step._change_point) {
            ++result._total_change_points;
            result._last_change_index = step._step_index;
        }
        result._max_drift = std::max(result._max_drift, std::abs(step._current_drift));
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
    _calibBuffer.clear();
    _calibrated = false;
}

double CUSUMDetector::compute_threshold() const {
    // h = min(threshold × σ × √n, cap × σ)（√n + 上限，与 Python 对齐）
    size_t n = std::max(_count, size_t(1));
    double h = _config._threshold_multiplier * _config._sigma * std::sqrt(n);
    if (_config._threshold_cap > 0) {
        h = std::min(h, _config._threshold_cap * _config._sigma);
    }
    return h;
}
