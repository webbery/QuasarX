#include "Data/FundamentalData.h"
#include "Util/FinanceDB.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// statDate 是不是季末（03-31 / 06-30 / 09-30 / 12-31）
bool isQuarterEnd(const String& date) {
    if (date.size() < 10) return false;
    String md = date.substr(5, 5);
    return md == "03-31" || md == "06-30" || md == "09-30" || md == "12-31";
}

// 报告期对应的法定披露截止日：Q1→4/30, Q2→8/31, Q3→10/31, Q4→次年 4/30
String quarterDeadline(const String& statDate) {
    if (statDate.size() < 10) return "";
    // 日期来自下载到的 CSV，可能被改坏；std::stoi 会抛，这里先挡掉
    if (!std::all_of(statDate.begin(), statDate.begin() + 4,
                     [](char c) { return c >= '0' && c <= '9'; })) {
        return "";
    }
    int year = std::stoi(statDate.substr(0, 4));
    String md = statDate.substr(5, 5);
    if (md == "03-31") return fmt::format("{}-04-30", year);
    if (md == "06-30") return fmt::format("{}-08-31", year);
    if (md == "09-30") return fmt::format("{}-10-31", year);
    if (md == "12-31") return fmt::format("{}-04-30", year + 1);
    return "";
}

}  // namespace

// ═══════════════════════════════════════════════════════════
//  FundamentalData
// ═══════════════════════════════════════════════════════════

FundamentalData::FundamentalData(const String& financeDir, const String& interpreter)
    : _financeDir(financeDir), _interpreter(interpreter) {
    _categories = AllCategories();
}

Vector<DataCategory> FundamentalData::AllCategories() {
    return {DataCategory::Profit,    DataCategory::Operation, DataCategory::Growth,
            DataCategory::Balance,   DataCategory::Cashflow,  DataCategory::Dupont};
}

const char* FundamentalData::CategoryKey(DataCategory category) {
    switch (category) {
        case DataCategory::Profit:    return "profit";
        case DataCategory::Operation: return "operation";
        case DataCategory::Growth:    return "growth";
        case DataCategory::Balance:   return "balance";
        case DataCategory::Cashflow:  return "cashflow";
        case DataCategory::Dupont:    return "dupont";
        default:                      return "";
    }
}

