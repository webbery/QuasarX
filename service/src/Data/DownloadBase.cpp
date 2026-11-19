#include "Data/DownloadBase.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/system.h"
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

namespace {

// 已记录的异常（下载链路统一入口，凌晨补下载时取用）
std::mutex g_issueMtx;
Vector<DataIssue> g_issues;

}  // namespace

// ═══════════════════════════════════════════════════════════
//  DownloadBase —— 公共能力
// ═══════════════════════════════════════════════════════════

const char* DownloadBase::ToolFor(DataType type) {
    switch (type) {
        case DataType::Quote:       return "download_quote_bs.py";
        case DataType::Margin:      return "fetch_margin_data.py";
        case DataType::Fundamental: return "fetch_finance_data.py";
    }
    return "unknown";
}

String DownloadBase::ShellQuote(const String& s) {
    String result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";  // 结束单引号，加转义单引号，再开始单引号
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

bool DownloadBase::RunScript(const String& interpreter, const String& script,
                             const String& args, String& output) {
    // 脚本都放在 tools 目录下，且脚本内部按「相对脚本目录」读写数据文件，
    // 所以工作目录固定切到 tools。解释器由调用方按配置解析后传进来。
    String cmd = interpreter + " tools/" + script;
    if (!args.empty()) {
        cmd += " " + args;
    }
    INFO("[Download] exec: {}", cmd);
    return RunCommand(cmd, output);
}

bool DownloadBase::ParseManifest(const String& output, nlohmann::json& out) {
    auto begin = output.find('{');
    auto end = output.rfind('}');
    if (begin == String::npos || end == String::npos || end <= begin) {
        return false;
    }
    try {
        out = nlohmann::json::parse(output.substr(begin, end - begin + 1));
        return true;
    } catch (const std::exception& e) {
        WARN("[Download] manifest parse failed: {}", e.what());
        return false;
    }
}

Vector<String> DownloadBase::ManifestFiles(const nlohmann::json& manifest) {
    Vector<String> files;
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        return files;
    }
    for (const auto& f : manifest["files"]) {
        if (f.is_string()) {
            String p = f.get<String>();
            if (!p.empty()) files.push_back(p);
        } else if (f.is_object()) {
            // fetch_finance_data.py 的形态：{symbol, category, path, rows}
            String p = f.value("path", "");
            if (!p.empty()) files.push_back(p);
        }
    }
    // 老版本脚本只给 "file" 单数
    if (files.empty() && manifest.contains("file") && manifest["file"].is_string()) {
        String p = manifest["file"].get<String>();
        if (!p.empty()) files.push_back(p);
    }
    return files;
}

Vector<String> DownloadBase::SplitCsvLine(const String& line) {
    Vector<String> cols;
    String cur;
    bool in_quote = false;
    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (c == ',' && !in_quote) {
            cols.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    cols.push_back(cur);
    return cols;
}

bool DownloadBase::ReadCsvHeader(const String& path, Vector<String>& cols) {
    cols.clear();
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }
    String header;
    if (!std::getline(ifs, header)) {
        return false;
    }
    // 去 BOM（fetch_margin_data.py 用 utf-8-sig 写出）
    if (header.size() >= 3 && header[0] == '\xEF' && header[1] == '\xBB' &&
        header[2] == '\xBF') {
        header = header.substr(3);
    }
    // 去行尾 \r
    while (!header.empty() && (header.back() == '\r' || header.back() == '\n')) {
        header.pop_back();
    }
    cols = SplitCsvLine(header);
    return !cols.empty();
}

int DownloadBase::ColIndex(const Vector<String>& cols, const String& name) {
    for (size_t i = 0; i < cols.size(); i++) {
        if (cols[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Vector<String> DownloadBase::MissingColumns(const Vector<String>& cols,
                                             const Vector<String>& expected) {
    Vector<String> missing;
    for (const auto& e : expected) {
        if (ColIndex(cols, e) < 0) {
            missing.push_back(e);
        }
    }
    return missing;
}

void DownloadBase::AddIssue(DataCategory category, symbol_t symbol,
                            const String& date, DataReason reason) {
    DataIssue issue;
    issue._type = Type();
    issue._category = category;
    issue._symbol = symbol;
    issue._date = date;
    issue._reason = reason;
    _issues.push_back(issue);
}

void DownloadBase::RecordIssues(const Vector<DataIssue>& issues) {
    if (issues.empty()) return;
    std::lock_guard<std::mutex> lock(g_issueMtx);
    for (const auto& issue : issues) {
        g_issues.push_back(issue);
        WARN("[DataCheck] type={} cat={} sym={} date={} reason={}（补数据用 {}）",
             static_cast<int>(issue._type),
             static_cast<int>(issue._category),
             get_symbol(issue._symbol),
             issue._date.empty() ? "-" : issue._date,
             static_cast<int>(issue._reason),
             ToolFor(issue._type));
    }
}

Vector<DataIssue> DownloadBase::TakeIssues() {
    std::lock_guard<std::mutex> lock(g_issueMtx);
    Vector<DataIssue> result;
    result.swap(g_issues);
    return result;
}

Vector<DataIssue> DownloadBase::TakeMyIssues(DataType type) {
    std::lock_guard<std::mutex> lock(g_issueMtx);
    Vector<DataIssue> mine;
    Vector<DataIssue> others;
    for (const auto& issue : g_issues) {
        if (issue._type == type) {
            mine.push_back(issue);
        } else {
            others.push_back(issue);
        }
    }
    // 别的类型的问题原样放回去
    g_issues.swap(others);
    return mine;
}
