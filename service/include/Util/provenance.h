#pragma once
#include "std_header.h"
#include "json.hpp"

/**
 * 产物版本指纹（provenance）
 *
 * 背景：策略文件 scripts/{name}、模型 production/{id}.json、特征缓存 CSV 三者在
 * 磁盘上都是"原地覆盖"，路径与 mtime 都不构成身份 —— 事后无法回答"这次回测到底
 * 用了哪一版策略图 / 哪一版模型"。
 *
 * ── 核心原则：fail-closed ──────────────────────────────────────────────
 * 每一层字段都采用「排除已知的非语义字段」，**未知字段一律纳入指纹**。
 * 反例（曾经的错误设计）是白名单 —— 只取已知语义字段，于是新增一个语义字段
 * （如 edge.data.imfIndex）会被静默漏检，两次不同的策略被判为"同一份"。
 *
 * 失效方向是不对称的：
 *   · 漏检 → 静默，永远发现不了        ← 危险
 *   · 误报 → hash 无端变化，会被察觉  ← 安全
 * 所以宁可误报。排除集只依赖两类几乎不变的来源（Vue Flow 运行时几何/交互字段、
 * param 的 UI 元数据），并由骨架指纹兜住遗漏。
 *
 * ── 双指纹 ────────────────────────────────────────────────────────────
 *   hash         值指纹    —— 图内容变了才变（跨 UI 噪声稳定）
 *   shapeDigest  骨架指纹  —— 语义字段名「层级:字段名」去重集合，字段增删改才变，
 *                             与取值无关，也与节点/边数量无关
 *
 * 两个指纹共用同一张排除表：被排除的非语义字段（Vue Flow 几何/交互状态、param
 * UI 元数据）在 hash 与 shape 里都不出现。因此"拖动节点"这类只碰几何量的操作
 * 两个指纹都不变；而任何**未知**字段都既进 hash 又进 shape，误报可见可诊断。
 *
 * 两者组合可自动判读差异类型：
 *   hash=  shape=  完全同一份
 *   hash=  shape≠  字段有增删但值没变 → 投影归类该复审
 *   hash≠  shape=  同一套字段、值变了 → 正常新版本
 *   hash≠  shape≠  结构与值都变 → 新版本 + 复审
 *
 * ── 规则版本 ──────────────────────────────────────────────────────────
 * scheme() 是投影规则的版本号。**规则一变必须 bump**，否则新旧记录里语义不同的
 * hash 会被当成可直接比较的。scheme 与 hash 分开存放，避免"规则变了"与"值变了"
 * 无法区分。
 *
 * ── 只在服务端算 ──────────────────────────────────────────────────────
 * 前端 POST 回来的 JSON 数字格式已被 JSON.stringify 改写（1e-06 → 0.000001），
 * 浏览器算出的 hash 与磁盘文件算出的必然不同。故哈希只在 C++ 侧计算。
 */
namespace prov {

/** 投影规则版本。规则变更（增删排除项、改归一化）时必须递增。 */
constexpr const char* kScheme = "prov-v1";

// ============================ 哈希 ============================

/**
 * @brief 流式 SHA-256
 *
 * 自包含纯 C++ 实现，不依赖 OpenSSL 开发包（运行/构建环境未必装 headers，
 * 引入头文件依赖会提高构建脆弱性）
 */
class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    String finalHex();

private:
    void processBlock(const uint8_t* block);

    uint32_t _h[8];
    uint64_t _totalLen;
    uint8_t _buf[64];
    size_t _bufLen;
};

String sha256Hex(const void* data, size_t len);
String sha256String(const String& s);

/** @brief 分块读取并哈希文件；不可读返回空串 */
String sha256File(const String& path);

/** @brief 截断 hex 用于显示/文件名（默认 12 位） */
String shortHash(const String& hex, size_t n = 12);

// ======================== 策略图指纹 ========================

struct StrategyFingerprint {
    String hash;         // 64 hex，语义值指纹
    String shapeDigest;  // 64 hex，字段骨架指纹
    size_t nodeCount = 0;
    size_t edgeCount = 0;
    String err;          // 非空表示输入不可用（hash/shapeDigest 为空）
};

/**
 * @brief 策略图双指纹
 *
 * 归一化处理：
 *   · 数值统一经 double + %.17g（消除 int/float 与科学计数法写法差异）
 *   · nodes 按 id 排序、edges 按其规范串排序（文件里的数组顺序不稳定）
 *     —— 数组元素自身顺序（如 Input 的 symbol 列表）保持原样，顺序变化算变更
 *   · edge.data 缺失/null → {}（空与缺失语义相同，不应算差异）
 */
StrategyFingerprint fingerprintStrategy(const nlohmann::json& script);

/** @brief 语义投影的规范字符串（调试/差异定位用） */
String canonicalizeStrategy(const nlohmann::json& script);

/** @brief 骨架清单（每行一个 "层级:字段名"，调试用） */
String strategyShape(const nlohmann::json& script);

// ===================== 通用键骨架（模型 meta 等） =====================

/**
 * @brief 任意 JSON 的嵌套键路径骨架指纹（值不参与）
 *
 * 用于模型 .meta.json：新增训练参数（如 early_stopping_rounds）会让骨架变化，
 * 从而暴露"元数据字段增删"而不必预先枚举所有字段。
 */
String keyPathShapeDigest(const nlohmann::json& v);

/** @brief 嵌套键路径清单（调试用） */
String keyPathShape(const nlohmann::json& v);

/** @brief 特征名（顺序敏感）指纹，用于检测训练/推理特征顺序漂移 */
String featureNamesHash(const Vector<String>& names);

/**
 * @brief 取只作记录用的元数据字段，原样返回其 JSON 值（缺失时返回 fallback）
 *
 * 存在的原因：obj.value("version", "") 会选中 const char* 重载 → get<std::string>()，
 * 遇到"版本号写成数值"的常见写法（"version": 1）就抛 type_error.302。这个异常会
 * 从 Handler 一路穿到 httplib，把整个请求打成 500（响应体为空），而它本该只是一条
 * 旁路记录。此函数不做类型断言，数值/字符串/对象都原样返回。
 */
nlohmann::json metaField(const nlohmann::json& obj, const char* key,
                         nlohmann::json fallback = nlohmann::json());

// ======================== 运行时信息 ========================

/** @brief git describe（由 CMake 注入 QS_VERSION） */
String runtimeVersion();

/** @brief 本 TU 的编译时间（近似二进制构建时间） */
String runtimeBuildTime();

/** @brief SIMD 档位（由 CMake 注入 QS_SIMD_LEVEL） */
String runtimeSimdLevel();

// ======================== manifest 落盘 ========================

/**
 * @brief 写 {dir}/provenance.json（覆盖）+ 追加一行到 {dir}/provenance.jsonl
 *
 * .jsonl 保留历史：同一策略名多次回测会覆盖 CSV，但 manifest 历史不丢。
 */
bool writeProvenance(const String& dir, const nlohmann::json& manifest, String* err = nullptr);

} // namespace prov