bool FundamentalData::CategoryFromKey(const String& key, DataCategory& out) {
    for (auto c : AllCategories()) {
        if (key == CategoryKey(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

bool FundamentalData::downloadOne(symbol_t symbol, DataCategory category,
                                  String& outCsv) {
    const String sym = get_symbol(symbol);
    const String cat = CategoryKey(category);
    if (cat.empty()) return false;

    // CSV 目录用绝对路径：脚本被 cd 到 ../tools 执行，相对路径会跑偏
    String csvDir = std::filesystem::absolute(_dst + "/csv").string();
    std::error_code ec;
    std::filesystem::create_directories(csvDir, ec);

    String args = "--code " + ShellQuote(sym) + " --category " + cat +
                  " --download --data-dir " + ShellQuote(csvDir);
    if (!_startYq.empty()) args += " --start " + _startYq;
    if (!_endYq.empty())   args += " --end " + _endYq;

    String output;
    bool ok = RunScript(_interpreter, "fetch_finance_data.py", args, output);
    if (!ok) {
        WARN("[Fundamental] 下载失败 {} {}: {}", sym, cat, output.substr(0, 200));
        return false;
    }

    nlohmann::json manifest;
    if (!ParseManifest(output, manifest)) {
        WARN("[Fundamental] manifest 解析失败 {} {}", sym, cat);
        return false;
    }
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        WARN("[Fundamental] manifest 里没有 files {} {}", sym, cat);
        return false;
    }

    // manifest 里 files[] 的元素是 {symbol, category, path, rows}
    for (const auto& f : manifest["files"]) {
        if (!f.is_object()) continue;
        if (f.value("category", "") != cat) continue;
        String path = f.value("path", "");
        if (path.empty()) continue;
        std::error_code pec;
        if (!std::filesystem::exists(path, pec)) continue;
        outCsv = path;
        return true;
    }

    // 老版本脚本直接给字符串数组
    for (const auto& f : manifest["files"]) {
        if (!f.is_string()) continue;
        String path = f.get<String>();
        if (path.find(cat) == String::npos) continue;
        std::error_code pec;
        if (!std::filesystem::exists(path, pec)) continue;
        outCsv = path;
        return true;
    }

    WARN("[Fundamental] manifest 里没有 {} 的产物 {}", sym, cat);
    return false;
}

bool FundamentalData::checkCsv(symbol_t symbol, DataCategory category,
                               const String& path) {
    const String cat = CategoryKey(category);
    Vector<String> cols;
    if (!ReadCsvHeader(path, cols)) {
        AddIssue(category, symbol, "", DataReason::Missing);
        WARN("[Fundamental] CSV 读不出来: {}", path);
        return false;
    }

    bool ok = true;

    // ── 表头必须覆盖该类目声明的每一个列 ──
    // 列名对不上 → 那一列会全是 NULL，而表看起来是正常的。
    // 这正是以前 operation/balance/cashflow/dupont 12 个列永久 NULL 的原因。
    Vector<String> expected = {"code", "pubDate", "statDate"};
    for (const auto& c : FinanceDB::categoryCsvColumns(cat)) {
        expected.push_back(c);
    }
    Vector<String> missing = MissingColumns(cols, expected);
    if (!missing.empty()) {
        String m;
        for (size_t i = 0; i < missing.size(); i++) {
            if (i > 0) m += ", ";
            m += missing[i];
        }
        WARN("[Fundamental] {} 的 CSV 缺少期望列 [{}]: {}",
             cat, m, path);
        AddIssue(category, symbol, "", DataReason::Mismatch);
        ok = false;
    }

    int iCode = ColIndex(cols, "code");
    int iPub = ColIndex(cols, "pubDate");
    int iStat = ColIndex(cols, "statDate");
    if (iCode < 0 || iPub < 0 || iStat < 0) {
        if (ok) {
            AddIssue(category, symbol, "", DataReason::Invalid);
        }
        WARN("[Fundamental] {} 缺少 code/pubDate/statDate 列: {}", cat, path);
        return false;
    }

    // ── 逐行检查 ──
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        AddIssue(category, symbol, "", DataReason::Missing);
        return false;
    }
    String line;
    std::getline(ifs, line);  // 跳过表头

    String today = ToString(Now(), "%Y-%m-%d");
    Set<String> seenDates;
    int rows = 0;
    int badPub = 0;
    int badQuarter = 0;
    int future = 0;
    int dup = 0;
    String latestStat;
    String firstBad;

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        Vector<String> row = SplitCsvLine(line);
        if (static_cast<int>(row.size()) <= iStat) continue;

        String stat = row[iStat];
        String pub = (static_cast<int>(row.size()) > iPub) ? row[iPub] : "";
        rows++;

        if (!seenDates.insert(stat).second) {
            dup++;
            if (firstBad.empty()) firstBad = fmt::format("报告期重复 {}", stat);
        }
        if (!isQuarterEnd(stat)) {
            badQuarter++;
            if (firstBad.empty()) firstBad = fmt::format("报告期不是季末 {}", stat);
        }
        if (stat > today) {
            future++;
            if (firstBad.empty()) firstBad = fmt::format("报告期在未来 {}", stat);
        }
        if (pub.empty() || pub < stat) {
            badPub++;
            if (firstBad.empty()) {
                firstBad = fmt::format("公告日非法 pub={} stat={}", pub, stat);
            }
        } else {
            String deadline = quarterDeadline(stat);
            if (!deadline.empty() && pub > deadline) {
                badPub++;
                if (firstBad.empty()) {
                    firstBad = fmt::format("公告日超出披露期限 pub={} 期限={}", pub, deadline);
                }
            }
        }
        if (stat > latestStat) latestStat = stat;
    }

    if (rows == 0) {
        AddIssue(category, symbol, "", DataReason::Missing);
        return false;
    }
    if (rows < _minQuarters) {
        WARN("[Fundamental] {} {} 只有 {} 个季度，至少要 {}",
             cat, get_symbol(symbol), rows, _minQuarters);
        AddIssue(category, symbol, latestStat, DataReason::Insufficient);
        ok = false;
    }
    if (dup > 0 || badPub > 0 || badQuarter > 0 || future > 0) {
        WARN("[Fundamental] {} {} 数据不合法: dup={} badPub={} badQuarter={} future={} ({})",
             cat, get_symbol(symbol), dup, badPub, badQuarter, future, firstBad);
        AddIssue(category, symbol, latestStat, DataReason::Invalid);
        ok = false;
    }
    return ok;
}

int FundamentalData::Download(const String& dst) {
    _dst = dst;
    _downloaded.clear();
    _bad.clear();

    if (_symbols.empty()) {
        WARN("[Fundamental] 没有要下载的标的");
        return 0;
    }

    // ★ FinanceDB 必须先打开 ★
    // 以前这条路径直接调 financeDB.importCsv，而 importCsv 没有
    // isInitialized() 守卫 → 空连接 → 整个基本面刷新静默全灭。
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        if (!financeDB.init(_financeDir, "finance.db")) {
            WARN("[Fundamental] FinanceDB 初始化失败: {}", _financeDir);
            return -1;
        }
    }

    int downloaded = 0;
    for (const auto& category : _categories) {
        for (const auto& symbol : _symbols) {
            String csv;
            if (!downloadOne(symbol, category, csv)) {
                AddIssue(category, symbol, "", DataReason::Missing);
                continue;
            }
            _downloaded[category][symbol] = csv;
            downloaded++;
        }
    }

    INFO("[Fundamental] 下载完成: {} 个 CSV（{} 类目 × {} 标的）",
         downloaded, _categories.size(), _symbols.size());
    return downloaded;
}

