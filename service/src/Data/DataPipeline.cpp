#include "Data/DataPipeline.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include <filesystem>
#include <fstream>

// ═══════════════════════════════════════════════════════════
//  RefreshState
// ═══════════════════════════════════════════════════════════

RefreshState::RefreshState(const String& dbPath)
    : _path(dbPath + "/data_refresh_state.json") {
    load();
}

void RefreshState::load() {
    std::lock_guard<std::mutex> lock(_mtx);
    std::ifstream ifs(_path);
    if (!ifs.is_open()) return;
    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) {
                _states[it.key()] = it.value().get<String>();
            }
        }
    } catch (const std::exception& e) {
        WARN("[RefreshState] 状态文件解析失败，按未刷新处理: {}", e.what());
        _states.clear();
    }
}

void RefreshState::save() const {
    std::error_code ec;
    std::filesystem::path p(_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [k, v] : _states) {
        j[k] = v;
    }
    std::ofstream ofs(_path);
    if (!ofs.is_open()) {
        WARN("[RefreshState] 状态文件写入失败: {}", _path);
        return;
    }
    ofs << j.dump(2);
}

String RefreshState::LastDone(const String& key) const {
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _states.find(key);
    return it == _states.end() ? String() : it->second;
}

void RefreshState::MarkDone(const String& key, const String& targetDay) {
    std::lock_guard<std::mutex> lock(_mtx);
    _states[key] = targetDay;
    save();
    INFO("[RefreshState] {} 已刷新到 {}", key, targetDay);
}

bool RefreshState::NeedRefresh(const String& key, const String& targetDay) const {
    return LastDone(key) != targetDay;
}

void RefreshState::Clear(const String& key) {
    std::lock_guard<std::mutex> lock(_mtx);
    if (_states.erase(key) > 0) {
        save();
    }
}

// ═══════════════════════════════════════════════════════════
//  DataPipeline
// ═══════════════════════════════════════════════════════════

String DataPipeline::TargetTradingDay() {
    // 最近一个已收盘的交易日：收盘前（15:05 之前）当天还没有完整日线，
    // 从前一天开始找；周末与法定节假日跳过。
    return LastSettledTradingDay(Now());
}

int DataPipeline::Run(const String& dst, const StageCallback& onStage) {
    const DataType type = _data.Type();
    _data.ClearIssues();
    _data.ResetRows();

    const auto stage = [&onStage](DataStage s) {
        if (onStage) onStage(s);
    };

    // ── 1. 下载 ──
    stage(DataStage::Download);
    int dl = _data.Download(dst);
    if (dl < 0) {
        _data.AddIssue(DataCategory::Day, null_symbol(), "", DataReason::Missing);
        DownloadBase::RecordIssues(_data.Issues());
        WARN("[Pipeline] type={} 下载失败", static_cast<int>(type));
        return -1;
    }

    // ── 2. CSV 级体检 ──
    stage(DataStage::Check);
    _data.Check();

    // ── 3. 导入（好的入库，坏的跳过；末尾做 DB 级复查）──
    stage(DataStage::Import);
    int imported = _data.ImportData();

    // ── 4. 本轮问题进队列 ──
    DownloadBase::RecordIssues(_data.Issues());

    // ── 5. 立刻重试一次 ──
    stage(DataStage::Retry);
    int retried = _data.UpdateData();

    // 返回累计行数：补数据那一段也入了库，只报第一段会少算一大截
    const int total = _data.RowsImported();
    INFO("[Pipeline] type={} 完成: 下载 {} 个, 导入 {} 行（入库 {} + 重试 {}）, 重试修复 {} 项, 遗留问题 {} 条",
         static_cast<int>(type), dl, total, imported, total - imported, retried,
         _data.Issues().size());
    return total;
}

int DataPipeline::Repair(const String& dst) {
    _data.SetDst(dst);
    int n = _data.UpdateData();
    INFO("[Pipeline] type={} 补数据: 修复 {} 项",
         static_cast<int>(_data.Type()), n);
    return n;
}
