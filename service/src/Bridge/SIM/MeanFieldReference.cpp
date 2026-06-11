#include "Bridge/SIM/MeanFieldReference.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>

MeanFieldReference::MeanFieldReference()
    : _dataLength(0)
{
}

int MeanFieldReference::LoadFromDirectory(const String& dataDir, const Vector<String>& symbols) {
    int loaded = 0;
    for (const auto& sym : symbols) {
        // 尝试多种可能的路径格式
        Vector<String> candidates = {
            dataDir + "/" + sym + ".csv",
            dataDir + "/" + sym + ".CSV",
        };

        bool success = false;
        for (const auto& path : candidates) {
            if (std::filesystem::exists(path)) {
                success = LoadFromCSV(sym, path);
                if (success) {
                    ++loaded;
                    break;
                }
            }
        }
        if (!success) {
            WARN("MeanFieldReference: failed to load data for '{}'", sym);
        }
    }

    if (loaded > 1) {
        AlignReturns();
        ComputeCorrelationMatrix();
        ComputeCholesky();
    }

    return loaded;
}

bool MeanFieldReference::LoadFromCSV(const String& symbol, const String& csvPath) {
    std::ifstream file(csvPath);
    if (!file.is_open()) {
        WARN("MeanFieldReference: cannot open '{}'", csvPath);
        return false;
    }

    Vector<double> closes;
    String line;
    bool headerSkipped = false;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // 跳过可能的 CSV 头行
        if (!headerSkipped) {
            // 检查第一列是否是日期格式（YYYY-MM-DD 或时间戳）
            bool isHeader = false;
            if (line.find("date") != String::npos || line.find("Date") != String::npos ||
                line.find("time") != String::npos || line.find("Time") != String::npos ||
                line.find("open") != String::npos || line.find("Open") != String::npos) {
                isHeader = true;
            }
            headerSkipped = true;
            if (isHeader) continue;
        }

        double close = 0.0;
        if (ParseCSVLine(line, close, /*volume=*/ nullptr)) {
            if (close > 0.0) {
                closes.push_back(close);
            }
        }
    }

    if (closes.empty()) {
        WARN("MeanFieldReference: no valid data for '{}' in '{}'", symbol, csvPath);
        return false;
    }

    // 计算对数收益率
    Vector<double> logRet;
    for (size_t i = 1; i < closes.size(); ++i) {
        if (closes[i - 1] > 0.0) {
            logRet.push_back(std::log(closes[i] / closes[i - 1]));
        } else {
            logRet.push_back(0.0);
        }
    }

    // 计算统计量
    double meanPrice = 0.0;
    for (auto p : closes) meanPrice += p;
    meanPrice /= closes.size();

    double meanRet = 0.0;
    for (auto r : logRet) meanRet += r;
    meanRet /= logRet.size();

    double varRet = 0.0;
    for (auto r : logRet) varRet += (r - meanRet) * (r - meanRet);
    varRet /= logRet.size();
    double vol = std::sqrt(varRet);

    _symbols.push_back(symbol);
    _closePrices[symbol] = closes;
    _logReturns[symbol] = logRet;
    _meanPrices[symbol] = meanPrice;
    _meanLogReturns[symbol] = meanRet;
    _volatilities[symbol] = vol;

    if (_dataLength == 0 || static_cast<int>(logRet.size()) < _dataLength) {
        _dataLength = static_cast<int>(logRet.size());
    }

    return true;
}

const Vector<double>& MeanFieldReference::GetClosePrices(const String& symbol) const {
    static const Vector<double> empty;
    auto it = _closePrices.find(symbol);
    if (it == _closePrices.end()) return empty;
    return it->second;
}

const Vector<double>& MeanFieldReference::GetLogReturns(const String& symbol) const {
    static const Vector<double> empty;
    auto it = _logReturns.find(symbol);
    if (it == _logReturns.end()) return empty;
    return it->second;
}

double MeanFieldReference::GetMeanPrice(const String& symbol) const {
    auto it = _meanPrices.find(symbol);
    if (it == _meanPrices.end()) return 0.0;
    return it->second;
}

double MeanFieldReference::GetMeanLogReturn(const String& symbol) const {
    auto it = _meanLogReturns.find(symbol);
    if (it == _meanLogReturns.end()) return 0.0;
    return it->second;
}

double MeanFieldReference::GetVolatility(const String& symbol) const {
    auto it = _volatilities.find(symbol);
    if (it == _volatilities.end()) return 0.0;
    return it->second;
}

Eigen::VectorXd MeanFieldReference::GetMarketMeanPrice() const {
    int N = GetSymbolCount();
    Eigen::VectorXd mean(N);
    for (int i = 0; i < N; ++i) {
        mean(i) = GetMeanPrice(_symbols[i]);
    }
    return mean;
}

const Eigen::MatrixXd& MeanFieldReference::GetCorrelationMatrix() const {
    return _correlationMatrix;
}

const Eigen::MatrixXd& MeanFieldReference::GetCovarianceMatrix() const {
    return _covarianceMatrix;
}

const Eigen::MatrixXd& MeanFieldReference::GetCholeskyFactor() const {
    return _choleskyFactor;
}

