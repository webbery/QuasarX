#include "Util/provenance.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace prov {

// ============================================================================
// SHA-256（自包含实现）
// ============================================================================

namespace {

constexpr uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

} // namespace

void Sha256::reset() {
    _h[0] = 0x6a09e667u; _h[1] = 0xbb67ae85u; _h[2] = 0x3c6ef372u; _h[3] = 0xa54ff53au;
    _h[4] = 0x510e527fu; _h[5] = 0x9b05688cu; _h[6] = 0x1f83d9abu; _h[7] = 0x5be0cd19u;
    _totalLen = 0;
    _bufLen = 0;
    std::memset(_buf, 0, sizeof(_buf));
}

void Sha256::processBlock(const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = _h[0], b = _h[1], c = _h[2], d = _h[3];
    uint32_t e = _h[4], f = _h[5], g = _h[6], h = _h[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    _h[0] += a; _h[1] += b; _h[2] += c; _h[3] += d;
    _h[4] += e; _h[5] += f; _h[6] += g; _h[7] += h;
}

void Sha256::update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    _totalLen += len;

    if (_bufLen > 0) {
        const size_t need = sizeof(_buf) - _bufLen;
        const size_t take = (len < need) ? len : need;
        std::memcpy(_buf + _bufLen, p, take);
        _bufLen += take;
        p += take;
        len -= take;
        if (_bufLen == sizeof(_buf)) {
            processBlock(_buf);
            _bufLen = 0;
        }
    }

    while (len >= 64) {
        processBlock(p);
        p += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(_buf, p, len);
        _bufLen = len;
    }
}

String Sha256::finalHex() {
    // 补位：0x80 + 0...0 + 64-bit 大端比特长度
    const uint64_t bitLen = _totalLen * 8ull;
    uint8_t pad[72];
    size_t padLen = 0;
    pad[padLen++] = 0x80;
    const size_t rem = _bufLen + 1;
    const size_t zeros = (rem % 64 <= 56) ? (56 - rem % 64) : (120 - rem % 64);
    for (size_t i = 0; i < zeros; ++i) pad[padLen++] = 0x00;
    for (int i = 7; i >= 0; --i) {
        pad[padLen++] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xffu);
    }

    size_t idx = 0;
    while (idx < padLen) {
        const size_t need = sizeof(_buf) - _bufLen;
        const size_t take = (padLen - idx < need) ? (padLen - idx) : need;
        std::memcpy(_buf + _bufLen, pad + idx, take);
        _bufLen += take;
        idx += take;
        if (_bufLen == sizeof(_buf)) {
            processBlock(_buf);
            _bufLen = 0;
        }
    }

    static const char* HEX = "0123456789abcdef";
    String out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out += HEX[(_h[i] >> shift) & 0xfu];
        }
    }
    return out;
}

String sha256Hex(const void* data, size_t len) {
    Sha256 s;
    s.update(data, len);
    return s.finalHex();
}

String sha256String(const String& s) {
    return sha256Hex(s.data(), s.size());
}

String sha256File(const String& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return String();

    Sha256 s;
    char buf[65536];
    while (ifs.good()) {
        ifs.read(buf, sizeof(buf));
        const std::streamsize got = ifs.gcount();
        if (got > 0) s.update(buf, static_cast<size_t>(got));
    }
    if (ifs.bad()) return String();
    return s.finalHex();
}

String shortHash(const String& hex, size_t n) {
    return hex.size() <= n ? hex : hex.substr(0, n);
}

// ============================================================================
// 策略图语义投影（fail-closed：排除已知非语义字段，未知字段一律纳入）
// ============================================================================

