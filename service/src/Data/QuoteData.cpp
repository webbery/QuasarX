#include "Data/QuoteData.h"
#include "Util/FinanceDB.h"
#include "Util/QuoteDB.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// sh.510050 → 510050.SH
String toCsvName(const String& bsSymbol) {
    auto dot = bsSymbol.find('.');
    if (dot == String::npos || dot > 3) return bsSymbol;
    String market = bsSymbol.substr(0, dot);
    String code = bsSymbol.substr(dot + 1);
    std::transform(market.begin(), market.end(), market.begin(), ::toupper);
    return code + "." + market;
}

// "2026-11-18 00:00:00" → "2026-11-18"
String dateOnly(const String& dt) {
    auto pos = dt.find(' ');
    return (pos == String::npos) ? dt : dt.substr(0, pos);
}

}  // namespace

// ═══════════════════════════════════════════════════════════
//  QuoteData
// ═══════════════════════════════════════════════════════════

QuoteData::QuoteData(const String& interpreter, const String& freq)
    : _interpreter(interpreter), _freq(freq) {}

Vector<String> QuoteData::ExpectedColumns() {
    // 顺序即语义：QuoteDB::importCsv 按位置取列
    return {"datetime", "open", "close", "high", "low", "volume", "turnover"};
}

DataCategory QuoteData::CategoryOf(const String& freq) {
    if (freq == "5m")  return DataCategory::Min5;
    if (freq == "15m") return DataCategory::Min15;
    if (freq == "30m") return DataCategory::Min30;
    if (freq == "60m") return DataCategory::Min60;
    return DataCategory::Day;
}

bool QuoteData::downloadOne(symbol_t symbol, const String& start) {
    const String sym = get_symbol(symbol);
    bool etf = is_etf(symbol);
    const String assetType = etf ? "etf" : "stock";

    String args = sym + " " + ShellQuote(_dst) +
                  " --freq " + _freq +
                  " --asset-type " + assetType;
    if (!start.empty())   args += " --start " + start;
    if (!_endDate.empty()) args += " --end " + _endDate;

    String output;
    bool ok = RunScript(_interpreter, "download_quote_bs.py", args, output);
    if (!ok) {
        WARN("[Quote] 下载失败 {} {}: {}", sym, _freq, output.substr(0, 200));
        return false;
    }

    String csvName = toCsvName(sym);
    String orgPath = _dst + "/" + assetType + "_org/" + _freq + "/" + csvName + ".csv";
    String hfqPath = _dst + "/" + assetType + "_hfq/" + _freq + "/" + csvName + ".csv";

    std::error_code ec;
    bool hasOrg = std::filesystem::exists(orgPath, ec);
    bool hasHfq = std::filesystem::exists(hfqPath, ec);

    if (!hasOrg) {
        // 不复权(org)是原始价的唯一来源，缺一不可。
        // org 下载失败时不能把「后复权价」写进 close 列 —— 宁可这根 bar 不导入，
        // 也不能用后复权价冒充原始价。残留的 hfq 还要清掉，免得下次被误当成 org。
        WARN("[Quote] {} {} 缺 org 文件，跳过导入以免用后复权价污染原始价", sym, _freq);
        if (hasHfq) {
            std::filesystem::remove(hfqPath, ec);
        }
        return false;
    }

    SymbolFiles files;
    files._org = orgPath;
    files._hfq = hasHfq ? hfqPath : "";
    files._from = start;
    _downloaded[symbol] = files;
    return true;
}

