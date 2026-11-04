#include "Algorithms/MVO/MvoOptimizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#if HAVE_OSQP
#include <osqp/osqp.h>
#endif

namespace mvo {

#if HAVE_OSQP

namespace {

/// 协方差对角线的数值下限（年化口径）。低于此值时 OSQP 的内部缩放会失稳。
constexpr double kVarFloor = 1e-10;

}  // namespace

#endif  // HAVE_OSQP

MvoOptimizer::MvoOptimizer(const MvoParams& params)
    : _params(params) {
}

bool MvoOptimizer::solveQp(const Eigen::VectorXd& mu,
                           const Eigen::MatrixXd& cov,
                           const Eigen::VectorXd& w_prev,
                           double exposure,
                           Eigen::VectorXd& w_out,
                           String& note) {
#if !HAVE_OSQP
    (void)mu; (void)cov; (void)w_prev; (void)exposure; (void)w_out;
    note = "OSQP not available at build time";
    return false;
#else
    const Eigen::Index n = mu.size();
    if (n <= 0) {
        note = "empty candidate set";
        return false;
    }

    const OSQPInt N  = static_cast<OSQPInt>(n);
    const OSQPInt nv = 2 * N;             // 变量：x = [w(N); u(N)]
    const OSQPInt m  = 4 * N + 1;         // 约束行数

    // ── P（上三角 CSC）：[[γΣ, 0], [0, 0]] ──
    // 对角线加数值下限，保证 OSQP 的内部缩放稳定；给 PSD 矩阵加非负对角
    // 仍然保持 PSD。
    Eigen::MatrixXd cov_s = cov;
    for (Eigen::Index i = 0; i < n; ++i) {
        const double d = cov_s(i, i);
        if (d < kVarFloor) {
            cov_s(i, i) = kVarFloor;
        }
    }

    std::vector<OSQPInt>   pP(static_cast<size_t>(nv) + 1, 0);
    std::vector<OSQPInt>   iP;
    std::vector<OSQPFloat> xP;
    iP.reserve(static_cast<size_t>(N) * (static_cast<size_t>(N) + 1) / 2);
    xP.reserve(iP.capacity());

    OSQPInt off = 0;
    for (OSQPInt j = 0; j < nv; ++j) {
        pP[static_cast<size_t>(j)] = off;
        if (j < N) {
            for (OSQPInt i = 0; i <= j; ++i) {
                iP.push_back(i);
                xP.push_back(_params._risk_aversion * cov_s(i, j));
            }
            off += (j + 1);
        }
    }
    pP[static_cast<size_t>(nv)] = off;

    // ── q ──
    std::vector<OSQPFloat> q(static_cast<size_t>(nv), 0.0);
    for (Eigen::Index i = 0; i < n; ++i) {
        q[static_cast<size_t>(i)] = -mu(i);
        q[static_cast<size_t>(N) + static_cast<size_t>(i)] =
            _params._turnover_penalty * _params._cost_rate;
    }

    // ── A（m × nv，CSC）──
    //   行 0        : Σw = e
    //   行 1..N     : w_i − u_i ≤ w_prev_i
    //   行 N+1..2N  : −w_i − u_i ≤ −w_prev_i
    //   行 2N+1..3N : −w_i ≤ 0
    //   行 3N+1..4N : w_i ≤ w_max
    std::vector<OSQPInt>   pA(static_cast<size_t>(nv) + 1, 0);
    std::vector<OSQPInt>   iA;
    std::vector<OSQPFloat> xA;
    iA.reserve(static_cast<size_t>(m) + static_cast<size_t>(nv));
    xA.reserve(iA.capacity());

    for (OSQPInt col = 0; col < nv; ++col) {
        pA[static_cast<size_t>(col)] = static_cast<OSQPInt>(iA.size());
        if (col < N) {
            const OSQPInt i = col;
            iA.push_back(0);              xA.push_back(1.0);
            iA.push_back(1 + i);          xA.push_back(1.0);
            iA.push_back(N + 1 + i);      xA.push_back(-1.0);
            iA.push_back(2 * N + 1 + i);  xA.push_back(-1.0);
            iA.push_back(3 * N + 1 + i);  xA.push_back(1.0);
        } else {
            const OSQPInt i = col - N;
            iA.push_back(1 + i);          xA.push_back(-1.0);
            iA.push_back(N + 1 + i);      xA.push_back(-1.0);
        }
    }
    pA[static_cast<size_t>(nv)] = static_cast<OSQPInt>(iA.size());

    // ── l / u ──
    std::vector<OSQPFloat> l(static_cast<size_t>(m), -OSQP_INFTY);
    std::vector<OSQPFloat> u(static_cast<size_t>(m), OSQP_INFTY);
    l[0] = exposure;
    u[0] = exposure;
    for (OSQPInt i = 0; i < N; ++i) {
        const OSQPFloat wp = std::max(w_prev(i), 0.0);
        u[static_cast<size_t>(1 + i)] = wp;
        u[static_cast<size_t>(N + 1 + i)] = -wp;
        u[static_cast<size_t>(2 * N + 1 + i)] = 0.0;
        u[static_cast<size_t>(3 * N + 1 + i)] = _params._max_weight;
    }

    // ── 设置 + 求解 ──
    OSQPSettings settings;
    osqp_set_default_settings(&settings);
    settings.verbose = 0;
    settings.eps_abs = _params._eps_abs;
    settings.eps_rel = _params._eps_rel;
    settings.max_iter = _params._max_iter_qp;
    settings.polishing = 1;

    OSQPCscMatrix Pmat;
    Pmat.m = nv; Pmat.n = nv;
    Pmat.p = pP.data(); Pmat.i = iP.data(); Pmat.x = xP.data();
    Pmat.nzmax = static_cast<OSQPInt>(xP.size());
    Pmat.nz = -1;        // CSC 格式
    Pmat.owned = 0;      // 内存归本函数，OSQP 不负责释放

    OSQPCscMatrix Amat;
    Amat.m = m; Amat.n = nv;
    Amat.p = pA.data(); Amat.i = iA.data(); Amat.x = xA.data();
    Amat.nzmax = static_cast<OSQPInt>(xA.size());
    Amat.nz = -1;
    Amat.owned = 0;

    OSQPSolver* solver = nullptr;
    OSQPInt rc = osqp_setup(&solver, &Pmat, q.data(), &Amat,
                            l.data(), u.data(), m, nv, &settings);
    if (rc != 0 || solver == nullptr) {
        note = std::string("osqp_setup failed: ") + osqp_error_message(rc);
        return false;
    }

    rc = osqp_solve(solver);
    bool ok = false;
    if (rc == 0) {
        const OSQPInt status = solver->info->status_val;
        if (status == OSQP_SOLVED || status == OSQP_SOLVED_INACCURATE) {
            const OSQPFloat* x = solver->solution->x;
            w_out.resize(n);
            for (Eigen::Index i = 0; i < n; ++i) {
                w_out(i) = x[i];
            }
            ok = true;
            note = solver->info->status;
        } else {
            note = std::string("osqp status: ") + solver->info->status;
        }
    } else {
        note = std::string("osqp_solve failed: ") + osqp_error_message(rc);
    }

    osqp_cleanup(solver);
    return ok;
#endif
}

MvoResult MvoOptimizer::solve(const Eigen::VectorXd& mu,
                              const Eigen::MatrixXd& cov,
                              const Eigen::VectorXd& w_prev) {
    MvoResult out;
    const Eigen::Index n = mu.size();

    if (n <= 0) {
        out._note = "empty candidate set";
        return out;
    }

    // 单候选：没有自由度，权重就是敞口（受上限约束）
    if (n == 1) {
        out._weights.assign(1, std::min(_params._max_total_exposure,
                                        _params._max_weight));
        out._ok = true;
        out._note = "single candidate, no degrees of freedom";
        return out;
    }

    if (cov.rows() != n || cov.cols() != n) {
        out._note = "mu/cov dimension mismatch";
        return out;
    }

    Eigen::VectorXd prev = w_prev;
    if (prev.size() != n) {
        prev = Eigen::VectorXd::Zero(n);
    }

    double exposure = _params._max_total_exposure;
    Eigen::VectorXd w;
    String note;
    if (!solveQp(mu, cov, prev, exposure, w, note)) {
        out._note = note;
        return out;
    }

    // ── 波动率目标：不动点迭代 ──────────────────────────────
    // 先解出归一化的权重形状，再令 e = min(E_max, σ_target/σ_v)，以 e 为
    // 新的等式约束重解。收敛很快（3~5 轮）。
    if (_params._vol_target_enabled && n > 1) {
        for (int it = 0; it < _params._max_iter; ++it) {
            const double sigma = std::sqrt(std::max(w.dot(cov * w), 0.0));
            if (sigma <= 1e-12) {
                break;
            }
            const double new_e = std::min(_params._max_total_exposure,
                                          _params._vol_target / sigma);
            if (std::abs(new_e - exposure) < 1e-4) {
                break;
            }
            exposure = new_e;
            Eigen::VectorXd w2;
            String note2;
            if (!solveQp(mu, cov, prev, exposure, w2, note2)) {
                break;
            }
            w = w2;
        }
    }

    // ── 组合层面的现金门槛 ────────────────────────────────
    if (_params._min_expected_excess > 0.0) {
        const double excess = mu.dot(w) -
                              0.5 * _params._risk_aversion * w.dot(cov * w);
        if (excess < _params._min_expected_excess) {
            out._weights.assign(static_cast<size_t>(n), 0.0);
            out._ok = true;
            out._note = "expected excess below cash threshold";
            return out;
        }
    }

    // 数值兜底：裁剪到 [0, w_max]（与 lab 的 np.clip 同口径）。
    // 等式约束 Σw = e 由求解器保证；这里不再二次缩放 —— 缩放后又被上限
    // 裁掉会把敞口压低，反而制造新的偏差。
    for (Eigen::Index i = 0; i < n; ++i) {
        w(i) = std::clamp(w(i), 0.0, _params._max_weight);
    }

    out._weights.resize(static_cast<size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        out._weights[static_cast<size_t>(i)] = w(i);
    }
    out._ok = true;
    if (note.empty()) {
        note = "solved";
    }
    out._note = note;
    return out;
}

}  // namespace mvo