bool FundamentalData::Check() {
    bool allOk = true;
    for (const auto& [category, bySymbol] : _downloaded) {
        for (const auto& [symbol, path] : bySymbol) {
            if (!checkCsv(symbol, category, path)) {
                _bad.insert({category, symbol});
                allOk = false;
            }
        }
    }
    INFO("[Fundamental] CSV 级体检完成: {} 个文件, {} 个不合格",
         _downloaded.size(), _bad.size());
    return allOk;
}

int FundamentalData::ImportData() {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        WARN("[Fundamental] FinanceDB 未初始化，跳过导入");
        return -1;
    }

    int total = 0;
    for (const auto& [category, bySymbol] : _downloaded) {
        for (const auto& [symbol, path] : bySymbol) {
            if (_bad.count({category, symbol}) > 0) {
                WARN("[Fundamental] {} {} 体检未过，跳过导入",
                     CategoryKey(category), get_symbol(symbol));
                continue;
            }
            int n = financeDB.importCsv(path, CategoryKey(category));
            if (n > 0) {
                total += n;
                // 导入成功后删掉中间 CSV：留着只是垃圾（两融那边就攒了 1955 个）
                std::error_code ec;
                std::filesystem::remove(path, ec);
            } else {
                AddIssue(category, symbol, "", DataReason::Missing);
            }
        }
    }

    checkDb();
    INFO("[Fundamental] 导入完成: {} 行", total);
    AddRows(total);
    return total;
}

