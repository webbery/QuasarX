#include "Bridge/SIM/MVParticleSystem.h"
#include "Util/log.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

MVParticleSystem::MVParticleSystem()
    : _reference(nullptr)
    , _numParticles(0)
    , _rng(42)  // 固定种子保证可复现
    , _finished(false)
{
}

bool MVParticleSystem::Initialize(MeanFieldReference& reference, int numParticles,
                                   const MVSDEParams& params) {
    if (!reference.IsLoaded()) {
        WARN("MVParticleSystem: reference distribution not loaded");
        return false;
    }
    if (numParticles < 1) {
        WARN("MVParticleSystem: invalid particle count {}", numParticles);
        return false;
    }

    _reference = &reference;
    _numParticles = numParticles;
    _params = params;
    _symbols = reference.GetSymbols();
    _finished = false;

    int N = GetSymbolCount();
    int T = params._numSteps;

    // 初始化粒子路径
    _paths.resize(N);
    for (int i = 0; i < N; ++i) {
        _paths[i].resize(numParticles);
        for (int m = 0; m < numParticles; ++m) {
            _paths[i][m]._symbol = _symbols[i];
            _paths[i][m]._particleId = m;
            _paths[i][m]._prices.resize(T + 1);
            _paths[i][m]._returns.resize(T);

            // 初始价格：从历史均价出发
            _paths[i][m]._prices[0] = params._S0 > 0.0 ? params._S0
                                                        : reference.GetMeanPrice(_symbols[i]);
        }
    }

    // 从参考分布初始化 drift 参数
    if (_params._mu0 == 0.0) {
        // 使用平均历史收益率作为基础漂移
        double avgRet = 0.0;
        for (const auto& sym : _symbols) {
            avgRet += reference.GetMeanLogReturn(sym);
        }
        avgRet /= N;
        _params._mu0 = avgRet / _params._dt;  // 转为年化
    }

    // 从参考分布初始化 diffusion 参数
    if (_params._sigma0 == 0.02) {
        // 使用平均历史波动率
        double avgVol = 0.0;
        for (const auto& sym : _symbols) {
            avgVol += reference.GetVolatility(sym);
        }
        avgVol /= N;
        _params._sigma0 = avgVol / std::sqrt(_params._dt);  // 转为年化
    }

    return true;
}

bool MVParticleSystem::Step(int step) {
    if (!_reference || _symbols.empty()) {
        WARN("MVParticleSystem: not initialized");
        return false;
    }
    if (step < 0 || step >= _params._numSteps) {
        WARN("MVParticleSystem: invalid step {}", step);
        return false;
    }

    int N = GetSymbolCount();
    int M = _numParticles;
    double dt = _params._dt;
    double sqrtDt = std::sqrt(dt);

    // 生成相关随机数 (N 维，每个粒子独立)
    // 注意：所有粒子共享同一组随机数以保持相关性结构
    Eigen::VectorXd Z = GenerateCorrelatedNoise();

    // 对每个粒子执行 SDE 更新
    for (int m = 0; m < M; ++m) {
        // 计算当前时刻所有粒子的平均价格（mean field 项）
        // 注意：这里用参考分布的冻结均值，而非正在演化的粒子均值（方案 B）

        for (int i = 0; i < N; ++i) {
            double S = _paths[i][m]._prices[step];
            double refMean = GetReferenceMeanPrice(i);

            // Euler-Maruyama 离散化
            double drift = ComputeDrift(i, S, step);
            double diffusion = ComputeDiffusion(i, S);
            double dW = Z(i) * sqrtDt;

            double S_next = S + drift * dt + diffusion * dW;

            // 价格边界保护（不允许负价格）
            if (S_next < 1e-6) {
                S_next = 1e-6;
            }

            _paths[i][m]._prices[step + 1] = S_next;
            _paths[i][m]._returns[step] = (S - refMean) / refMean;
        }
    }

    return true;
}

bool MVParticleSystem::Simulate() {
    Reset();
    for (int step = 0; step < _params._numSteps; ++step) {
        if (!Step(step)) {
            WARN("MVParticleSystem: simulation failed at step {}", step);
            return false;
        }
    }
    _finished = true;
    return true;
}

void MVParticleSystem::Reset() {
    _finished = false;
    int N = GetSymbolCount();
    int T = _params._numSteps;

    for (int i = 0; i < N; ++i) {
        for (int m = 0; m < _numParticles; ++m) {
            _paths[i][m]._prices.assign(T + 1, 0.0);
            _paths[i][m]._returns.assign(T, 0.0);
            _paths[i][m]._prices[0] = _params._S0 > 0.0 ? _params._S0
                                                         : _reference->GetMeanPrice(_symbols[i]);
        }
    }
}

