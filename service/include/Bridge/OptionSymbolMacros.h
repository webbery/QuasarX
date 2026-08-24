#pragma once
#include "std_header.h"

// ═══════════════════════════════════════════════════════════
//  期权 symbol_t scale 扩展宏
// ═══════════════════════════════════════════════════════════
//
// 背景:
//   symbol_t 的 _price 字段 (10 bits, max 1023) 在 unit=100 编码下
//   最多表达 10.23 元/点, 只能覆盖 ETF 期权 strike 范围
//   (50ETF/300ETF/500ETF/STAR50ETF/SZSE 系列).
//
//   CFFEX 股指期权 (IO/HO/MO) strike 范围 2500~8000 点,
//   远超 1023, 需用更大的单位 (unit=1000) 才能放下.
//
// 方案:
//   不改动 symbol_t 位域布局, 用 _reserved 的 bit 0 做 scale 标志
//   ┌──────────────────────────────────────────────────────┐
//   │ bit 11..1   │  bit 0  │   _year    │ _month │  _price │
//   │  (保留)     │  scale  │    6b      │  4b    │   10b   │
//   └──────────────────────────────────────────────────────┘
//
//   scale = 0 → _price × OPT_STRIKE_UNIT_ETF    (0.01 元/单位, 范围 0~1023 元, 精度 0.01)
//   scale = 1 → _price × OPT_STRIKE_UNIT_INDEX  (10   点/单位, 范围 0~10230 点, 精度 10)
//              IO/HO/MO strikes (2500~8000) 全在范围内
//
//   对已有股票 / 旧 ETF 期权数据完全兼容 — 其 _reserved 全为 0, 默认 scale=0.
//
// 用法:
//   symbol_t sym = ...;
//   SET_SYMBOL_OPT_SCALE(sym, OPT_SCALE_INDEX);  // 标记为股指期权
//   uint8_t s = GET_SYMBOL_OPT_SCALE(sym);        // 读出 scale
//   double strike = decode_option_strike(sym);     // 自动按 scale 选单位
// ═══════════════════════════════════════════════════════════

// ── scale 取值 ──
#define OPT_SCALE_ETF     0   // _price 单位 100   (ETF 期权, 0~1023 元, 精度 0.01)
#define OPT_SCALE_INDEX   1   // _price 单位 10    (股指期权, 0~10230 点, 精度 10)

// ── _reserved 中使用的 bit 位置 ──
#define OPT_SCALE_BIT     0   // bit 0 of _reserved (LSB)

// ── strike 单位 (单位 × _price = 实际 strike) ──
// ETF mode:    _price × OPT_STRIKE_UNIT_ETF    = strike (元), 精度 0.01
// Index mode:  _price × OPT_STRIKE_UNIT_INDEX  = strike (点), 精度 10
#define OPT_STRIKE_UNIT_ETF     0.01     // 元/单位
#define OPT_STRIKE_UNIT_INDEX   10.0     // 点/单位

// ── strike → _price raw 转换因子 (即 1.0 / 单位) ──
#define OPT_PRICE_FACTOR_ETF    100.0    // strike × 100 = _price
#define OPT_PRICE_FACTOR_INDEX  0.1      // strike × 0.1  = _price

// ── 读 ──
#define GET_SYMBOL_OPT_SCALE(sym) \
    (static_cast<uint8_t>(((sym)._reserved >> OPT_SCALE_BIT) & 0x1u))

// ── 写 (保持其他位不变) ──
#define SET_SYMBOL_OPT_SCALE(sym, val)                                  \
    do {                                                                \
        (sym)._reserved = static_cast<uint32_t>(                        \
            ((sym)._reserved & ~(1u << OPT_SCALE_BIT))                   \
            | ((static_cast<uint32_t>(val) & 0x1u) << OPT_SCALE_BIT));  \
    } while (0)

// ── 便利: 解码 strike (按 scale 自动选单位) ──
inline double decode_option_strike(symbol_t sym) {
    const uint8_t scale = GET_SYMBOL_OPT_SCALE(sym);
    return (scale == OPT_SCALE_INDEX)
         ? (static_cast<double>(sym._price) * OPT_STRIKE_UNIT_INDEX)
         : (static_cast<double>(sym._price) * OPT_STRIKE_UNIT_ETF);
}

// ── 便利: 编码 strike (返回 true 表示成功, false 表示 _price 溢出) ──
// 注意: sym 通过引用传入, 函数会就地修改
inline bool encode_option_strike(symbol_t& sym, double strike, uint8_t scale) {
    uint32_t raw;
    if (scale == OPT_SCALE_INDEX) {
        raw = static_cast<uint32_t>(strike * OPT_PRICE_FACTOR_INDEX + 0.5);  // 四舍五入到 10 点
    } else {
        raw = static_cast<uint32_t>(strike * OPT_PRICE_FACTOR_ETF + 0.5);    // 四舍五入到 1 分
    }
    if (raw > 1023u) return false;  // _price 10 bits 溢出
    sym._price = static_cast<uint16_t>(raw);
    SET_SYMBOL_OPT_SCALE(sym, scale);
    return true;
}

// ── 检测字符串是否为 CFFEX 股指期权格式 ──
//   格式: "IO/HO/MO" + 4 位年月 (YYMM) + "-" + C/P + "-" + strike
//   例:   "IO2401-C-3800", "HO2403-P-3000", "MO2406-C-7000"
inline bool is_cffex_option_format(const String& code) {
    if (code.size() < 11) return false;  // "MO2401-C-1" 最短 11 字符
    // 前缀: IO / HO / MO
    String prefix = code.substr(0, 2);
    if (prefix != "IO" && prefix != "HO" && prefix != "MO") return false;
    // 第 2-5 位: YYMM (4 位数字)
    for (size_t i = 2; i < 6; ++i) {
        char c = code[i];
        if (c < '0' || c > '9') return false;
    }
    // 第 6 位: '-'
    if (code[6] != '-') return false;
    // 第 7 位: C 或 P
    if (code[7] != 'C' && code[7] != 'P') return false;
    // 第 8 位: '-'
    if (code[8] != '-') return false;
    // 第 9 位起: strike (至少 1 位数字)
    for (size_t i = 9; i < code.size(); ++i) {
        char c = code[i];
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// ── 解析 CFFEX 格式并填充 symbol_t ──
//   返回 true 表示成功填充 (含 strike 编码), false 表示格式不匹配或溢出
inline bool parse_cffex_option(symbol_t& sym, const String& code) {
    if (!is_cffex_option_format(code)) return false;
    try {
        sym._year  = static_cast<uint32_t>(std::stoi(code.substr(2, 2))) & 0x3F;   // 6 bits
        sym._month = static_cast<uint32_t>(std::stoi(code.substr(4, 2))) & 0x0F;   // 4 bits
        sym._type  = (code[7] == 'C') ? contract_type::call : contract_type::put;
        sym._exchange = static_cast<char>(2);  // 2 = CFFEX (约定, 与 Server 内部一致)
        double strike = std::stod(code.substr(9));
        return encode_option_strike(sym, strike, OPT_SCALE_INDEX);
    } catch (...) {
        return false;
    }
}
