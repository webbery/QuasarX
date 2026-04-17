#include "Nodes/SpreadNode.h"
#include "Util/log.h"
#include <cmath>
#include <numeric>

SpreadNode::SpreadNode()
: _method(SpreadMethod::SIMPLE_DIFF)
, _window(60)
, _fixedBeta(1.0)
, _dynamicBeta(false)
, _count(0)
, _nextIndex(0)
{
}

SpreadNode::~SpreadNode() {
}

bool SpreadNode::Init(const nlohmann::json& config) {
    // 读取计算方法
    if (config["params"].contains("method")) {
        String method = config["params"]["method"]["value"];
        if (method == "simple_diff") {
            _method = SpreadMethod::SIMPLE_DIFF;
        }
        else if (method == "log_diff") {
            _method = SpreadMethod::LOG_DIFF;
        }
        else if (method == "rolling_regression") {
            _method = SpreadMethod::ROLLING_REGRESSION;
        }
    }
    
    // 读取滚动窗口大小
    if (config["params"].contains("window")) {
        _window = config["params"]["window"]["value"];
    }
    
    // 读取固定β值
    if (config["params"].contains("beta")) {
        _fixedBeta = config["params"]["beta"]["value"];
    }
    
    // 判断是否动态β
    _dynamicBeta = (_method == SpreadMethod::ROLLING_REGRESSION);
    
    // 如果是滚动回归，初始化缓冲区
    if (_dynamicBeta) {
        _priceABuffer.resize(_window, 0.0);
        _priceBBuffer.resize(_window, 0.0);
        _count = 0;
        _nextIndex = 0;
    }
    
    return true;
}

/**
 * 计算简单价差: Price_A - Price_B
 */
double SpreadNode::calculateSimpleSpread(double priceA, double priceB) const {
    return priceA - priceB;
}

/**
 * 计算对数价差: ln(Price_A) - ln(Price_B)
 */
double SpreadNode::calculateLogSpread(double priceA, double priceB) const {
    if (priceA <= 0 || priceB <= 0) {
        return std::nan("nan");
    }
    return std::log(priceA) - std::log(priceB);
}

/**
 * 滚动回归计算价差: Price_A - β × Price_B
 * 
 * 使用最小二乘法计算β:
 * β = Cov(Price_A, Price_B) / Var(Price_B)
 * α = Mean(Price_A) - β × Mean(Price_B)
 * Spread = Price_A - (α + β × Price_B)
 */
double SpreadNode::calculateRollingRegression(double priceA, double priceB) {
    // 更新缓冲区
    if (_count < _window) {
        _priceABuffer[_count] = priceA;
        _priceBBuffer[_count] = priceB;
        ++_count;
    } else {
        _priceABuffer[_nextIndex] = priceA;
        _priceBBuffer[_nextIndex] = priceB;
        _nextIndex = (_nextIndex + 1) % _window;
    }
    
    // 数据不足时返回NaN
    if (_count < _window) {
        return std::nan("nan");
    }
    
    // 计算均值
    double sumA = 0.0, sumB = 0.0;
    for (size_t i = 0; i < _count; ++i) {
        sumA += _priceABuffer[i];
        sumB += _priceBBuffer[i];
    }
    double meanA = sumA / _count;
    double meanB = sumB / _count;
    
    // 计算协方差和方差
    double covAB = 0.0, varB = 0.0;
    for (size_t i = 0; i < _count; ++i) {
        double diffA = _priceABuffer[i] - meanA;
        double diffB = _priceBBuffer[i] - meanB;
        covAB += diffA * diffB;
        varB += diffB * diffB;
    }
    
    // 避免除以零
    if (varB < 1e-12) {
        return std::nan("nan");
    }
    
    // 计算β和α
    double beta = covAB / varB;
    double alpha = meanA - beta * meanB;
    
    // 计算价差: Price_A - (α + β × Price_B)
    double spread = priceA - (alpha + beta * priceB);
    
    // 存储β值供输出
    _fixedBeta = beta;
    
    return spread;
}

NodeProcessResult SpreadNode::Process(const String& strategy, DataContext& context) {
    // 从输入节点获取两只股票的数据
    if (_ins.size() < 2) {
        WARN("SpreadNode requires at least 2 input nodes (stock A and stock B)");
        return NodeProcessResult::Error;
    }
    
    // 获取第一个输入节点的数据（标的A）
    auto itrA = _ins.begin();
    String keyA = itrA->second->out_elements().begin()->first;
    auto& dataA = context.get<Vector<double>>(keyA);
    double priceA = dataA.back();
    
    // 获取第二个输入节点的数据（标的B）
    auto itrB = std::next(_ins.begin());
    String keyB = itrB->second->out_elements().begin()->first;
    auto& dataB = context.get<Vector<double>>(keyB);
    double priceB = dataB.back();
    
    // 检查价格有效性
    if (priceA <= 0 || priceB <= 0) {
        WARN("Invalid price: priceA={}, priceB={}", priceA, priceB);
        return NodeProcessResult::Skip;
    }
    
    // 计算价差
    double spread = 0.0;
    switch (_method) {
    case SpreadMethod::LOG_DIFF:
        spread = calculateLogSpread(priceA, priceB);
    break;
    case SpreadMethod::ROLLING_REGRESSION:
        spread = calculateRollingRegression(priceA, priceB);
    break;
    default:
        spread = calculateSimpleSpread(priceA, priceB);
    break;
    }
    
    // 检查价差计算结果
    if (std::isnan(spread)) {
        // 数据不足或计算异常，跳过
        return NodeProcessResult::Skip;
    }
    
    // 输出价差值
    if (context.exist(_spreadOutputKey)) {
        context.add(_spreadOutputKey, spread);
    } else {
        Vector<double> timeseries;
        timeseries.push_back(spread);
        context.set(_spreadOutputKey, timeseries);
    }
    
    // 如果是滚动回归，输出β值
    if (_dynamicBeta) {
        if (context.exist(_betaOutputKey)) {
            context.add(_betaOutputKey, _fixedBeta);
        } else {
            Vector<double> timeseries;
            timeseries.push_back(_fixedBeta);
            context.set(_betaOutputKey, timeseries);
        }
    }
    
    return NodeProcessResult::Success;
}

Map<String, ArgType> SpreadNode::out_elements() {
    Map<String, ArgType> elems;
    elems[_spreadOutputKey] = ArgType::Double_TimeSeries;
    if (_dynamicBeta) {
        elems[_betaOutputKey] = ArgType::Double_TimeSeries;
    }
    return elems;
}

const nlohmann::json SpreadNode::getParams() {
    return nlohmann::json{
        {"method", "string"},
        {"window", "number"},
        {"beta", "number"}
    };
}