namespace {

/**
 * 投影所在的层级。层级决定用哪张排除表，并给骨架指纹一个稳定的命名空间
 * （用层级而非实体 id/路径 → 骨架不随节点数量变化）
 */
enum class Lv { Script, Node, NodeData, Param, Edge, Generic };

const char* levelName(Lv lv) {
    switch (lv) {
    case Lv::Script: return "script";
    case Lv::Node: return "node";
    case Lv::NodeData: return "nodeData";
    case Lv::Param: return "param";
    case Lv::Edge: return "edge";
    default: return "generic";
    }
}

/**
 * 非语义字段排除表（硬编码）。
 *
 * 只有两类来源，变更频率极低：
 *   1. Vue Flow 运行时几何/交互状态（position/handleBounds/sourceX/…）
 *   2. param 的 UI 元数据（label/min/max/type/…）
 * 注意 node 的 data.label 与 param 的 label 语义不同：前者是下游 FormulaNode
 * 按名引用变量的标识（语义），后者只是控件文案（非语义）。
 */
bool isExcluded(Lv lv, const String& key) {
    // 下划线前缀约定为自定义元数据（如 _prov），一律不算语义
    if (!key.empty() && key[0] == '_') return true;

    switch (lv) {
    case Lv::Script:
        // name/id/description/version 是命名元数据，不是图内容；
        // 它们单独记进 manifest，不参与图指纹
        return key == "name" || key == "id" || key == "description" || key == "version";

    case Lv::Node:
        return key == "position" || key == "computedPosition" || key == "dimensions" ||
               key == "handleBounds" || key == "dragging" || key == "resizing" ||
               key == "selected" || key == "initialized" || key == "isParent" ||
               key == "events" || key == "type";

    case Lv::Param:
        return key == "label" || key == "placeholder" || key == "min" || key == "max" ||
               key == "step" || key == "unit" || key == "type" || key == "visible" ||
               key == "options" || key == "pattern" || key == "errorMsg";

    case Lv::Edge:
        // 几何/样式：sourceX/sourceY/targetX/targetY 是拖拽产生的浮点；
        // sourceNode/targetNode 内嵌整个节点对象（含 computedPosition）
        return key == "id" || key == "sourceX" || key == "sourceY" ||
               key == "targetX" || key == "targetY" || key == "sourceNode" ||
               key == "targetNode" || key == "style" || key == "markerEnd" ||
               key == "label" || key == "type" || key == "events" || key == "zIndex";

    case Lv::NodeData:
    case Lv::Generic:
    default:
        return false;
    }
}

/** 子字段所在的层级；数组元素沿用数组所在层级的子层级 */
Lv childLevel(Lv lv, const String& key) {
    switch (lv) {
    case Lv::Script:
        if (key == "nodes") return Lv::Node;
        if (key == "edges") return Lv::Edge;
        // backtest.{start,end} 仍是脚本级配置（骨架里记为 script:start 而非 generic:start）
        if (key == "backtest") return Lv::Script;
        return Lv::Generic;
    case Lv::Node:
        if (key == "data") return Lv::NodeData;
        return Lv::Generic;
    case Lv::NodeData:
        if (key == "params") return Lv::Param;
        return Lv::Generic;
    case Lv::Param:
        // param 的值对象（如 ProtectionNode 的 {enabled, percent}）里的键
        // 仍是 param 语义字段
        return Lv::Param;
    case Lv::Edge:
        // edge.data.imfIndex 是边级语义
        return Lv::Edge;
    default:
        return Lv::Generic;
    }
}

void projectValue(const nlohmann::json& v, Lv lv, nlohmann::json& out, Set<String>& shape);

void projectObject(const nlohmann::json& src, Lv lv, nlohmann::json& out, Set<String>& shape) {
    out = nlohmann::json::object();
    for (auto it = src.begin(); it != src.end(); ++it) {
        const String& k = it.key();
        if (isExcluded(lv, k)) continue;
        // 骨架只登记语义字段。被排除的字段（Vue Flow 几何/交互、param UI 元数据）
        // 照登会让"拖动一下节点"凭空多出 node:handleBounds / edge:sourceX 等键，
        // shape_digest 随之变化 —— 而拖动不代表图结构变了。
        // 真正未知的字段一定不在排除表里，仍会登记并报警。
        shape.insert(String(levelName(lv)) + ":" + k);
        nlohmann::json child;
        projectValue(it.value(), childLevel(lv, k), child, shape);
        out[k] = std::move(child);
    }
}

void projectValue(const nlohmann::json& v, Lv lv, nlohmann::json& out, Set<String>& shape) {
    if (v.is_object()) {
        projectObject(v, lv, out, shape);
    } else if (v.is_array()) {
        out = nlohmann::json::array();
        for (const auto& e : v) {
            nlohmann::json item;
            projectValue(e, lv, item, shape);
            out.push_back(std::move(item));
        }
    } else {
        out = v;
    }
}

/** 数值统一经 double + %.17g，消除 int/float 与科学计数法写法差异 */
void appendNumber(String& out, const nlohmann::json& v) {
    if (v.is_number_unsigned()) {
        out += std::to_string(v.get<uint64_t>());
    } else if (v.is_number_integer()) {
        out += std::to_string(v.get<int64_t>());
    } else {
        const double d = v.get<double>();
        if (std::isnan(d)) { out += "nan"; return; }
        if (std::isinf(d)) { out += (d > 0) ? "inf" : "-inf"; return; }
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.17g", d);
        out += buf;
    }
}

void dumpCanonical(const nlohmann::json& v, String& out) {
    switch (v.type()) {
    case nlohmann::json::value_t::null:
        out += "null";
        break;
    case nlohmann::json::value_t::boolean:
        out += v.get<bool>() ? "true" : "false";
        break;
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned:
    case nlohmann::json::value_t::number_float:
        appendNumber(out, v);
        break;
    case nlohmann::json::value_t::string:
        out += v.dump();
        break;
    case nlohmann::json::value_t::array: {
        out += '[';
        bool first = true;
        for (const auto& e : v) {
            if (!first) out += ',';
            first = false;
            dumpCanonical(e, out);
        }
        out += ']';
        break;
    }
    case nlohmann::json::value_t::object: {
        // nlohmann 的 object 底层是 std::map<std::string, ...>，迭代即字节序，
        // 与 locale 无关，跨平台一致
        out += '{';
        bool first = true;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (!first) out += ',';
            first = false;
            out += nlohmann::json(it.key()).dump();
            out += ':';
            dumpCanonical(it.value(), out);
        }
        out += '}';
        break;
    }
    default:
        out += "?";
        break;
    }
}

