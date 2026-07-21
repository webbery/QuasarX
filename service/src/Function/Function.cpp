#include "Function/Function.h"
#include "Util/datetime.h"
#include <type_traits>
#include <variant>
#include <Eigen/Dense>

MA::MA(short count): _count(0), _nextIndex(0), _sum(0.) {
    _buffer.resize(count, 0.0);
}

context_t MA::operator()(const Map<String, context_t>& args) {
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            value = std::nan("nan");
        }
    }, args.begin()->second);
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

STD::STD(int32_t count)
: _window(count), _count(0), _nextIndex(0), _sum(0.0), _sumSq(0.0) {
    _buffer.resize(count, 0.0);
}

context_t STD::operator()(const Map<String, context_t>& args) {
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            value = std::nan("nan");
        }
    }, args.begin()->second);

    if (_count < static_cast<size_t>(_window)) {
        _buffer[_count] = value;
        _sum += value;
        _sumSq += value * value;
        ++_count;
    } else {
        double old_value = _buffer[_nextIndex];
        _sum -= old_value;
        _sumSq -= old_value * old_value;

        _buffer[_nextIndex] = value;
        _sum += value;
        _sumSq += value * value;

        _nextIndex = (_nextIndex + 1) % _window;
    }

    if (_count < static_cast<size_t>(_window)) {
        return std::nan("nan");
    }

    double mean = _sum / _count;
    double variance = (_sumSq / _count) - (mean * mean);

    if (variance <= 0.0) {
        return 0.0;
    }

    return std::sqrt(variance);
}

Return::Return(int32_t count): _cnts(count){

}

/**
 * @brief 计算单个标的的收益率
 *
 * 使用对数收益率公式: log(P(t) / P(t-n))
 * 其中 n = _cnts (由 range 参数映射而来)
 *
 * @param args 输入参数，包含单个标的的价格时间序列
 *             key: 槽位名（如 "price"）
 *             value: Vector<double> 价格序列
 * @return double 对数收益率
 */
context_t Return::operator()(const Map<String, context_t>& args) {
    auto& vec = std::get<Vector<double>>(args.begin()->second);
    size_t n = vec.size();
    if (n <= _cnts) {
        return std::nan("nan");
    }

    double currentPrice = vec[n - 1];
    double pastPrice = vec[n - 1 - _cnts];

    if (pastPrice <= 0) {
        return std::nan("nan");
    }

    return std::log(currentPrice / pastPrice);
}

R2::R2(int32_t window)
:_window(window){

}

context_t R2::operator()(const Map<String, context_t>& args) {
    auto& vec = std::get<Vector<double>>(args.begin()->second);
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
    double value;
    std::visit([&value] (auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double>) {
            value = v;
        }
        else if constexpr (std::is_same_v<T, Vector<double>>) {
            value = v.back();
        } else {
            value = std::nan("nan");
        }
    }, args.begin()->second);

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

// ── VPCorr 实现 ───────────────────────────────────────────────
//
// 注意: VPCorr 的第一个 bar 仅记录 prev（前一个 close/volume），不产生收益率，
// 从第二个 bar 开始计算 ret = log(close[t]/close[t-1]) 和 vol_chg。
// 这意味着对于 N 个 bar 的输入，VPCorr 产生 N-1 个收益率，
// 第一个 bar 的输出为 NaN。滚动窗口在累积 window 个收益率后开始输出有效相关系数。
// Python 对齐: ret = pd.Series(np.log(closes[1:] / closes[:-1])) 天然匹配此行为。
//

VPCorr::VPCorr(int32_t window)
    : _window(window), _count(0), _nextIndex(0),
      _prevClose(0), _prevVolume(0), _hasPrev(false)
{
    _retBuf.resize(window, 0.0);
    _volBuf.resize(window, 0.0);
}