int QuoteData::Download(const String& dst) {
    _dst = dst;
    _downloaded.clear();
    _bad.clear();

    if (_symbols.empty()) {
        WARN("[Quote] 没有要下载的标的（{}）", _freq);
        return 0;
    }

    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        if (!quoteDB.init(_dst, "quote.db")) {
            WARN("[Quote] QuoteDB 初始化失败: {}", _dst);
            return -1;
        }
    }

    // 先按资产类型分组，每组只取一次时间范围（getSymbolTimeRanges 是全表扫描，
    // 所以只扫本次真的用得到的表 —— 没有 ETF 就不去扫 etf_1d）
    Map<String, Map<symbol_t, String>> lastDates;   // table → (symbol → 库里最新日期)
    Map<String, String> tableOfAsset = {
        {"stock", QuoteDB::tableName("stock", _freq)},
        {"etf", QuoteDB::tableName("etf", _freq)},
    };
    Set<String> neededTables;
    for (const auto& symbol : _symbols) {
        neededTables.insert(tableOfAsset[is_etf(symbol) ? "etf" : "stock"]);
    }
    for (const auto& table : neededTables) {
        for (const auto& r : quoteDB.getSymbolTimeRanges(table)) {
            lastDates[table][r.symbol] = dateOnly(r.end_time);
        }
    }

    int downloaded = 0;
    for (const auto& symbol : _symbols) {
        String asset = is_etf(symbol) ? "etf" : "stock";
        const String& table = tableOfAsset[asset];

        // 已有最新日期 → 从那天起重下（含当天，靠 upsert 幂等）；
        // 表里没有 → 走全量；overwrite → 无视最新日期全量重下。
        String start;
        if (!_overwrite) {
            auto it = lastDates[table].find(symbol);
            if (it != lastDates[table].end()) {
                start = it->second;
            }
        }

        if (downloadOne(symbol, start)) {
            downloaded++;
        } else {
            AddIssue(CategoryOf(_freq), symbol, "", DataReason::Missing);
        }
    }

    INFO("[Quote] 下载完成 {}: {} 个标的", _freq, downloaded);
    return downloaded;
}