/** 排序键：畸形图（nodes 里混入非对象）不能抛异常，否则回测会被指纹拖崩 */
String sortKeyOf(const nlohmann::json& j) {
    if (j.is_object()) {
        auto it = j.find("id");
        if (it != j.end() && it->is_string()) return it->get<String>();
    }
    return String("~") + j.dump();  // 非对象/无 id 排后面，dump 保证确定性
}

/** 把投影结果里的数组顺序归一：nodes 按 id，edges 按规范串 */
void normalizeOrder(nlohmann::json& proj) {
    if (proj.contains("nodes") && proj["nodes"].is_array()) {
        auto& nodes = proj["nodes"];
        std::sort(nodes.begin(), nodes.end(),
                  [](const nlohmann::json& a, const nlohmann::json& b) {
                      const String ia = sortKeyOf(a);
                      const String ib = sortKeyOf(b);
                      if (ia != ib) return ia < ib;
                      return a.dump() < b.dump();
                  });
    }
    if (proj.contains("edges") && proj["edges"].is_array()) {
        auto& edges = proj["edges"];
        for (auto& e : edges) {
            // 缺失 / null 与 {} 语义相同，统一成 {} 以免被当成差异
            if (!e.is_object()) e = nlohmann::json::object();
            if (!e.contains("data") || e["data"].is_null()) {
                e["data"] = nlohmann::json::object();
            }
        }
        std::sort(edges.begin(), edges.end(),
                  [](const nlohmann::json& a, const nlohmann::json& b) {
                      return a.dump() < b.dump();
                  });
    }
}

} // namespace

String canonicalizeStrategy(const nlohmann::json& script) {
    if (!script.is_object()) return String();
    Set<String> shape;
    nlohmann::json proj;
    projectValue(script, Lv::Script, proj, shape);
    normalizeOrder(proj);
    String out;
    dumpCanonical(proj, out);
    return out;
}