double MeanFieldReference::GetCorrelation(const String& symA, const String& symB) const {
    auto itA = std::find(_symbols.begin(), _symbols.end(), symA);
    auto itB = std::find(_symbols.begin(), _symbols.end(), symB);
    if (itA == _symbols.end() || itB == _symbols.end()) return 0.0;

    int i = static_cast<int>(itA - _symbols.begin());
    int j = static_cast<int>(itB - _symbols.begin());
    if (i >= _correlationMatrix.rows() || j >= _correlationMatrix.cols()) return 0.0;

    return _correlationMatrix(i, j);
}

void MeanFieldReference::AlignReturns() {
    // 确保所有标的的收益率序列长度一致，取最小长度
    int minLen = std::numeric_limits<int>::max();
    for (const auto& sym : _symbols) {
        auto it = _logReturns.find(sym);
        if (it != _logReturns.end()) {
            int len = static_cast<int>(it->second.size());
            if (len < minLen) minLen = len;
        }
    }
    _dataLength = minLen;

    // 截断到统一长度（从末尾对齐，保留最新数据）
    for (const auto& sym : _symbols) {
        auto& rets = _logReturns[sym];
        auto& prices = _closePrices[sym];
        if (static_cast<int>(rets.size()) > _dataLength) {
            int trim = static_cast<int>(rets.size()) - _dataLength;
            rets.erase(rets.begin(), rets.begin() + trim);
            // 同时截断收盘价
            if (static_cast<int>(prices.size()) > _dataLength + 1) {
                prices.erase(prices.begin(), prices.begin() + trim);
            }
        }
    }
}

void MeanFieldReference::ComputeCorrelationMatrix() {
    int N = GetSymbolCount();
    if (N < 2 || _dataLength <= 0) {
        _correlationMatrix = Eigen::MatrixXd::Identity(1, 1);
        _covarianceMatrix = Eigen::MatrixXd::Identity(1, 1);
        _choleskyFactor = Eigen::MatrixXd::Identity(1, 1);
        return;
    }

    // 构建收益率矩阵 (N × T)
    Eigen::MatrixXd retMatrix(N, _dataLength);
    for (int i = 0; i < N; ++i) {
        const auto& rets = _logReturns[_symbols[i]];
        for (int t = 0; t < _dataLength; ++t) {
            retMatrix(i, t) = rets[t];
        }
    }

    // 计算协方差矩阵
    Eigen::VectorXd means(N);
    for (int i = 0; i < N; ++i) {
        means(i) = _meanLogReturns[_symbols[i]];
    }

    Eigen::MatrixXd centered = retMatrix;
    for (int i = 0; i < N; ++i) {
        centered.row(i).array() -= means(i);
    }
    _covarianceMatrix = (centered * centered.transpose()) / (_dataLength - 1);

    // 计算相关系数矩阵
    _correlationMatrix.resize(N, N);
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            double sigma_i = _volatilities[_symbols[i]];
            double sigma_j = _volatilities[_symbols[j]];
            if (sigma_i < 1e-12 || sigma_j < 1e-12) {
                _correlationMatrix(i, j) = (i == j) ? 1.0 : 0.0;
            } else {
                _correlationMatrix(i, j) = _covarianceMatrix(i, j) / (sigma_i * sigma_j);
            }
        }
    }

    // 确保相关系数矩阵对称且正定
    _correlationMatrix = (_correlationMatrix + _correlationMatrix.transpose()) / 2.0;

    // 对角线强制为 1
    for (int i = 0; i < N; ++i) {
        _correlationMatrix(i, i) = 1.0;
    }

}

void MeanFieldReference::ComputeCholesky() {
    int N = GetSymbolCount();
    if (N < 1) {
        _choleskyFactor = Eigen::MatrixXd::Identity(1, 1);
        return;
    }

    // 优先使用 LLT（正定矩阵更快）
    Eigen::LLT<Eigen::MatrixXd> llt(_correlationMatrix);
    if (llt.info() == Eigen::Success) {
        _choleskyFactor = llt.matrixL().toDenseMatrix();
    } else {
        // 回退到 LDLT
        Eigen::LDLT<Eigen::MatrixXd> ldlt(_correlationMatrix);
        if (ldlt.info() != Eigen::Success) {
            WARN("MeanFieldReference: Cholesky decomposition failed, falling back to identity");
            _choleskyFactor = Eigen::MatrixXd::Identity(N, N);
            return;
        }
        // LDLT: correlation = L * D * L^T，所以 L_chol = L * sqrt(D)
        Eigen::MatrixXd L = ldlt.matrixL().toDenseMatrix();
        Eigen::VectorXd d = ldlt.vectorD();
        for (int i = 0; i < N; ++i) {
            L.col(i) *= std::sqrt(std::max(d(i), 0.0));
        }
        _choleskyFactor = L;
    }

    // 验证：L * L^T ≈ correlation
    Eigen::MatrixXd reconstructed = _choleskyFactor * _choleskyFactor.transpose();
    double error = (reconstructed - _correlationMatrix).norm();
    if (error > 1e-6) {
        WARN("MeanFieldReference: Cholesky reconstruction error = {:.2e}", error);
    }
}

bool MeanFieldReference::ParseCSVLine(const String& line, double& close, double* volume) {
    Vector<String> tokens;
    split(line, tokens, ",");
    if (tokens.size() < 5) return false;

    // CSV 格式: datetime, open, close, high, low, volume, ...
    // 或者: datetime, open, high, low, close, volume, ...
    // 尝试常见格式
    close = atof(tokens[2].c_str());  // 默认第3列是 close

    if (volume && tokens.size() > 5) {
        *volume = atof(tokens[5].c_str());
    }

    return close > 0.0;
}
