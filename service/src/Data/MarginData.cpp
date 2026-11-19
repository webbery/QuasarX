#include "Data/MarginData.h"
#include "Util/FinanceDB.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <filesystem>
#include <fstream>

namespace {

// 两融明细的期望列（与 fetch_margin_data.py 的 CSV_HEADER 一一对应）
// FinanceDB::categoryCsvColumns("margin_daily") 给的是同一份清单，
// 但这里显式列出来，体检时能立刻看出是哪一列丢了。
const char* kMarginFields[] = {
    "rzBalance", "rzBuy", "rzRepay", "rqVolume",
    "rqSell", "rqRepay", "rqBalance", "totalBalance",
};

}  // namespace

// ═══════════════════════════════════════════════════════════
//  MarginData
// ═══════════════════════════════════════════════════════════

MarginData::MarginData(const String& financeDir, const String& interpreter)
    : _financeDir(financeDir), _interpreter(interpreter) {
    _exchanges = {"sh", "sz"};
}

String MarginData::exchangeOf(const String& path) {
    // detail_20260113.sh.csv → sh
    std::filesystem::path p(path);
    String name = p.filename().string();
    auto first = name.find('.');
    if (first == String::npos) return "";
    auto second = name.find('.', first + 1);
    if (second == String::npos) return "";
    return name.substr(first + 1, second - first - 1);
}

int MarginData::Download(const String& dst) {
    _dst = dst;
    _downloaded.clear();
    _bad.clear();

    String marginDir = std::filesystem::absolute(dst).string();
    std::error_code ec;
    std::filesystem::create_directories(marginDir, ec);

    String args = "--out-dir " + ShellQuote(marginDir);
    if (!_targetDay.empty()) {
        args += " --date " + _targetDay;
    }

    String output;
    bool runOk = RunScript(_interpreter, "fetch_margin_data.py", args, output);

    nlohmann::json manifest;
    bool parsed = ParseManifest(output, manifest);

    // 老版本脚本在早退分支上只给 "ok"，新版本给 "fetch_ok"
    bool fetchOk = manifest.value("fetch_ok", manifest.value("ok", false));
    int rows = manifest.value("rows", 0);

    if (!parsed || rows <= 0) {
        WARN("[Margin] 抓取失败（runOk={} parsed={} rows={}）：{}",
             runOk, parsed, rows,
             manifest.value("error", output.substr(0, 200)));
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        return -1;
    }

    // 部分数据源失败：CSV 里只有成功那部分。以前只打一行 WARN 就照常入库，
    // 结果「只有上交所」的残缺日被当成完整日。现在记成异常，让补数据去补。
    if (!fetchOk) {
        WARN("[Margin] 部分交易所失败，本次只入库 {} 行：{}",
             rows, manifest.value("error", ""));
        AddIssue(DataCategory::MarginDaily, null_symbol(),
                 manifest.value("date", _targetDay), DataReason::Missing);
    }

    for (const auto& path : ManifestFiles(manifest)) {
        std::error_code pec;
        if (!std::filesystem::exists(path, pec)) {
            WARN("[Margin] manifest 里的 CSV 不存在，跳过: {}", path);
            continue;
        }
        String ex = exchangeOf(path);
        if (ex.empty()) {
            WARN("[Margin] 认不出交易所后缀，跳过: {}", path);
            continue;
        }
        _downloaded[ex] = path;
    }

    INFO("[Margin] 下载完成: {} 个交易所文件（{}）",
         _downloaded.size(), manifest.value("date", "?"));
    return static_cast<int>(_downloaded.size());
}

