#include "Util/Spec/regression_spec.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace regression_spec {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool Fail(SpecError& err, int status, const std::string& msg) {
    err._status = status;
    err._message = msg;
    return false;
}

// ════════════════════════════════════════════════════════════════════════════
// JSON → 基础类型
// ════════════════════════════════════════════════════════════════════════════

bool ReadStringArray(const nlohmann::json& spec, const char* key,
                     std::vector<std::string>& out, SpecError& err) {
    if (!spec.contains(key)) {
        return Fail(err, 400, std::string("缺少字段: ") + key);
    }
    const auto& v = spec[key];
    if (!v.is_array()) {
        return Fail(err, 400, std::string("字段必须是数组: ") + key);
    }
    out.clear();
    out.reserve(v.size());
    for (const auto& e : v) {
        if (!e.is_string()) {
            return Fail(err, 400, std::string("数组元素必须是字符串: ") + key);
        }
        out.push_back(e.get<std::string>());
    }
    return true;
}

/// 读 T×N 面板；null → NaN（内核按 listwise 删除，不插补）
bool ReadMatrix(const nlohmann::json& v, int T, int N, const std::string& label,
                regression::Mat& out, SpecError& err) {
    if (!v.is_array()) {
        return Fail(err, 400, "面板必须是二维数组: " + label);
    }
    if (static_cast<int>(v.size()) != T) {
        return Fail(err, 400, "面板行数与 dates 不符: " + label +
                     "（期望 " + std::to_string(T) +
                     "，实际 " + std::to_string(v.size()) + "）");
    }
    out.resize(T, N);
    for (int t = 0; t < T; ++t) {
        const auto& row = v[static_cast<size_t>(t)];
        if (!row.is_array() || static_cast<int>(row.size()) != N) {
            return Fail(err, 400, "面板列数与 symbols 不符: " + label +
                         "（第 " + std::to_string(t) + " 行期望 " +
                         std::to_string(N) + " 列）");
        }
        for (int i = 0; i < N; ++i) {
            const auto& cell = row[static_cast<size_t>(i)];
            if (cell.is_null()) {
                out(t, i) = kNaN;
            } else if (cell.is_number()) {
                out(t, i) = cell.get<double>();
            } else {
                return Fail(err, 400, "面板元素必须是数字或 null: " + label);
            }
        }
    }
    return true;
}

/// 读长度 T 的时序因子
bool ReadSeries(const nlohmann::json& v, int T, const std::string& label,
                regression::Vec& out, SpecError& err) {
    if (!v.is_array()) {
        return Fail(err, 400, "时序因子必须是一维数组: " + label);
    }
    if (static_cast<int>(v.size()) != T) {
        return Fail(err, 400, "时序因子长度与 dates 不符: " + label +
                     "（期望 " + std::to_string(T) +
                     "，实际 " + std::to_string(v.size()) + "）");
    }
    out.resize(T);
    for (int t = 0; t < T; ++t) {
        const auto& cell = v[static_cast<size_t>(t)];
        if (cell.is_null()) {
            out(t) = kNaN;
        } else if (cell.is_number()) {
            out(t) = cell.get<double>();
        } else {
            return Fail(err, 400, "时序因子元素必须是数字或 null: " + label);
        }
    }
    return true;
}

/// 读分组标签（长度 N，每个标的一个）
bool ReadGroupLabels(const nlohmann::json& v, int N, const std::string& label,
                     std::vector<std::string>& out, SpecError& err) {
    if (!v.is_array()) {
        return Fail(err, 400, "分组变量必须是数组: " + label);
    }
    if (static_cast<int>(v.size()) != N) {
        return Fail(err, 400, "分组变量长度与 symbols 不符: " + label +
                     "（期望 " + std::to_string(N) +
                     "，实际 " + std::to_string(v.size()) + "）");
    }
    out.clear();
    out.reserve(v.size());
    for (const auto& e : v) {
        if (!e.is_string()) {
            return Fail(err, 400, "分组标签必须是字符串: " + label);
        }
        out.push_back(e.get<std::string>());
    }
    return true;
}