void FundamentalData::checkDb() {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) return;

    for (const auto& category : _categories) {
        const String cat = CategoryKey(category);
        nlohmann::json h = financeDB.queryCategoryHealth(cat);
        if (h.contains("error")) {
            WARN("[Fundamental] 类目表不可用 {}: {}", cat, h["error"].get<String>());
            AddIssue(category, null_symbol(), "", DataReason::Missing);
            continue;
        }
        int64_t rows = h.value("rows", 0);
        if (rows == 0) {
            AddIssue(category, null_symbol(), "", DataReason::Missing);
            continue;
        }
        String latest = h.value("latest_date", "");
        if (h.value("pub_date_before_stat", 0) > 0 ||
            h.value("pub_date_missing", 0) > 0) {
            WARN("[Fundamental] {} 公告日异常: before_stat={} missing={}",
                 cat, h.value("pub_date_before_stat", 0), h.value("pub_date_missing", 0));
            AddIssue(category, null_symbol(), latest, DataReason::Invalid);
        }
        if (h.value("stat_date_not_quarter_end", 0) > 0) {
            WARN("[Fundamental] {} 有 {} 行报告期不是季末",
                 cat, h.value("stat_date_not_quarter_end", 0));
            AddIssue(category, null_symbol(), latest, DataReason::Invalid);
        }
        // 整列 NULL = 列名对不上（以前 cashflow 那 7 列就是这样永久 NULL 的）
        if (h.contains("null_counts") && h["null_counts"].is_object()) {
            for (auto it = h["null_counts"].begin(); it != h["null_counts"].end(); ++it) {
                if (it.value().get<int64_t>() == rows) {
                    WARN("[Fundamental] {} 的列 {} 整列为 NULL（列名与数据源对不上）",
                         cat, it.key());
                    AddIssue(category, null_symbol(), latest, DataReason::Mismatch);
                }
            }
        }
    }
}

int FundamentalData::refreshOne(symbol_t symbol, DataCategory category) {
    String csv;
    if (!downloadOne(symbol, category, csv)) {
        AddIssue(category, symbol, "", DataReason::Missing);
        return 0;
    }
    if (!checkCsv(symbol, category, csv)) {
        AddIssue(category, symbol, "", DataReason::Invalid);
        return 0;
    }
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        return 0;
    }
    int n = financeDB.importCsv(csv, CategoryKey(category));
    if (n > 0) {
        AddRows(n);
        std::error_code ec;
        std::filesystem::remove(csv, ec);
        return 1;   // 修好了一个 (标的, 类目)
    }
    return 0;
}

int FundamentalData::UpdateData() {
    Vector<DataIssue> pending = TakeMyIssues(Type());
    if (pending.empty()) {
        return 0;
    }

    // 按 (标的, 类目) 去重
    Set<std::pair<DataCategory, symbol_t>> targets;
    Vector<DataIssue> unattempted;
    for (const auto& issue : pending) {
        // 全市场级的问题没法按标的补；不在本次类目范围内的也不该由这一轮负责
        bool inScope = false;
        for (auto c : _categories) {
            if (c == issue._category) { inScope = true; break; }
        }
        if (is_null(issue._symbol) || !inScope) {
            unattempted.push_back(issue);
            continue;
        }
        targets.insert({issue._category, issue._symbol});
    }

    // 重试期间新出的问题追加在 _issues 尾部
    size_t before = _issues.size();

    int fixed = 0;
    for (const auto& target : targets) {
        INFO("[Fundamental] 补下载 {} {}", get_symbol(target.second),
             CategoryKey(target.first));
        fixed += refreshOne(target.second, target.first);
    }

    // 没修好的重新入队，等下一轮 / 凌晨再试。
    // 修好的那些已经从队列里摘掉了（TakeMyIssues 把它们取走了），不再入队。
    Vector<DataIssue> stillBad;
    for (size_t i = before; i < _issues.size(); i++) {
        stillBad.push_back(_issues[i]);
    }
    RecordIssues(unattempted);
    RecordIssues(stillBad);
    return fixed;
}