bool MarginData::checkCsv(const String& path, const String& exchange) {
    Vector<String> cols;
    if (!ReadCsvHeader(path, cols)) {
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        WARN("[Margin] CSV 读不出来: {}", path);
        return false;
    }

    bool ok = true;

    Vector<String> expected = {"code", "pubDate", "statDate"};
    for (const auto& f : kMarginFields) {
        expected.push_back(f);
    }
    Vector<String> missing = MissingColumns(cols, expected);
    if (!missing.empty()) {
        String m;
        for (size_t i = 0; i < missing.size(); i++) {
            if (i > 0) m += ", ";
            m += missing[i];
        }
        WARN("[Margin] {} 的 CSV 缺少期望列 [{}]: {}", exchange, m, path);
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Mismatch);
        ok = false;
    }

    int iCode = ColIndex(cols, "code");
    int iPub = ColIndex(cols, "pubDate");
    int iStat = ColIndex(cols, "statDate");
    if (iCode < 0 || iPub < 0 || iStat < 0) {
        if (ok) {
            AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                     DataReason::Invalid);
        }
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        return false;
    }
    String line;
    std::getline(ifs, line);

    int rows = 0;
    int badPub = 0;
    int badDate = 0;
    String statDate;
    String firstBad;
    String expectedPub;

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        Vector<String> row = SplitCsvLine(line);
        if (static_cast<int>(row.size()) <= iStat) continue;

        String stat = row[iStat];
        String pub = (static_cast<int>(row.size()) > iPub) ? row[iPub] : "";
        rows++;
        if (statDate.empty()) {
            statDate = stat;
            // 交易所盘后才公布，脚本按「交易日 + 1 天」写 pubDate
            time_t statT = IsYmdDate(stat) ? FromStr(stat, "%Y-%m-%d") : -1;
            if (statT < 0) {
                badDate++;
                if (firstBad.empty()) firstBad = fmt::format("交易日非法 {}", stat);
            } else {
                expectedPub = ToString(ShiftDay(statT, 1), "%Y-%m-%d");
            }
        }
        if (pub != expectedPub) {
            badPub++;
            if (firstBad.empty()) {
                firstBad = fmt::format("pub={} 期望={} stat={}", pub, expectedPub, stat);
            }
        }
    }

    if (rows == 0) {
        WARN("[Margin] {} 的 CSV 没有数据行: {}", exchange, path);
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        return false;
    }
    if (rows < _minRows) {
        WARN("[Margin] {} 只有 {} 行，低于下限 {}: {}", exchange, rows, _minRows, path);
        AddIssue(DataCategory::MarginDaily, null_symbol(), statDate,
                 DataReason::Insufficient);
        ok = false;
    }
    if (!_targetDay.empty() && statDate != _targetDay) {
        WARN("[Margin] {} 的交易日是 {}，期望 {}: {}", exchange, statDate, _targetDay, path);
        AddIssue(DataCategory::MarginDaily, null_symbol(), statDate,
                 DataReason::Stale);
        ok = false;
    }
    if (badPub > 0 || badDate > 0) {
        WARN("[Margin] {} 有 {} 行公告日不等于交易日+1、{} 行交易日非法（{}）",
             exchange, badPub, badDate, firstBad);
        AddIssue(DataCategory::MarginDaily, null_symbol(), statDate,
                 DataReason::Invalid);
        ok = false;
    }
    return ok;
}

bool MarginData::Check() {
    bool allOk = true;

    // 缺一个交易所就是残缺日，不能默认「拿到几个算几个」
    for (const auto& ex : _exchanges) {
        if (_downloaded.count(ex) == 0) {
            WARN("[Margin] 缺少 {} 的明细文件", ex);
            AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                     DataReason::Missing);
            allOk = false;
        }
    }

    for (const auto& [ex, path] : _downloaded) {
        if (!checkCsv(path, ex)) {
            _bad.insert(ex);
            allOk = false;
        }
    }

    INFO("[Margin] CSV 级体检完成: {} 个文件, {} 个不合格",
         _downloaded.size(), _bad.size());
    return allOk;
}