bool ReadOptions(const nlohmann::json& spec, regression::FitOptions& opt, SpecError& err) {
    if (!spec.contains("options") || spec["options"].is_null()) {
        return true;  // 全用默认值
    }
    const auto& o = spec["options"];
    if (!o.is_object()) {
        return Fail(err, 400, "options 必须是对象");
    }

    auto readInt = [&](const char* key, int& dst) -> bool {
        if (!o.contains(key)) return true;
        if (!o[key].is_number_integer()) {
            return Fail(err, 400, std::string("options.") + key + " 必须是整数");
        }
        dst = o[key].get<int>();
        return true;
    };
    auto readDouble = [&](const char* key, double& dst) -> bool {
        if (!o.contains(key)) return true;
        if (!o[key].is_number()) {
            return Fail(err, 400, std::string("options.") + key + " 必须是数字");
        }
        dst = o[key].get<double>();
        return true;
    };
    auto readBool = [&](const char* key, bool& dst) -> bool {
        if (!o.contains(key)) return true;
        if (!o[key].is_boolean()) {
            return Fail(err, 400, std::string("options.") + key + " 必须是布尔值");
        }
        dst = o[key].get<bool>();
        return true;
    };

    if (!readInt("min_obs", opt._min_obs)) return false;
    if (!readInt("min_group_size", opt._min_group_size)) return false;
    if (!readInt("hac_lag", opt._hac_lag)) return false;
    if (!readBool("add_const", opt._add_const)) return false;
    if (!readDouble("winsor_q", opt._winsor_q)) return false;
    if (!readDouble("ridge_lambda", opt._ridge_lambda)) return false;
    if (!readBool("report_vif", opt._report_vif)) return false;
    if (!readBool("time_ordered", opt._time_ordered)) return false;
    if (o.contains("fallback_group")) {
        if (!o["fallback_group"].is_string()) {
            return Fail(err, 400, "options.fallback_group 必须是字符串");
        }
        opt._fallback_group = o["fallback_group"].get<std::string>();
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// 基础类型 → JSON（NaN/Inf 一律 null，与 Python 的 _json_safe 一致）
// ════════════════════════════════════════════════════════════════════════════

nlohmann::json NumOrNull(double v) {
    if (!std::isfinite(v)) return nullptr;
    return v;
}

nlohmann::json MatToJson(const regression::Mat& m) {
    nlohmann::json a = nlohmann::json::array();
    for (int i = 0; i < m.rows(); ++i) {
        nlohmann::json row = nlohmann::json::array();
        for (int j = 0; j < m.cols(); ++j) row.push_back(NumOrNull(m(i, j)));
        a.push_back(std::move(row));
    }
    return a;
}

nlohmann::json VecToJson(const regression::Vec& v) {
    nlohmann::json a = nlohmann::json::array();
    for (int i = 0; i < v.size(); ++i) a.push_back(NumOrNull(v(i)));
    return a;
}

/// 扁平剔除掩码 → 嵌套 rows×cols
/// C++ 内部是扁平 vector<int>（长度 rows*cols），Python 侧是嵌套二维数组；
/// parity 用 np.array_equal 比较，形状必须一致。
nlohmann::json DroppedToNested(const std::vector<int>& flat, int rows, int cols) {
    nlohmann::json a = nlohmann::json::array();
    for (int r = 0; r < rows; ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (int c = 0; c < cols; ++c) {
            const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(cols) +
                               static_cast<size_t>(c);
            row.push_back(idx < flat.size() ? flat[idx] : 0);
        }
        a.push_back(std::move(row));
    }
    return a;
}

nlohmann::json TermsToJson(const std::vector<regression::Term>& terms) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : terms) {
        nlohmann::json j;
        j["name"] = t._name;
        j["beta"] = NumOrNull(t._beta);
        j["se"] = NumOrNull(t._se);
        j["t"] = NumOrNull(t._t);
        j["p"] = NumOrNull(t._p);
        j["std_beta"] = NumOrNull(t._std_beta);
        j["dropped"] = t._dropped;
        j["drop_reason"] = t._drop_reason;
        arr.push_back(std::move(j));
    }
    return arr;
}

nlohmann::json VifToJson(const std::vector<double>& vif) {
    nlohmann::json a = nlohmann::json::array();
    for (double v : vif) a.push_back(NumOrNull(v));
    return a;
}

nlohmann::json WarningsToJson(const std::vector<std::string>& w) {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& s : w) a.push_back(s);
    return a;
}

// ════════════════════════════════════════════════════════════════════════════
// 响应序列化（schema 必须与 Python 侧逐字同构）
// ════════════════════════════════════════════════════════════════════════════

nlohmann::json ToJson(const regression::CrossSectionResult& r,
                      const std::vector<std::string>& symbols) {
    const int T = static_cast<int>(r._beta.rows());
    const int K = static_cast<int>(r._terms.size());
    nlohmann::json out;
    out["mode"] = "cross_section";
    out["dates"] = r._dates;
    out["symbols"] = symbols;
    out["terms"] = TermsToJson(r._terms);
    out["vif"] = VifToJson(r._vif);
    out["cond"] = NumOrNull(r._cond);
    out["warnings"] = WarningsToJson(r._warnings);
    out["beta"] = MatToJson(r._beta);
    out["resid"] = MatToJson(r._resid);
    out["r2"] = VecToJson(r._r2);
    out["n_names"] = r._n_names;
    out["dropped"] = DroppedToNested(r._dropped, T, K);
    return out;
}