bool QuoteData::checkCsv(symbol_t symbol, const SymbolFiles& files) {
    const String sym = get_symbol(symbol);
    const DataCategory cat = CategoryOf(_freq);

    // ── org：表头必须逐列等于期望列 ──
    Vector<String> cols;
    if (!ReadCsvHeader(files._org, cols)) {
        WARN("[Quote] org CSV 读不出来: {}", files._org);
        AddIssue(cat, symbol, "", DataReason::Missing);
        return false;
    }
    Vector<String> expected = ExpectedColumns();
    bool headerOk = (cols.size() >= expected.size());
    if (headerOk) {
        for (size_t i = 0; i < expected.size(); i++) {
            if (cols[i] != expected[i]) {
                headerOk = false;
                break;
            }
        }
    }
    if (!headerOk) {
        String got;
        for (size_t i = 0; i < cols.size(); i++) {
            if (i > 0) got += ",";
            got += cols[i];
        }
        WARN("[Quote] {} 表头不对，期望 {} 实际 {}", sym,
             "datetime,open,close,high,low,volume,turnover", got);
        AddIssue(cat, symbol, "", DataReason::Mismatch);
        return false;
    }

    // ── 逐行检查 org ──
    auto scan = [&](const String& path, Set<String>& dates, bool checkOhlc) -> bool {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        String line;
        std::getline(ifs, line);
        String prev;
        int rows = 0, badOhlc = 0, badOrder = 0;
        String firstBad;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            if (line.back() == '\r') line.pop_back();
            Vector<String> row = SplitCsvLine(line);
            if (row.size() < 7) continue;

            const String& dt = row[0];
            if (!prev.empty() && dt <= prev) {
                badOrder++;
                if (firstBad.empty()) firstBad = fmt::format("时间乱序/重复 {} → {}", prev, dt);
            }
            prev = dt;
            dates.insert(dt);
            rows++;

            if (checkOhlc) {
                // BaoStock 的停牌日：volume 为 0，价格字段可能是 0 或空。
                // 停牌 bar 没有可比价格，直接跳过 OHLC 自洽检查 —— 否则
                // 窗口里只要有一天停牌，整个标的都会被判成 invalid 而不入库。
                // 判定口径与 QuoteDB::importCsv 写 ext 的口径一致：
                // volume == 0 或四价全 0/全空。
                double open = 0, close = 0, high = 0, low = 0;
                double volume = 0, turnover = 0;
                auto num = [](const String& s, double& out) -> bool {
                    if (s.empty()) return false;
                    try {
                        out = std::stod(s);
                        return std::isfinite(out);
                    } catch (...) {
                        return false;
                    }
                };
                const bool okOpen  = num(row[1], open);
                const bool okClose = num(row[2], close);
                const bool okHigh  = num(row[3], high);
                const bool okLow   = num(row[4], low);
                const bool okVolume = num(row[5], volume);
                const bool okTurnover = num(row[6], turnover);

                const bool pricesZero = okOpen && okClose && okHigh && okLow &&
                                        open == 0 && close == 0 && high == 0 && low == 0;
                const bool pricesEmpty = !okOpen && !okClose && !okHigh && !okLow;
                // volume 有值且非 0，就说明当天有成交；这时价格缺失是真异常，
                // 不能当成停牌放过去。
                const bool suspended = (okVolume && volume == 0) ||
                                       pricesZero ||
                                       (pricesEmpty && !okVolume);
                if (!suspended) {
                    if (!okOpen || !okClose || !okHigh || !okLow) {
                        badOhlc++;
                        if (firstBad.empty()) firstBad = "数值解析失败";
                    } else {
                        bool bad = !(high >= open && high >= close && low <= open &&
                                     low <= close && high >= low);
                        if (bad) {
                            badOhlc++;
                            if (firstBad.empty()) {
                                firstBad = fmt::format("OHLC 不自洽 o={} c={} h={} l={}",
                                                       open, close, high, low);
                            }
                        }
                    }
                    if (!okVolume || !okTurnover || volume < 0 || turnover < 0) {
                        badOhlc++;
                        if (firstBad.empty()) firstBad = "volume/turnover 非法";
                    }
                }
            }
        }
        if (rows == 0) {
            AddIssue(cat, symbol, "", DataReason::Missing);
            WARN("[Quote] {} 没有数据行: {}", sym, path);
            return false;
        }
        if (badOhlc > 0) {
            WARN("[Quote] {} 有 {} 行 OHLC 不合法（{}）", sym, badOhlc, firstBad);
            AddIssue(cat, symbol, "", DataReason::Invalid);
            return false;
        }
        if (badOrder > 0) {
            WARN("[Quote] {} 有 {} 行时间乱序或重复（{}）", sym, badOrder, firstBad);
            AddIssue(cat, symbol, "", DataReason::Invalid);
            return false;
        }
        return true;
    };

    Set<String> orgDates;
    if (!scan(files._org, orgDates, true)) {
        return false;
    }

    // ── hfq：日期集合必须与 org 一致，否则 adj_* 会错位 ──
    if (!files._hfq.empty()) {
        Set<String> hfqDates;
        if (!scan(files._hfq, hfqDates, false)) {
            return false;
        }
        if (hfqDates != orgDates) {
            size_t onlyOrg = 0, onlyHfq = 0;
            for (const auto& d : orgDates) {
                if (hfqDates.count(d) == 0) onlyOrg++;
            }
            for (const auto& d : hfqDates) {
                if (orgDates.count(d) == 0) onlyHfq++;
            }
            WARN("[Quote] {} org/hfq 日期不一致: 仅org={} 仅hfq={}", sym, onlyOrg, onlyHfq);
            AddIssue(cat, symbol, "", DataReason::Mismatch);
            return false;
        }
    }

    return true;
}

bool QuoteData::Check() {
    bool allOk = true;
    for (const auto& [symbol, files] : _downloaded) {
        if (!checkCsv(symbol, files)) {
            _bad.insert(symbol);
            allOk = false;
        }
    }
    INFO("[Quote] {} CSV 级体检完成: {} 个文件, {} 个不合格",
         _freq, _downloaded.size(), _bad.size());
    return allOk;
}

int QuoteData::ImportData() {
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) {
        WARN("[Quote] QuoteDB 未初始化，跳过导入");
        return -1;
    }

    int total = 0;
    for (const auto& [symbol, files] : _downloaded) {
        const String sym = get_symbol(symbol);
        if (_bad.count(symbol) > 0) {
            WARN("[Quote] {} {} 体检未过，跳过导入", sym, _freq);
            continue;
        }

        String asset = is_etf(symbol) ? "etf" : "stock";
        String table = QuoteDB::tableName(asset, _freq);

        // hfq 缺失时退化为 org：此时 adj_* 暂时等于原始价，
        // 随后的复权比对会按 dividend 表重算复权列。
        String org = files._org;
        String hfq = files._hfq.empty() ? files._org : files._hfq;

        int n = quoteDB.importCsv(org, hfq, table, sym, _overwrite);
        if (n > 0) {
            total += n;
            // 导入成功才删中间 CSV：失败时留着还能重试
            std::error_code ec;
            std::filesystem::remove(files._org, ec);
            if (!files._hfq.empty()) {
                std::filesystem::remove(files._hfq, ec);
            }
        } else {
            AddIssue(CategoryOf(_freq), symbol, "", DataReason::Missing);
        }
    }

    checkDb();
    INFO("[Quote] {} 导入完成: {} 行", _freq, total);
    return total;
}