context_t VPCorr::operator()(const Map<String, context_t>& args) {
    // 按槽位名提取（与 FunctionNode::methodSlotMap 的契约）
    // methodSlotMap 定义: {"price": "close", "volume": "volume"}
    // FunctionNode 保证传入这些槽位，过滤掉上游其他值
    if (args.find("price") == args.end() || args.find("volume") == args.end()) {
        return std::nan("nan");
    }

    auto extractDouble = [](const context_t& val) -> double {
        return std::visit([](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, Vector<double>>) return v.empty() ? 0.0 : v.back();
            else return 0.0;
        }, val);
    };

    double close = extractDouble(args.at("price"));
    double volume = extractDouble(args.at("volume"));

    // 第一个 bar：只记录 prev，不计算收益率，返回 NaN。
    // 这是 VPCorr 的关键特性：N 个 bar 输入产生 N-1 个收益率输出，
    // 第一个 bar 的 NaN 输出必须被 FunctionNode 写入 context 以保持与时间向量对齐。
    if (!_hasPrev) {
        _prevClose = close;
        _prevVolume = volume;
        _hasPrev = true;
        return std::nan("nan");
    }

    // 计算对数收益率和成交量变化率
    double ret = 0, volChg = 0;
    if (_prevClose > 0 && _prevVolume > 0) {
        ret = std::log(close / _prevClose);
        volChg = volume / _prevVolume - 1.0;
    }
    _prevClose = close;
    _prevVolume = volume;

    // 更新环形缓冲
    if (_count < static_cast<size_t>(_window)) {
        _retBuf[_count] = ret;
        _volBuf[_count] = volChg;
        ++_count;
    } else {
        _retBuf[_nextIndex] = ret;
        _volBuf[_nextIndex] = volChg;
        _nextIndex = (_nextIndex + 1) % _window;
    }

    // 数据不足窗口时返回 NaN
    if (_count < static_cast<size_t>(_window)) {
        return std::nan("nan");
    }

    // 计算滚动相关系数
    double sumR = 0, sumV = 0, sumRR = 0, sumVV = 0, sumRV = 0;
    size_t n = _count;
    for (size_t i = 0; i < n; ++i) {
        double r = _retBuf[i];
        double v = _volBuf[i];
        sumR += r;
        sumV += v;
        sumRR += r * r;
        sumVV += v * v;
        sumRV += r * v;
    }

    double meanR = sumR / n;
    double meanV = sumV / n;
    double varR = sumRR / n - meanR * meanR;
    double varV = sumVV / n - meanV * meanV;
    double covRV = sumRV / n - meanR * meanV;

    double denom = std::sqrt(varR * varV);
    if (denom < 1e-12) {
        return 0.0;
    }

    double corr = covRV / denom;
    // 截断到 [-1, 1]
    if (corr < -1.0) corr = -1.0;
    if (corr > 1.0) corr = 1.0;

    return corr;
}

// ── ATR 实现 ───────────────────────────────────────────────
//
// True Range = max(high - low, |high - prev_close|, |low - prev_close|)
// ATR = 滑动窗口均值(TR, period)
//
// 第一个 bar 仅记录 prevClose，不计算 TR，返回 NaN。
// 从第二个 bar 开始计算 TR 并更新环形缓冲。
// 累积满 period 个 TR 后开始输出有效 ATR 值。
//

ATR::ATR(int32_t period)
    : _period(period), _count(0), _nextIndex(0), _sum(0.0),
      _prevClose(0), _hasPrev(false)
{
    _trBuffer.resize(period, 0.0);
}

context_t ATR::operator()(const Map<String, context_t>& args) {
    if (args.find("high") == args.end() ||
        args.find("low") == args.end() ||
        args.find("close") == args.end()) {
        return std::nan("nan");
    }

    auto extractDouble = [](const context_t& val) -> double {
        return std::visit([](const auto& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, Vector<double>>)
                return v.empty() ? 0.0 : v.back();
            else return 0.0;
        }, val);
    };

    double high  = extractDouble(args.at("high"));
    double low   = extractDouble(args.at("low"));
    double close = extractDouble(args.at("close"));

    // 第一个 bar：只记录 prevClose，不计算 TR
    if (!_hasPrev) {
        _prevClose = close;
        _hasPrev = true;
        return std::nan("nan");
    }

    // True Range = max(H-L, |H-prevC|, |L-prevC|)
    double tr = std::max({high - low,
                          std::abs(high - _prevClose),
                          std::abs(low  - _prevClose)});
    _prevClose = close;

    // 更新环形缓冲
    if (_count < static_cast<size_t>(_period)) {
        _trBuffer[_count] = tr;
        _sum += tr;
        ++_count;
    } else {
        double old_tr = _trBuffer[_nextIndex];
        _sum -= old_tr;
        _trBuffer[_nextIndex] = tr;
        _sum += tr;
        _nextIndex = (_nextIndex + 1) % _period;
    }

    // 数据不足窗口时返回 NaN
    if (_count < static_cast<size_t>(_period)) {
        return std::nan("nan");
    }

    return _sum / _count;
}