nlohmann::json ToJson(const regression::TimeSeriesResult& r,
                      const std::vector<std::string>& dates) {
    const int N = static_cast<int>(r._beta.rows());
    const int K = static_cast<int>(r._terms.size());
    nlohmann::json out;
    out["mode"] = "time_series";
    out["dates"] = dates;
    out["symbols"] = r._symbols;
    out["terms"] = TermsToJson(r._terms);
    out["vif"] = VifToJson(r._vif);
    out["cond"] = NumOrNull(r._cond);
    out["warnings"] = WarningsToJson(r._warnings);
    out["beta"] = MatToJson(r._beta);
    out["se"] = MatToJson(r._se);
    out["t"] = MatToJson(r._tstat);
    out["r2"] = VecToJson(r._r2);
    out["n_obs"] = r._n_obs;
    out["dropped"] = DroppedToNested(r._dropped, N, K);
    return out;
}

nlohmann::json ToJson(const regression::PanelResult& r,
                      const std::vector<std::string>& dates,
                      const std::vector<std::string>& symbols) {
    const int K = static_cast<int>(r._terms.size());
    nlohmann::json out;
    out["mode"] = "panel";
    out["dates"] = dates;
    out["symbols"] = symbols;
    out["terms"] = TermsToJson(r._terms);
    // PanelResult 结构体没有 vif/cond 字段（双向 FE 去均值后 VIF 的意义有限），
    // Python 侧同样固定返回 vif=[0.0]*K、cond=0.0。
    // 两侧必须一致，否则 parity 的 _compare_fit 会在 cond 上失败。
    out["vif"] = nlohmann::json::array();
    for (int k = 0; k < K; ++k) out["vif"].push_back(0.0);
    out["cond"] = 0.0;
    out["warnings"] = WarningsToJson(r._warnings);
    out["r2"] = NumOrNull(r._r2);
    out["adj_r2"] = NumOrNull(r._adj_r2);
    out["n_obs"] = r._n_obs;
    return out;
}

// ════════════════════════════════════════════════════════════════════════════
// spec 解析
// ════════════════════════════════════════════════════════════════════════════

/// 解析出 PanelData + 因子清单
struct ParsedSpec {
    regression::PanelData _panel;
    std::vector<std::string> _xnames;
    std::vector<std::string> _fnames;
    std::vector<std::string> _groupnames;
    regression::SeType _se = regression::SeType::IID;
    regression::FitOptions _opt;
    std::string _mode;
};

