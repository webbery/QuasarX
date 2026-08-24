#include "Util/OptionDataDB.h"
#include "Util/system.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Bridge/OptionSymbolMacros.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
//  单例
// ═══════════════════════════════════════════════════════════

OptionDataDB& OptionDataDB::instance() {
    static OptionDataDB inst;
    return inst;
}

OptionDataDB::~OptionDataDB() = default;

// ═══════════════════════════════════════════════════════════
//  建表 (symbol_id BIGINT 取代 contract_code VARCHAR)
// ═══════════════════════════════════════════════════════════

void OptionDataDB::ensureTables() {
    exec(
        "CREATE TABLE IF NOT EXISTS option_daily ("
        "    id                  INTEGER,"
        "    trade_date          TIMESTAMP NOT NULL,"
        "    symbol_id           BIGINT NOT NULL,"        //-- packed symbol_t (8 字节)
        "    exchange            VARCHAR NOT NULL,"       //-- 'CFFEX' / 'SSE' / 'SZSE'
        "    product             VARCHAR NOT NULL,"       //-- 'IO' / '50ETF' / '159919' ...
        "    underlying          VARCHAR,"               //-- 标的代码 (e.g. '510050')
        "    contract_name       VARCHAR NOT NULL,"      // -- 人类可读: 'IO2401-C-3800'
        "    call_put            VARCHAR,"               //-- '认购' / '认沽'
        "    strike_price        DOUBLE,"                //-- 显式存 (避免 symbol_t 解码精度损失)
        "    open                DOUBLE,"
        "    high                DOUBLE,"
        "    low                 DOUBLE,"
        "    close               DOUBLE,"
        "    settlement          DOUBLE,"
        "    prev_settlement     DOUBLE,"
        "    volume              BIGINT,"
        "    turnover            DOUBLE,"
        "    open_interest       BIGINT,"
        "    exercise_volume     BIGINT,"
        "    delta               DOUBLE,"
        "    gamma               DOUBLE,"
        "    vega                DOUBLE,"
        "    theta               DOUBLE,"
        "    implied_volatility  DOUBLE,"
        "    UNIQUE(symbol_id, trade_date)"
        ")"
    );

    exec("CREATE INDEX IF NOT EXISTS idx_option_daily_symbol "
         "ON option_daily(symbol_id, trade_date)");
    exec("CREATE INDEX IF NOT EXISTS idx_option_daily_exchange_product "
         "ON option_daily(exchange, product, trade_date)");
}

// ═══════════════════════════════════════════════════════════
//  合约字符串 → symbol_t 编码
//  委托 to_symbol (system.cpp 已扩展 CFFEX 格式检测)
// ═══════════════════════════════════════════════════════════

symbol_t OptionDataDB::encodeContract(const String& exchange,
                                       const String& contract_code,
                                       const String& contract_name,
                                       double strike) {
    (void)contract_name;  // to_symbol 内部处理
    (void)strike;         // to_symbol 内部从 contract_code 解析

    // ── SSE/SZSE ETF 期权: 8 位数字 (如 "10003187") ──
    // ETFOptionSymbol 内部通过 Server::GetSecurity 获取到期日等
    if ((exchange == "SSE" || exchange == "SZSE") && contract_code.size() == 8) {
        try {
            ETFOptionSymbol etf_opt(contract_code, contract_name);
            symbol_t sym = static_cast<symbol_t>(etf_opt);
            SET_SYMBOL_OPT_SCALE(sym, OPT_SCALE_ETF);
            return sym;
        } catch (...) {
            WARN("[OptionDataDB] encodeContract SSE/SZSE failed: {}", contract_code);
            return symbol_t{};
        }
    }

    // ── CFFEX / 其他: 委托 to_symbol ──
    // to_symbol 内部已扩展 parse_cffex_option 兜底解析
    return to_symbol(contract_code);
}

int64_t OptionDataDB::encodeContractId(const String& exchange,
                                        const String& contract_code,
                                        const String& contract_name,
                                        double strike) {
    symbol_t sym = encodeContract(exchange, contract_code, contract_name, strike);
    int64_t id = 0;
    std::memcpy(&id, &sym, sizeof(symbol_t));
    return id;
}