const MVParticlePath* MVParticleSystem::GetParticlePath(const String& symbol, int particleId) const {
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return nullptr;

    int idx = static_cast<int>(it - _symbols.begin());
    if (particleId < 0 || particleId >= _numParticles) return nullptr;

    return &_paths[idx][particleId];
}

Vector<const MVParticlePath*> MVParticleSystem::GetParticlePaths(const String& symbol) const {
    Vector<const MVParticlePath*> result;
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return result;

    int idx = static_cast<int>(it - _symbols.begin());
    for (int m = 0; m < _numParticles; ++m) {
        result.push_back(&_paths[idx][m]);
    }
    return result;
}

double MVParticleSystem::GetParticleMeanPrice(const String& symbol, int step) const {
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return 0.0;

    int idx = static_cast<int>(it - _symbols.begin());
    if (step < 0 || step > _params._numSteps) return 0.0;

    double sum = 0.0;
    for (int m = 0; m < _numParticles; ++m) {
        sum += _paths[idx][m]._prices[step];
    }
    return sum / _numParticles;
}

double MVParticleSystem::GetParticleVariance(const String& symbol, int step) const {
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return 0.0;

    int idx = static_cast<int>(it - _symbols.begin());
    if (step < 0 || step > _params._numSteps) return 0.0;

    double mean = GetParticleMeanPrice(symbol, step);
    double var = 0.0;
    for (int m = 0; m < _numParticles; ++m) {
        double diff = _paths[idx][m]._prices[step] - mean;
        var += diff * diff;
    }
    return var / (_numParticles - 1);
}

double MVParticleSystem::GetParticleMedianPrice(const String& symbol, int step) const {
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return 0.0;

    int idx = static_cast<int>(it - _symbols.begin());
    if (step < 0 || step > _params._numSteps) return 0.0;

    Vector<double> prices(_numParticles);
    for (int m = 0; m < _numParticles; ++m) {
        prices[m] = _paths[idx][m]._prices[step];
    }
    std::nth_element(prices.begin(), prices.begin() + _numParticles / 2, prices.end());
    return prices[_numParticles / 2];
}

double MVParticleSystem::GetParticleQuantile(const String& symbol, int step, double q) const {
    auto it = std::find(_symbols.begin(), _symbols.end(), symbol);
    if (it == _symbols.end()) return 0.0;

    int idx = static_cast<int>(it - _symbols.begin());
    if (step < 0 || step > _params._numSteps) return 0.0;
    if (q < 0.0 || q > 1.0) return 0.0;

    Vector<double> prices(_numParticles);
    for (int m = 0; m < _numParticles; ++m) {
        prices[m] = _paths[idx][m]._prices[step];
    }
    std::sort(prices.begin(), prices.end());

    int pos = static_cast<int>(q * (_numParticles - 1));
    return prices[pos];
}

double MVParticleSystem::ComputeDrift(int symbolIdx, double S, int /*step*/) const {
    double refMean = GetReferenceMeanPrice(symbolIdx);

    // μ(t, S, S̄_hist) = μ0 + μ1·(S - S̄_hist) + μ2·loss_signal
    // 目前 μ2 = 0（Phase 2 由 planner 驱动）
    return _params._mu0 + _params._mu1 * (S - refMean);
}

double MVParticleSystem::ComputeDiffusion(int symbolIdx, double S) const {
    double refMean = GetReferenceMeanPrice(symbolIdx);

    // σ(t, S, S̄_hist) = σ0 + σ1·|S - S̄_hist| + σ2·λ(t)
    // 目前 σ2 = 1.0（Phase 2 由 planner 动态控制 λ(t)）
    return _params._sigma0 + _params._sigma1 * std::abs(S - refMean) + _params._sigma2;
}

Eigen::VectorXd MVParticleSystem::GenerateCorrelatedNoise() {
    int N = GetSymbolCount();
    if (N < 1) return Eigen::VectorXd::Zero(1);

    // 生成独立标准正态随机数
    Eigen::VectorXd Z(N);
    std::normal_distribution<double> nd(0.0, 1.0);
    for (int i = 0; i < N; ++i) {
        Z(i) = nd(_rng);
    }

    // 通过 Cholesky 分解引入相关性
    const auto& L = _reference->GetCholeskyFactor();
    return L * Z;
}

double MVParticleSystem::GetReferenceMeanPrice(int symbolIdx) const {
    if (symbolIdx < 0 || symbolIdx >= static_cast<int>(_symbols.size())) {
        return 0.0;
    }
    return _reference->GetMeanPrice(_symbols[symbolIdx]);
}