bool ParseCommon(const nlohmann::json& spec, const Limits& limits,
                 ParsedSpec& p, SpecError& err) {
    if (!spec.is_object()) {
        return Fail(err, 400, "请求体必须是 JSON 对象");
    }

    // 变体 B（服务端取数）尚未实现——明确拒绝而不是静默忽略
    if (spec.contains("source")) {
        return Fail(err, 400,
                    "source 取数（变体 B）尚未实现；当前只支持面板直传（变体 A）");
    }

    if (!spec.contains("mode") || !spec["mode"].is_string()) {
        return Fail(err, 400, "缺少字段: mode");
    }
    p._mode = spec["mode"].get<std::string>();
    if (p._mode != "cross_section" && p._mode != "time_series" && p._mode != "panel") {
        return Fail(err, 400, "未知 mode: " + p._mode +
                    "（可选 cross_section / time_series / panel）");
    }

    if (!ReadStringArray(spec, "dates", p._panel._dates, err)) return false;
    if (!ReadStringArray(spec, "symbols", p._panel._symbols, err)) return false;

    const int T = static_cast<int>(p._panel._dates.size());
    const int N = static_cast<int>(p._panel._symbols.size());
    if (T == 0) return Fail(err, 400, "dates 不能为空");
    if (N == 0) return Fail(err, 400, "symbols 不能为空");

    // 规模上限：超限直接 400，避免同步计算占满 httplib 工作线程
    if (N > limits._max_symbols) {
        return Fail(err, 400, "标的数超限: " + std::to_string(N) +
                    " > " + std::to_string(limits._max_symbols));
    }
    if (T > limits._max_dates) {
        return Fail(err, 400, "日期数超限: " + std::to_string(T) +
                    " > " + std::to_string(limits._max_dates));
    }
    const long long cells = static_cast<long long>(T) * static_cast<long long>(N);
    if (cells > limits._max_cells) {
        return Fail(err, 400, "面板单元格数超限: " + std::to_string(cells) +
                    " > " + std::to_string(limits._max_cells) +
                    "（大规模分析请先落缓存、再按 spec 离线跑）");
    }

    if (!spec.contains("y")) return Fail(err, 400, "缺少字段: y");
    if (!ReadMatrix(spec["y"], T, N, "y", p._panel._Y, err)) return false;

    if (spec.contains("x") && !spec["x"].is_null()) {
        if (!spec["x"].is_object()) return Fail(err, 400, "x 必须是对象");
        for (auto it = spec["x"].begin(); it != spec["x"].end(); ++it) {
            regression::Mat m;
            if (!ReadMatrix(it.value(), T, N, "x." + it.key(), m, err)) return false;
            p._panel._X[it.key()] = std::move(m);
        }
    }

    if (spec.contains("f") && !spec["f"].is_null()) {
        if (!spec["f"].is_object()) return Fail(err, 400, "f 必须是对象");
        for (auto it = spec["f"].begin(); it != spec["f"].end(); ++it) {
            regression::Vec v;
            if (!ReadSeries(it.value(), T, "f." + it.key(), v, err)) return false;
            p._panel._F[it.key()] = std::move(v);
        }
    }

    if (spec.contains("groups") && !spec["groups"].is_null()) {
        if (!spec["groups"].is_object()) return Fail(err, 400, "groups 必须是对象");
        for (auto it = spec["groups"].begin(); it != spec["groups"].end(); ++it) {
            std::vector<std::string> labels;
            if (!ReadGroupLabels(it.value(), N, "groups." + it.key(), labels, err)) {
                return false;
            }
            p._panel._groups[it.key()] = std::move(labels);
        }
    }

    // 因子清单：显式给出，不从列名解析
    if (spec.contains("xnames")) {
        if (!ReadStringArray(spec, "xnames", p._xnames, err)) return false;
    }
    if (spec.contains("fnames")) {
        if (!ReadStringArray(spec, "fnames", p._fnames, err)) return false;
    }
    if (spec.contains("groupnames")) {
        if (!ReadStringArray(spec, "groupnames", p._groupnames, err)) return false;
    }

    // 引用的因子必须存在于数据里——这是 400，不是内核 warning
    for (const auto& xn : p._xnames) {
        if (p._panel._X.find(xn) == p._panel._X.end()) {
            return Fail(err, 400, "xnames 引用了不存在的截面因子: " + xn);
        }
    }
    for (const auto& fn : p._fnames) {
        if (p._panel._F.find(fn) == p._panel._F.end()) {
            return Fail(err, 400, "fnames 引用了不存在的时序因子: " + fn);
        }
    }
    for (const auto& gn : p._groupnames) {
        if (p._panel._groups.find(gn) == p._panel._groups.end()) {
            return Fail(err, 400, "groupnames 引用了不存在的分组变量: " + gn);
        }
    }

    const std::string se = spec.value("se", std::string("iid"));
    if (se == "iid") {
        p._se = regression::SeType::IID;
    } else if (se == "hac") {
        p._se = regression::SeType::HAC;
    } else if (se == "cluster") {
        p._se = regression::SeType::Cluster;
    } else {
        return Fail(err, 400, "未知 se: " + se + "（可选 iid / hac / cluster）");
    }

    return ReadOptions(spec, p._opt, err);
}

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// 入口
// ════════════════════════════════════════════════════════════════════════════

bool Execute(const nlohmann::json& spec, const Limits& limits,
             nlohmann::json& out, SpecError& err) {
    ParsedSpec p;
    if (!ParseCommon(spec, limits, p, err)) return false;

    try {
        if (p._mode == "cross_section") {
            auto r = regression::crossSection(p._panel, p._xnames, p._fnames,
                                              p._groupnames, p._opt, p._se);
            out = ToJson(r, p._panel._symbols);
        } else if (p._mode == "time_series") {
            auto r = regression::timeSeries(p._panel, p._xnames, p._fnames,
                                            p._opt, p._se);
            out = ToJson(r, p._panel._dates);
        } else {
            auto r = regression::panel(p._panel, p._xnames, p._opt, p._se);
            out = ToJson(r, p._panel._dates, p._panel._symbols);
        }
    } catch (const std::exception& e) {
        return Fail(err, 500, std::string("回归内核异常: ") + e.what());
    } catch (...) {
        return Fail(err, 500, "回归内核未知异常");
    }

    return true;
}

bool Execute(const nlohmann::json& spec, nlohmann::json& out, SpecError& err) {
    return Execute(spec, Limits{}, out, err);
}

}  // namespace regression_spec
