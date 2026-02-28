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

context_t Return::operator()(const Map<String, context_t>& args) {
    if (args.size() != 1) {
        return std::nan("nan");
    }
    auto itr = args.begin();
    auto& prop_name = itr->first;
    auto& vec = std::get<Vector<double>>(itr->second);
    if (vec.size() < _cnts) {
        return std::nan("nan");
    }
    return 0.;
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