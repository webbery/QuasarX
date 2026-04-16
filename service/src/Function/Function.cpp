#include "Function/Function.h"
#include "Util/datetime.h"
#include <type_traits>
#include <variant>
#include <Eigen/Dense>

MA::MA(short count): _count(0), _nextIndex(0), _sum(0.) {
    _buffer.resize(count, 0.0);
}

context_t MA::operator()(const Map<String, context_t>& features) {
    if (features.size() != 1) {
        return std::nan("nan");
    }
    auto itr = features.begin();
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            INFO("Not support MA");
        }
    }, itr->second);
    if (_count < _buffer.size()) {
        _buffer[_count] = value;
        _sum += value;
        ++_count;
        return average();
    }
    _sum -= _buffer[_nextIndex];
    _buffer[_nextIndex] = value;
    _sum += value;
    _nextIndex = (_nextIndex + 1) % _buffer.size();
    return average();
}

double MA::average() {
    if (_count == 0) return 0.0;
    return _sum / _count;
}

EMA::EMA(short count, double alpha)
:_alpha(alpha) {
    _buffer.resize(count);
}

context_t EMA::operator()(const Map<String, context_t>& args) {
    return 0.;
}

context_t MACD::operator()(const Map<String, context_t>& args) {

    return 0.;
}

STD::STD(int32_t count) {

}

context_t STD::operator()(const Map<String, context_t>& args) {

    return 0.;
}

Return::Return(int32_t count): _cnts(count){

}

/**
 * @brief 计算多个标的的收益率
 * 
 * 使用对数收益率公式: log(P(t) / P(t-n))
 * 其中 n = _cnts (由 range 参数映射而来)
 * 
 * @param args 输入参数，包含多个标的的价格时间序列
 *             key 格式: "symbol.property" (如 "sh600519.close")
 *             value: Vector<double> 价格序列
 * @return Vector<double> 所有标的的对数收益率
 */
context_t Return::operator()(const Map<String, context_t>& args) {
    Vector<double> returns;
    
    // 遍历所有标的的数据
    for (auto& [key, value] : args) {
        // INFO("type: {}", get_context_type_name(value));
        auto& vec = std::get<Vector<double>>(value);
        // 计算收益率: log(P(t) / P(t-_cnts))
        size_t n = vec.size();
        if (n <= _cnts) {
            returns.push_back(std::nan("nan"));
            continue;
        }

        double currentPrice = vec[n - 1];
        double pastPrice = vec[n - 1 - _cnts];
        
        // 价格无效时跳过
        if (pastPrice <= 0) {
            returns.push_back(std::nan("nan"));
            continue;
        }
        
        // 对数收益率
        returns.push_back(std::log(currentPrice / pastPrice));
    }
    
    return returns;
}

R2::R2(int32_t window)
:_window(window){

}

context_t R2::operator()(const Map<String, context_t>& args) {
    if (args.size() != 1) {
        return std::nan("nan");
    }
    auto itr = args.begin();
    auto& prop_name = itr->first;
    auto& vec = std::get<Vector<double>>(itr->second);
    const int32_t n = static_cast<int32_t>(vec.size());

    // 检查数据量是否足够
    if (n < _window || _window < 2) {
        return std::nan("nan");
    }
    // 取最后 _window 个数据点作为因变量 y
    auto start = vec.end() - _window;
    Eigen::Map<const Eigen::VectorXd> y(&(*start), _window);

    // 构造自变量 x：0, 1, ..., _window-1
    Eigen::VectorXd x = Eigen::VectorXd::LinSpaced(_window, 0, _window - 1);

    // 构建设计矩阵 X = [1, x]
    Eigen::MatrixXd X(_window, 2);
    X.col(0).setOnes();
    X.col(1) = x;

    // 使用 QR 分解求解线性回归系数
    Eigen::VectorXd coeff = X.colPivHouseholderQr().solve(y);

    // 计算预测值、残差平方和 SSE 及总平方和 SST
    Eigen::VectorXd y_pred = X * coeff;
    double SSE = (y - y_pred).squaredNorm();
    double SST = (y.array() - y.mean()).square().sum();

    // 若总平方和接近于零（所有 y 相等），返回 NaN
    if (std::abs(SST) < 1e-12) {
        return std::nan("nan");
    }

    double R2 = 1.0 - SSE / SST;
    // 因浮点误差可能略超出 [0,1]，进行截断
    if (R2 < 0.0) R2 = 0.0;
    if (R2 > 1.0) R2 = 1.0;

    return R2;
}

ZScore::ZScore(int32_t window)
: _window(window), _count(0), _nextIndex(0), _sum(0.0), _sumSq(0.0) {
    _buffer.resize(window, 0.0);
}

/**
 * @brief Z-Score 标准化计算
 * 
 * Z-Score = (x - μ) / σ
 * 其中:
 *   μ = 均值 (窗口内平均值)
 *   σ = 标准差 (窗口内标准差)
 * 
 * 使用滑动窗口高效计算，避免重复遍历数据。
 * 
 * @param args 输入参数，包含价格时间序列
 *             key 格式: "symbol.property" (如 "spread.value")
 *             value: Vector<double> 或 double
 * @return double Z-Score 值，数据不足时返回 NaN
 */
context_t ZScore::operator()(const Map<String, context_t>& args) {
    if (args.size() != 1) {
        return std::nan("nan");
    }
    auto itr = args.begin();
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            INFO("Not support ZScore");
        }
    }, itr->second);

    // 滑动窗口更新
    if (_count < _window) {
        // 窗口未填满，直接追加
        _buffer[_count] = value;
        _sum += value;
        _sumSq += value * value;
        ++_count;
    } else {
        // 窗口已满，替换最旧的数据
        double old_value = _buffer[_nextIndex];
        _sum -= old_value;
        _sumSq -= old_value * old_value;
        
        _buffer[_nextIndex] = value;
        _sum += value;
        _sumSq += value * value;
        
        _nextIndex = (_nextIndex + 1) % _window;
    }

    // 数据不足窗口大小时返回 NaN
    if (_count < _window) {
        return std::nan("nan");
    }

    // 计算均值和标准差
    double mean = _sum / _count;
    double variance = (_sumSq / _count) - (mean * mean);
    
    // 方差为负（浮点误差）或接近零时处理
    if (variance <= 0.0) {
        if (variance < -1e-12) {
            // 异常的负方差，返回 NaN
            return std::nan("nan");
        }
        // 方差接近零，所有值相同，Z-Score 为 0
        return 0.0;
    }
    
    double std_dev = std::sqrt(variance);
    
    // 避免除以零
    if (std_dev < 1e-12) {
        return 0.0;
    }
    
    return (value - mean) / std_dev;
}