// ═══════════════════════════════════════════════════════════
//  工具: 从路径推断 exchange
// ═══════════════════════════════════════════════════════════

static String infer_exchange_from_path(const String& csv_path) {
    fs::path p(csv_path);
    for (auto it = p.begin(); it != p.end(); ++it) {
        String comp = it->string();
        std::transform(comp.begin(), comp.end(), comp.begin(), ::tolower);
        if (comp == "cffex") return "CFFEX";
        if (comp == "sse")   return "SSE";
        if (comp == "szse")  return "SZSE";
    }
    String fname = p.filename().string();
    std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
    if (fname.find("cffex") != String::npos || fname.find("io") == 0 || fname.find("ho") == 0 || fname.find("mo") == 0)
        return "CFFEX";
    if (fname.find("sse") != String::npos) return "SSE";
    if (fname.find("szse") != String::npos) return "SZSE";
    return "UNKNOWN";
}

// ═══════════════════════════════════════════════════════════
//  CSV 导入
// ═══════════════════════════════════════════════════════════

int OptionDataDB::importCsv(const String& csv_path) {
    std::lock_guard<std::recursive_mutex> lock(mtx());

    std::ifstream ifs(csv_path);
    if (!ifs.is_open()) {
        SPDLOG_ERROR("[OptionDataDB] Cannot open: {}", csv_path);
        return -1;
    }

    String exchange = infer_exchange_from_path(csv_path);

    // ── 解析 header ──
    String header_line;
    if (!std::getline(ifs, header_line)) {
        SPDLOG_WARN("[OptionDataDB] Empty file: {}", csv_path);
        return 0;
    }
    if (header_line.size() >= 3 &&
        header_line[0] == '\xEF' && header_line[1] == '\xBB' && header_line[2] == '\xBF') {
        header_line = header_line.substr(3);
    }

    Vector<String> csv_headers;
    {
        std::istringstream ss(header_line);
        String tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty() && tok.back() == '\r') tok.pop_back();
            csv_headers.push_back(tok);
        }
    }

    auto colIdx = [&](const String& name) -> int {
        for (size_t i = 0; i < csv_headers.size(); i++) {
            if (csv_headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    int c_trade_date = colIdx("trade_date");
    int c_product    = colIdx("product");
    int c_underlying = colIdx("underlying");
    int c_contract   = colIdx("contract_code");
    int c_name       = colIdx("contract_name");
    int c_call_put   = colIdx("call_put");
    int c_strike     = colIdx("strike_price");
    int c_open       = colIdx("open");
    int c_high       = colIdx("high");
    int c_low        = colIdx("low");
    int c_close      = colIdx("close");
    int c_settle     = colIdx("settlement");
    int c_prev_set   = colIdx("prev_settlement");
    int c_volume     = colIdx("volume");
    int c_turnover   = colIdx("turnover");
    int c_oi         = colIdx("open_interest");
    int c_exercise   = colIdx("exercise_volume");
    int c_delta      = colIdx("delta");
    int c_gamma      = colIdx("gamma");
    int c_vega       = colIdx("vega");
    int c_theta      = colIdx("theta");
    int c_iv         = colIdx("implied_volatility");

    if (c_trade_date < 0 || c_contract < 0) {
        SPDLOG_ERROR("[OptionDataDB] CSV missing required columns (trade_date/contract_code): {}", csv_path);
        return -1;
    }

    auto safeDouble = [](const String& s) -> std::pair<bool, double> {
        if (s.empty()) return {false, 0.0};
        try { return {true, std::stod(s)}; } catch (...) { return {false, 0.0}; }
    };
    auto safeInt = [](const String& s) -> std::pair<bool, int64_t> {
        if (s.empty()) return {false, 0};
        try { return {true, std::stoll(s)}; } catch (...) { return {false, 0}; }
    };
    auto getField = [&](const Vector<String>& cols, int idx) -> String {
        if (idx >= 0 && idx < static_cast<int>(cols.size())) return cols[idx];
        return "";
    };

    // ── 解析数据行 ──
    struct Row {
        String trade_date;
        String exchange;
        String product;
        String underlying;
        String contract_code;
        String contract_name;
        String call_put;
        int64_t symbol_id = 0;
        double strike_price = 0;
        double open = 0, high = 0, low = 0, close = 0;
        double settlement = 0, prev_settlement = 0;
        int64_t volume = 0;
        double turnover = 0;
        int64_t open_interest = 0, exercise_volume = 0;
        double delta = 0, gamma = 0, vega = 0, theta = 0;
        double implied_volatility = 0;
        bool has_strike = false, has_open = false, has_high = false, has_low = false;
        bool has_close = false, has_settle = false, has_prev_set = false;
        bool has_volume = false, has_turnover = false;
        bool has_oi = false, has_exercise = false;
        bool has_delta = false, has_gamma = false, has_vega = false;
        bool has_theta = false, has_iv = false;
    };

    Vector<Row> rows;
    rows.reserve(256);

    String line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream ss(line);
        String tok;
        Vector<String> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);

        Row r;
        r.trade_date     = getField(cols, c_trade_date);
        r.product        = getField(cols, c_product);
        r.underlying     = getField(cols, c_underlying);
        r.contract_code  = getField(cols, c_contract);
        r.contract_name  = getField(cols, c_name);
        r.call_put       = getField(cols, c_call_put);
        r.exchange       = exchange;

        if (r.trade_date.empty() || r.contract_code.empty()) continue;
        if (r.contract_name.empty()) r.contract_name = r.contract_code;  // fallback

        // 标的代码从 product/contract 前缀推断
        if (r.underlying.empty() && !r.product.empty()) {
            r.underlying = r.product;
        }

        // ★ 编码 contract → symbol_id (委托 to_symbol)
        auto [ok_s, v_s] = safeDouble(getField(cols, c_strike));
        r.strike_price = v_s;
        r.has_strike = ok_s;
        r.symbol_id = encodeContractId(exchange, r.contract_code, r.contract_name, v_s);

        auto ok_open    = safeDouble(getField(cols, c_open));         r.open        = ok_open.second;    r.has_open    = ok_open.first;
        auto ok_high    = safeDouble(getField(cols, c_high));         r.high        = ok_high.second;    r.has_high    = ok_high.first;
        auto ok_low     = safeDouble(getField(cols, c_low));          r.low         = ok_low.second;     r.has_low     = ok_low.first;
        auto ok_close   = safeDouble(getField(cols, c_close));        r.close       = ok_close.second;   r.has_close   = ok_close.first;
        auto ok_settle  = safeDouble(getField(cols, c_settle));       r.settlement  = ok_settle.second;  r.has_settle  = ok_settle.first;
        auto ok_prevset = safeDouble(getField(cols, c_prev_set));     r.prev_settlement = ok_prevset.second; r.has_prev_set = ok_prevset.first;
        auto ok_vol     = safeInt(getField(cols, c_volume));          r.volume      = ok_vol.second;     r.has_volume  = ok_vol.first;
        auto ok_to      = safeDouble(getField(cols, c_turnover));    r.turnover    = ok_to.second;      r.has_turnover= ok_to.first;
        auto ok_oi      = safeInt(getField(cols, c_oi));              r.open_interest = ok_oi.second;    r.has_oi      = ok_oi.first;
        auto ok_ex      = safeInt(getField(cols, c_exercise));        r.exercise_volume = ok_ex.second;  r.has_exercise= ok_ex.first;
        auto ok_delta   = safeDouble(getField(cols, c_delta));        r.delta       = ok_delta.second;   r.has_delta   = ok_delta.first;
        auto ok_gamma   = safeDouble(getField(cols, c_gamma));        r.gamma       = ok_gamma.second;   r.has_gamma   = ok_gamma.first;
        auto ok_vega    = safeDouble(getField(cols, c_vega));         r.vega        = ok_vega.second;    r.has_vega    = ok_vega.first;
        auto ok_theta   = safeDouble(getField(cols, c_theta));        r.theta       = ok_theta.second;   r.has_theta   = ok_theta.first;
        auto ok_iv      = safeDouble(getField(cols, c_iv));           r.implied_volatility = ok_iv.second; r.has_iv    = ok_iv.first;

        rows.push_back(std::move(r));
    }
    ifs.close();

    if (rows.empty()) {
        SPDLOG_WARN("[OptionDataDB] No valid rows in {}", csv_path);
        return 0;
    }

    // ── 构建批量 INSERT ──
    String sql;
    sql.reserve(rows.size() * 384 + 512);
    sql +=
        "INSERT INTO option_daily (trade_date, symbol_id, exchange, product, "
        "underlying, contract_name, call_put, strike_price, "
        "open, high, low, close, settlement, prev_settlement, "
        "volume, turnover, open_interest, exercise_volume, "
        "delta, gamma, vega, theta, implied_volatility) VALUES ";

    auto D = [](bool ok, double v) -> String {
        return ok ? fmt::format("{:.6f}", v) : "NULL";
    };
    auto I = [](bool ok, int64_t v) -> String {
        return ok ? std::to_string(v) : "NULL";
    };
    auto S = [](const String& s) -> String {
        String escaped;
        escaped.reserve(s.size() + 2);
        for (char c : s) {
            if (c == '\'') escaped += "''";
            else escaped += c;
        }
        return "'" + escaped + "'";
    };

    for (size_t i = 0; i < rows.size(); i++) {
        const auto& r = rows[i];
        if (i > 0) sql += ", ";

        sql += fmt::format("({},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{})",
            S(r.trade_date),
            std::to_string(r.symbol_id),                  // BIGINT 直接传数字
            S(r.exchange), S(r.product),
            r.underlying.empty() ? "NULL" : S(r.underlying),
            S(r.contract_name),
            r.call_put.empty() ? "NULL" : S(r.call_put),
            D(r.has_strike, r.strike_price),
            D(r.has_open, r.open), D(r.has_high, r.high),
            D(r.has_low, r.low), D(r.has_close, r.close),
            D(r.has_settle, r.settlement), D(r.has_prev_set, r.prev_settlement),
            I(r.has_volume, r.volume), D(r.has_turnover, r.turnover),
            I(r.has_oi, r.open_interest), I(r.has_exercise, r.exercise_volume),
            D(r.has_delta, r.delta), D(r.has_gamma, r.gamma),
            D(r.has_vega, r.vega), D(r.has_theta, r.theta),
            D(r.has_iv, r.implied_volatility));
    }

    sql += " ON CONFLICT(symbol_id, trade_date) DO UPDATE SET "
           "exchange=excluded.exchange, product=excluded.product, "
           "underlying=excluded.underlying, contract_name=excluded.contract_name, "
           "call_put=excluded.call_put, strike_price=excluded.strike_price, "
           "open=excluded.open, high=excluded.high, low=excluded.low, close=excluded.close, "
           "settlement=excluded.settlement, prev_settlement=excluded.prev_settlement, "
           "volume=excluded.volume, turnover=excluded.turnover, "
           "open_interest=excluded.open_interest, exercise_volume=excluded.exercise_volume, "
           "delta=excluded.delta, gamma=excluded.gamma, vega=excluded.vega, "
           "theta=excluded.theta, implied_volatility=excluded.implied_volatility";

    // ── 执行 ──
    exec("BEGIN TRANSACTION");
    duckdb_result result;
    duckdb_state state = duckdb_query(conn(), sql.c_str(), &result);
    if (state != DuckDBSuccess) {
        const char* err = duckdb_result_error(&result);
        SPDLOG_ERROR("[OptionDataDB] Batch insert failed ({}): {}", csv_path, err ? err : "unknown");
        duckdb_destroy_result(&result);
        exec("ROLLBACK");
        return -1;
    }
    duckdb_destroy_result(&result);
    exec("COMMIT");

    int count = static_cast<int>(rows.size());
    SPDLOG_INFO("[OptionDataDB] Imported {} rows (exchange={}) from {}", count, exchange, csv_path);
    return count;
}