String strategyShape(const nlohmann::json& script) {
    if (!script.is_object()) return String();
    Set<String> shape;
    nlohmann::json proj;
    projectValue(script, Lv::Script, proj, shape);
    String out;
    for (const auto& s : shape) {
        if (!out.empty()) out += '\n';
        out += s;
    }
    return out;
}

StrategyFingerprint fingerprintStrategy(const nlohmann::json& script) {
    StrategyFingerprint fp;
    if (!script.is_object()) {
        fp.err = "script is not a JSON object";
        return fp;
    }

    Set<String> shape;
    nlohmann::json proj;
    projectValue(script, Lv::Script, proj, shape);
    normalizeOrder(proj);

    if (proj.contains("nodes") && proj["nodes"].is_array()) fp.nodeCount = proj["nodes"].size();
    if (proj.contains("edges") && proj["edges"].is_array()) fp.edgeCount = proj["edges"].size();

    String canon;
    dumpCanonical(proj, canon);
    fp.hash = sha256String(canon);

    String shapeText;
    for (const auto& s : shape) {
        shapeText += s;
        shapeText += '\n';
    }
    fp.shapeDigest = sha256String(shapeText);
    return fp;
}

// ============================================================================
// 通用键路径骨架
// ============================================================================

namespace {

void collectKeyPaths(const nlohmann::json& v, const String& prefix, Set<String>& out) {
    if (v.is_object()) {
        for (auto it = v.begin(); it != v.end(); ++it) {
            const String p = prefix.empty() ? it.key() : prefix + "." + it.key();
            out.insert(p);
            collectKeyPaths(it.value(), p, out);
        }
    } else if (v.is_array()) {
        for (const auto& e : v) {
            collectKeyPaths(e, prefix + "[]", out);
        }
    }
}

} // namespace

String keyPathShape(const nlohmann::json& v) {
    Set<String> keys;
    collectKeyPaths(v, String(), keys);
    String out;
    for (const auto& k : keys) {
        if (!out.empty()) out += '\n';
        out += k;
    }
    return out;
}

String keyPathShapeDigest(const nlohmann::json& v) {
    return sha256String(keyPathShape(v));
}

String featureNamesHash(const Vector<String>& names) {
    String s;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) s += '\n';
        s += names[i];
    }
    return sha256String(s);
}

nlohmann::json metaField(const nlohmann::json& obj, const char* key, nlohmann::json fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    return it == obj.end() ? std::move(fallback) : *it;
}

// ============================================================================
// 运行时信息
// ============================================================================

String runtimeVersion() {
#ifdef QS_VERSION
    return QS_VERSION;
#else
    return String("unknown");
#endif
}

String runtimeBuildTime() {
    return String(__DATE__) + " " + String(__TIME__);
}

String runtimeSimdLevel() {
#ifdef QS_SIMD_LEVEL
    return QS_SIMD_LEVEL;
#else
    return String("unknown");
#endif
}

// ============================================================================
// manifest 落盘
// ============================================================================

bool writeProvenance(const String& dir, const nlohmann::json& manifest, String* err) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::exists(dir)) {
        if (err) *err = "cannot create directory: " + dir;
        return false;
    }

    const String jsonPath = dir + "/provenance.json";
    {
        std::ofstream ofs(jsonPath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            if (err) *err = "cannot open " + jsonPath;
            return false;
        }
        ofs << manifest.dump(2) << "\n";
    }

    // 历史追加：同一策略名多次回测会覆盖 CSV，但 manifest 不丢
    const String jsonlPath = dir + "/provenance.jsonl";
    {
        std::ofstream ofs(jsonlPath, std::ios::out | std::ios::app);
        if (!ofs.is_open()) {
            if (err) *err = "cannot open " + jsonlPath;
            return false;
        }
        ofs << manifest.dump() << "\n";
    }
    return true;
}

} // namespace prov