int MarginData::ImportData() {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) {
        if (!financeDB.init(_financeDir, "finance.db")) {
            WARN("[Margin] FinanceDB 初始化失败: {}", _financeDir);
            return -1;
        }
    }

    int total = 0;
    for (const auto& [ex, path] : _downloaded) {
        if (_bad.count(ex) > 0) {
            WARN("[Margin] {} 体检未过，跳过导入", ex);
            continue;
        }
        // 走通用类目通道：类目决定表结构，ON CONFLICT 保证重跑幂等
        int n = financeDB.importCsv(path, "margin_daily");
        if (n > 0) {
            total += n;
            // 导入成功后删掉中间 CSV。以前这里不删，磁盘上攒了 1955 个文件。
            std::error_code ec;
            std::filesystem::remove(path, ec);
        } else if (n < 0) {
            AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                     DataReason::Missing);
        }
    }

    checkDb();
    INFO("[Margin] 导入完成: {} 行", total);
    return total;
}

void MarginData::checkDb() {
    auto& financeDB = FinanceDB::instance();
    if (!financeDB.isInitialized()) return;

    nlohmann::json h = financeDB.queryCategoryHealth("margin_daily");
    if (h.contains("error")) {
        WARN("[Margin] margin_daily 表不可用: {}", h["error"].get<String>());
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        return;
    }

    int64_t rows = h.value("rows", 0);
    if (rows == 0) {
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
        return;
    }

    String latest = h.value("latest_date", "");
    if (!_targetDay.empty() && latest < _targetDay) {
        WARN("[Margin] 表里最新交易日 {} 落后于目标 {}", latest, _targetDay);
        AddIssue(DataCategory::MarginDaily, null_symbol(), latest, DataReason::Stale);
    }
    if (h.value("pub_date_before_stat", 0) > 0) {
        WARN("[Margin] {} 行的公告日早于交易日（前视）",
             h.value("pub_date_before_stat", 0));
        AddIssue(DataCategory::MarginDaily, null_symbol(), latest, DataReason::Invalid);
    }

    // 公告日必须正好是交易日 + 1 天
    int64_t offsetBad = financeDB.countWhere(
        "margin_daily", "pub_date <> stat_date + INTERVAL 1 DAY");
    if (offsetBad > 0) {
        WARN("[Margin] {} 行的公告日不等于交易日 + 1 天", offsetBad);
        AddIssue(DataCategory::MarginDaily, null_symbol(), latest, DataReason::Invalid);
    }

    // 整列 NULL = 数据源改了列名（SSE 的 rqBalance/totalBalance 本来就空，
    // 那是「整列全空」的正常情形，所以只对声明过的 8 个字段判整列 NULL）
    if (h.contains("null_counts") && h["null_counts"].is_object()) {
        for (auto it = h["null_counts"].begin(); it != h["null_counts"].end(); ++it) {
            if (it.value().get<int64_t>() == rows) {
                WARN("[Margin] 列 {} 整列为 NULL（列名与数据源对不上）", it.key());
                AddIssue(DataCategory::MarginDaily, null_symbol(), latest,
                         DataReason::Mismatch);
            }
        }
    }
}

int MarginData::UpdateData() {
    Vector<DataIssue> pending = TakeMyIssues(Type());
    if (pending.empty()) {
        return 0;
    }

    // 两融是全市场级数据（交易所接口没有逐标的查询的口子），
    // 问题都落在「哪一天 / 哪个交易所」上，补法就是整日重抓一次。
    size_t before = _issues.size();

    if (_dst.empty()) {
        WARN("[Margin] 补数据缺少目标目录，跳过（调用方应先 SetDst）");
        RecordIssues(pending);
        return 0;
    }
    int dl = Download(_dst);
    int fixed = 0;
    if (dl < 0) {
        AddIssue(DataCategory::MarginDaily, null_symbol(), _targetDay,
                 DataReason::Missing);
    } else {
        Check();
        fixed = (ImportData() > 0) ? 1 : 0;   // 修好了一天
    }

    // 补完仍然没解决的重新入队，等下一轮 / 凌晨再试。
    // 修好的那些已经被 TakeMyIssues 从队列里摘掉了。
    Vector<DataIssue> stillBad;
    for (size_t i = before; i < _issues.size(); i++) {
        stillBad.push_back(_issues[i]);
    }
    RecordIssues(stillBad);
    return fixed;
}