int QuoteData::reconcileAdj(symbol_t symbol, const String& table, const String& from) {
    auto& quoteDB = QuoteDB::instance();
    auto& financeDB = FinanceDB::instance();
    if (!quoteDB.isInitialized() || !financeDB.isInitialized()) {
        return 0;
    }

    constexpr double REL_TOLERANCE = 0.01;  // 1% 相对差异阈值
    const String sym = get_symbol(symbol);

    // 1. 先把下载进来的复权价整段读出来（重算会覆盖它，读完再比）
    //    from 为空 = 全区间；增量时只比本次 import 重写过的区间
    auto downloaded = quoteDB.query(table, sym, from, "", 1 << 30);
    if (downloaded.empty()) return 0;

    // 2. 按 dividend 表重算（baostock 公式），写回库
    auto events = financeDB.getDividendEvents(sym);
    financeDB.recalcSymbolAdjPrices(sym, events, from, "");

    // 3. 读回来逐 bar 比对
    auto recalc = quoteDB.query(table, sym, from, "", 1 << 30);
    if (recalc.size() != downloaded.size()) {
        WARN("[Quote] {} 重算前后 bar 数不一致: {} vs {}",
             sym, downloaded.size(), recalc.size());
        return 0;
    }

    const size_t n = downloaded.size();
    Vector<QuoteDB::AdjPriceUpdate> restore;
    int mismatch = 0;
    for (size_t i = 0; i < n; i++) {
        const auto& d = downloaded[i];
        const auto& r = recalc[i];
        // 停牌 bar 不参与比对：那天没有成交，价格字段可能是 0/空，
        // 与分红重算值没有可比性（重算侧也已跳过这些 bar）。
        if ((d.ext & 0x01) != 0) continue;
        bool dValid = d.adj_close > 0 && std::isfinite(d.adj_close);
        bool rValid = r.adj_close > 0 && std::isfinite(r.adj_close);

        if (dValid && rValid) {
            double maxVal = std::max(d.adj_close, r.adj_close);
            double relDiff = std::abs(d.adj_close - r.adj_close) / maxVal;
            if (relDiff <= REL_TOLERANCE) continue;
            mismatch++;
            QuoteDB::AdjPriceUpdate u;
            u.datetime = d.datetime;
            u.adj_open = d.adj_open;
            u.adj_high = d.adj_high;
            u.adj_low = d.adj_low;
            u.adj_close = d.adj_close;
            restore.push_back(u);
        } else if (dValid && !rValid) {
            // 重算失败但下载值有效 → 信任下载值
            mismatch++;
            QuoteDB::AdjPriceUpdate u;
            u.datetime = d.datetime;
            u.adj_open = d.adj_open;
            u.adj_high = d.adj_high;
            u.adj_low = d.adj_low;
            u.adj_close = d.adj_close;
            restore.push_back(u);
        }
    }

    if (!restore.empty()) {
        WARN("[Quote] {} 复权价与分红重算有 {} 根 bar 不一致（比对区间 {} ~ 今），恢复成下载值",
             sym, mismatch, from.empty() ? "全区间" : from);
        quoteDB.updateAdjPrices(table, QuoteDB::encodeSymbol(sym), restore);
    }

    return mismatch;
}