// ═══════════════════════════════════════════════════════════
//  查询：按合约
// ═══════════════════════════════════════════════════════════

nlohmann::json OptionDataDB::queryByContract(const String& contract_code,
                                             const String& start_date,
                                             const String& end_date,
                                             int limit) {
    nlohmann::json result;
    if (!isInitialized()) {
        result["error"] = "OptionDataDB not initialized";
        return result;
    }

    // contract_code 可能是原始字符串 (e.g. "IO2401-C-3800") 或 "CFFEX:IO2401-C-3800"
    String exchange, code;
    auto colon = contract_code.find(':');
    if (colon != String::npos) {
        exchange = contract_code.substr(0, colon);
        code     = contract_code.substr(colon + 1);
    } else {
        code = contract_code;
    }

    // 编码为 symbol_id
    int64_t symbol_id = encodeContractId(exchange, code);

    String sql =
        "SELECT trade_date, symbol_id, exchange, product, underlying, contract_name, "
        "call_put, strike_price, open, high, low, close, settlement, prev_settlement, "
        "volume, turnover, open_interest, exercise_volume, "
        "delta, gamma, vega, theta, implied_volatility "
        "FROM option_daily WHERE symbol_id = " + std::to_string(symbol_id);

    if (!start_date.empty()) {
        sql += " AND trade_date >= '" + start_date + "'";
    }
    if (!end_date.empty()) {
        sql += " AND trade_date <= '" + end_date + "'";
    }
    sql += " ORDER BY trade_date ASC LIMIT " + std::to_string(limit);

    std::lock_guard<std::recursive_mutex> lock(mtx());
    duckdb_result res;
    if (duckdb_query(conn(), sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        result["error"] = fmt::format("Query failed: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    result["contract_code"] = contract_code;
    result["symbol_id"] = symbol_id;
    result["count"] = static_cast<int>(row_count);

    nlohmann::json::array_t data;
    for (idx_t i = 0; i < row_count; i++) {
        nlohmann::json row;
        row["trade_date"]         = duckdb_value_varchar(&res, 0,  i);
        row["symbol_id"]          = duckdb_value_int64(&res, 1,  i);
        row["exchange"]           = duckdb_value_varchar(&res, 2,  i);
        row["product"]            = duckdb_value_varchar(&res, 3,  i);
        row["underlying"]         = duckdb_value_varchar(&res, 4,  i);
        row["contract_name"]      = duckdb_value_varchar(&res, 5,  i);
        row["call_put"]           = duckdb_value_varchar(&res, 6,  i);
        row["strike_price"]       = duckdb_value_double(&res, 7,  i);
        row["open"]               = duckdb_value_double(&res, 8,  i);
        row["high"]               = duckdb_value_double(&res, 9,  i);
        row["low"]                = duckdb_value_double(&res, 10, i);
        row["close"]              = duckdb_value_double(&res, 11, i);
        row["settlement"]         = duckdb_value_double(&res, 12, i);
        row["prev_settlement"]    = duckdb_value_double(&res, 13, i);
        row["volume"]             = duckdb_value_int64(&res, 14, i);
        row["turnover"]           = duckdb_value_double(&res, 15, i);
        row["open_interest"]      = duckdb_value_int64(&res, 16, i);
        row["exercise_volume"]    = duckdb_value_int64(&res, 17, i);
        row["delta"]              = duckdb_value_double(&res, 18, i);
        row["gamma"]              = duckdb_value_double(&res, 19, i);
        row["vega"]               = duckdb_value_double(&res, 20, i);
        row["theta"]              = duckdb_value_double(&res, 21, i);
        row["implied_volatility"] = duckdb_value_double(&res, 22, i);
        data.push_back(std::move(row));
    }
    result["data"] = std::move(data);
    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  查询：列出合约概要
// ═══════════════════════════════════════════════════════════

nlohmann::json OptionDataDB::listContracts(const String& exchange_filter,
                                           const String& product_filter) {
    nlohmann::json result;
    if (!isInitialized()) {
        result["error"] = "OptionDataDB not initialized";
        return result;
    }

    String sql =
        "SELECT symbol_id, exchange, product, contract_name, call_put, "
        "       strike_price, underlying, MIN(trade_date), MAX(trade_date), COUNT(*) "
        "FROM option_daily WHERE 1=1";

    if (!exchange_filter.empty()) {
        sql += " AND exchange = '" + exchange_filter + "'";
    }
    if (!product_filter.empty()) {
        sql += " AND product = '" + product_filter + "'";
    }
    sql += " GROUP BY symbol_id, exchange, product, contract_name, call_put, "
           "          strike_price, underlying "
           "ORDER BY exchange, product, contract_name";

    std::lock_guard<std::recursive_mutex> lock(mtx());
    duckdb_result res;
    if (duckdb_query(conn(), sql.c_str(), &res) != DuckDBSuccess) {
        const char* err = duckdb_result_error(&res);
        result["error"] = fmt::format("Query failed: {}", err ? err : "unknown");
        duckdb_destroy_result(&res);
        return result;
    }

    idx_t row_count = duckdb_row_count(&res);
    nlohmann::json::array_t contracts;
    for (idx_t i = 0; i < row_count; i++) {
        nlohmann::json row;
        row["symbol_id"]     = duckdb_value_int64(&res, 0, i);
        row["exchange"]      = duckdb_value_varchar(&res, 1, i);
        row["product"]       = duckdb_value_varchar(&res, 2, i);
        row["contract_name"] = duckdb_value_varchar(&res, 3, i);
        row["call_put"]      = duckdb_value_varchar(&res, 4, i);
        row["strike_price"]  = duckdb_value_double(&res, 5, i);
        row["underlying"]    = duckdb_value_varchar(&res, 6, i);
        row["start_date"]    = duckdb_value_varchar(&res, 7, i);
        row["end_date"]      = duckdb_value_varchar(&res, 8, i);
        row["count"]         = duckdb_value_int64(&res, 9, i);
        contracts.push_back(std::move(row));
    }

    result["contracts"] = std::move(contracts);
    result["count"] = static_cast<int>(row_count);
    duckdb_destroy_result(&res);
    return result;
}

// ═══════════════════════════════════════════════════════════
//  删除
// ═══════════════════════════════════════════════════════════

bool OptionDataDB::deleteContract(const String& contract_code) {
    // contract_code 可为数字 symbol_id (如 "1234567890123456789") 或原始字符串 (如 "IO2401-C-3800")
    // 优先尝试解析为 int64_t (symbol_id), 失败则按字符串编码
    int64_t symbol_id = 0;
    bool is_numeric = false;
    try {
        // 检查是否全为数字 (允许负号)
        size_t start = (contract_code[0] == '-') ? 1 : 0;
        bool all_digits = true;
        for (size_t i = start; i < contract_code.size(); ++i) {
            if (contract_code[i] < '0' || contract_code[i] > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits && contract_code.size() > start) {
            symbol_id = std::stoll(contract_code);
            is_numeric = true;
        }
    } catch (...) {
        // not numeric, fall through
    }

    if (!is_numeric) {
        // 视为原始字符串, 假定无 exchange 前缀 (走默认 CFFEX)
        symbol_id = encodeContractId("CFFEX", contract_code);
    }

    String sql = "DELETE FROM option_daily WHERE symbol_id = " + std::to_string(symbol_id);
    std::lock_guard<std::recursive_mutex> lock(mtx());
    bool ok = exec(sql);
    if (ok) {
        SPDLOG_INFO("[OptionDataDB] Deleted symbol_id={} ({})", symbol_id, contract_code);
    } else {
        SPDLOG_ERROR("[OptionDataDB] Failed to delete: {}", contract_code);
    }
    return ok;
}

bool OptionDataDB::deleteAll() {
    std::lock_guard<std::recursive_mutex> lock(mtx());
    bool ok = exec("DELETE FROM option_daily");
    if (ok) {
        SPDLOG_INFO("[OptionDataDB] Cleared all option_daily data");
    }
    return ok;
}