void QuoteData::checkDb() {
    auto& quoteDB = QuoteDB::instance();
    if (!quoteDB.isInitialized()) return;

    const DataCategory cat = CategoryOf(_freq);

    Set<String> tables;
    for (const auto& symbol : _downloaded) {
        tables.insert(QuoteDB::tableName(is_etf(symbol.first) ? "etf" : "stock", _freq));
    }

    for (const auto& table : tables) {
        int64_t invalid = quoteDB.countInvalidBars(table);
        if (invalid > 0) {
            WARN("[Quote] {} 有 {} 行 OHLC 不自洽", table, invalid);
            AddIssue(cat, null_symbol(), "", DataReason::Invalid);
        }
    }

    // 复权一致性：只对日线做（分红事件是日频的，分钟线没有独立的复权因子）
    //
    // 比对区间只覆盖本次 import 真正重写过的部分：
    //   - 首次导入 / 全量下载 / overwrite → _from 为空，全区间比对
    //   - 增量下载 → 从库里原有最新日期开始（该 bar 会被 upsert 重写，含当天）
    // 更早的历史在当初导入的那一轮已经比对过，每次刷新都重跑纯属浪费：
    // 每标的要多两次全历史读 + 一次全历史复权重算。
    if (_freq != "daily" && _freq != "1d") return;
    size_t nFull = 0, nIncr = 0;
    for (const auto& [symbol, files] : _downloaded) {
        if (_bad.count(symbol) > 0) continue;
        if (is_etf(symbol)) continue;   // ETF 不走分红复权
        String table = QuoteDB::tableName("stock", _freq);
        (files._from.empty() ? nFull : nIncr)++;
        int mismatch = reconcileAdj(symbol, table, files._from);
        if (mismatch > 0) {
            AddIssue(cat, symbol, "", DataReason::Mismatch);
        }
    }
    INFO("[Quote] {} 复权比对完成: {} 个标的（全量 {}，增量 {}）",
         _freq, nFull + nIncr, nFull, nIncr);
}

int QuoteData::UpdateData() {
    Vector<DataIssue> pending = TakeMyIssues(Type());
    if (pending.empty()) {
        return 0;
    }

    // 只补本频率的；别的频率的问题原样放回队列（同一个 Quote 类型下按频率分工）
    const DataCategory cat = CategoryOf(_freq);
    Set<symbol_t> targets;
    Vector<DataIssue> notMine;
    for (const auto& issue : pending) {
        if (issue._category != cat) {
            notMine.push_back(issue);
            continue;
        }
        if (is_null(issue._symbol)) {
            notMine.push_back(issue);
            continue;
        }
        targets.insert(issue._symbol);
    }

    size_t before = _issues.size();
    int fixed = 0;

    for (const auto& symbol : targets) {
        const String sym = get_symbol(symbol);
        INFO("[Quote] 补下载 {} {}", sym, _freq);

        // 补数据走全量：出问题的可能是历史某一段，增量补不到
        if (!downloadOne(symbol, "")) {
            AddIssue(cat, symbol, "", DataReason::Missing);
            continue;
        }
        auto it = _downloaded.find(symbol);
        if (it == _downloaded.end()) continue;

        if (!checkCsv(symbol, it->second)) {
            AddIssue(cat, symbol, "", DataReason::Invalid);
            std::error_code ec;
            std::filesystem::remove(it->second._org, ec);
            if (!it->second._hfq.empty()) {
                std::filesystem::remove(it->second._hfq, ec);
            }
            continue;
        }

        bool etf = is_etf(symbol);
        String table = QuoteDB::tableName(etf ? "etf" : "stock", _freq);
        String org = it->second._org;
        String hfq = it->second._hfq.empty() ? it->second._org : it->second._hfq;
        int n = QuoteDB::instance().importCsv(org, hfq, table, sym, false);
        if (n > 0) {
            fixed++;
            // 补数据走的是全量重下，整段历史刚被重写；这里必须全区间复查一遍，
            // 否则下一轮增量比对会认为这段历史「已经检查过」而跳过它。
            if ((_freq == "daily" || _freq == "1d") && !etf) {
                int mismatch = reconcileAdj(symbol, table, "");
                if (mismatch > 0) {
                    AddIssue(cat, symbol, "", DataReason::Mismatch);
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(it->second._org, ec);
        if (!it->second._hfq.empty()) {
            std::filesystem::remove(it->second._hfq, ec);
        }
    }

    // 补完仍然没解决的重新入队。修好的已经不在队列里了。
    Vector<DataIssue> stillBad;
    for (size_t i = before; i < _issues.size(); i++) {
        stillBad.push_back(_issues[i]);
    }
    RecordIssues(notMine);
    RecordIssues(stillBad);
    return fixed;
}
