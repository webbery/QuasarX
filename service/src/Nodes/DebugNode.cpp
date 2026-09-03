#include "Nodes/DebugNode.h"
#include "server.h"
#include "Util/datetime.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <variant>
#include "Util/string_algorithm.h"

DebugNode::DebugNode(Server* server)
:_server(server), _context(nullptr) {

}

bool DebugNode::Init(const nlohmann::json& config) {
    // suffix: 导出文件扩展名（仅 export 模式生效）
    if (config.contains("params") && config["params"].contains("suffix")
        && config["params"]["suffix"].contains("value")) {
        _suffix = to_lower((String)config["params"]["suffix"]["value"]);
    } else {
        _suffix = "csv";
    }
    _label = (String)config["label"];

    // mode: export (默认) | import
    if (config.contains("params") && config["params"].contains("mode")
        && config["params"]["mode"].contains("value")) {
        _mode = to_lower((String)config["params"]["mode"]["value"]);
    } else {
        _mode = "export";
    }

    // file_path: import 模式的 CSV 文件路径
    if (config.contains("params") && config["params"].contains("file_path")
        && config["params"]["file_path"].contains("value")) {
        _filePath = (String)config["params"]["file_path"]["value"];
    }

    // export 模式：收集上游输出名（保持现有行为）
    // import 模式：列名在 Prepare() 读 CSV 后通过 out_elements() 暴露
    if (_mode == "export") {
        for (auto node: _ins) {
            auto names = node.second->out_elements();
            for (auto& name: names) {
                _inNames.insert(name.first);
            }
        }
    }

    return true;
}

void DebugNode::Prepare(const String& strategy, DataContext& context) {
    if (_mode == "import") {
        if (!importFromCsv(context)) {
            WARN("[DebugNode] import 模式初始化失败: file_path='{}'", _filePath);
        }
    }
}

bool DebugNode::importFromCsv(DataContext& context) {
    if (_filePath.empty()) {
        WARN("[DebugNode] import 模式缺少 file_path 参数");
        return false;
    }

    std::ifstream ifs(_filePath);
    if (!ifs.is_open()) {
        WARN("[DebugNode] 无法打开 CSV 文件: {}", _filePath);
        return false;
    }

    String line;
    Vector<String> columnNames;
    Map<String, Vector<double>> columnData;  // column name → values

    // 解析 header
    if (!std::getline(ifs, line)) {
        WARN("[DebugNode] CSV 文件为空: {}", _filePath);
        return false;
    }
    {
        std::stringstream ss(line);
        String cell;
        while (std::getline(ss, cell, ',')) {
            // 去除前后空白
            cell.erase(0, cell.find_first_not_of(" \t\r\n"));
            cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
            columnNames.push_back(cell);
        }
    }
    if (columnNames.empty() || columnNames[0] != "datetime") {
        WARN("[DebugNode] CSV 第一列必须为 datetime，实际为: '{}'", columnNames.empty() ? "(空)" : columnNames[0]);
        return false;
    }
    const size_t nCols = columnNames.size();
    for (size_t i = 1; i < nCols; ++i) {
        columnData[columnNames[i]] = Vector<double>{};
    }

    // 解析数据行
    size_t rowCount = 0;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        String cell;
        Vector<String> cells;
        while (std::getline(ss, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() < nCols) {
            WARN("[DebugNode] CSV 第 {} 行列数不足 ({} < {})，跳过", rowCount + 2, cells.size(), nCols);
            continue;
        }
        // 解析时间戳
        time_t t = FromStr(cells[0], "%Y-%m-%d %H:%M:%S");
        if (t < 0) {
            t = FromStr(cells[0], "%Y-%m-%d");
        }
        if (t < 0) {
            WARN("[DebugNode] CSV 第 {} 行时间解析失败: '{}'，跳过", rowCount + 2, cells[0]);
            continue;
        }
        context.SetTime(t);
        // 解析数据列
        for (size_t i = 1; i < nCols; ++i) {
            const String& colName = columnNames[i];
            if (cells[i].empty()) {
                columnData[colName].push_back(std::numeric_limits<double>::quiet_NaN());
            } else {
                try {
                    columnData[colName].push_back(std::stod(cells[i]));
                } catch (...) {
                    columnData[colName].push_back(std::numeric_limits<double>::quiet_NaN());
                }
            }
        }
        ++rowCount;
    }

    // 写入 context
    for (auto& [colName, values] : columnData) {
        context.set<Vector<double>>(colName, values);
        _outElements[colName] = Double_TimeSeries;
    }

    INFO("[DebugNode] import 成功: file='{}', rows={}, cols={}", _filePath, rowCount, nCols - 1);
    return true;
}

NodeProcessResult DebugNode::Process(const String& strategy, DataContext& context) {
    _context = &context;

    if (_mode == "import") {
        // import 模式：数据已在 Prepare() 注入到 context，无需逐 bar 处理
        return NodeProcessResult::Success;
    }

    return NodeProcessResult::Success;
}

void DebugNode::Done(const String& strategy) {
    if (_mode == "import") {
        // import 模式无需导出
        return;
    }

    if (!_context)
        return;

    // 保存数据到/data/debug/strategy 路径以便下载
    auto& cfg = _server->GetConfig();
    auto dir = cfg.GetDatabasePath() + "/data/debug/" + strategy;
    auto& times = _context->GetTime();

    // 收集列数据
    Vector<time_t> ts(times.begin(), times.end());
    Map<String, Vector<double>> columns;

    for (auto& name: _inNames) {
        if (!_context->exist(name)) {
            WARN("DebugNode: key {} not found in context, skipping", name);
            continue;
        }
        auto& feature = _context->get(name);
        INFO("read colunm {}", name);
        std::visit([&name, &columns](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, double>) {
                columns[name] = Vector<double>{val};
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
                columns[name] = Vector<double>(val.begin(), val.end());
            }
            else if constexpr (std::is_same_v<T, Vector<double>>) {
                columns[name] = val;
            } else {
                INFO("DebugNode::Done");
            }
        }, feature);
    }

    if (_suffix == "csv" && !ts.empty()) {
        String save_path = dir + "/" + _label + ".csv";
        std::filesystem::create_directories(dir);
        INFO("[DebugNode] Writing CSV: path='{}', rows={}, cols={}", save_path, ts.size(), columns.size());

        std::ofstream ofs(save_path);
        // header
        ofs << "datetime";
        for (auto& [name, col] : columns) {
            ofs << "," << name;
        }
        ofs << "\n";
        // rows
        for (size_t i = 0; i < ts.size(); ++i) {
            ofs << ToString(ts[i], "%Y-%m-%d %H:%M:%S");
            for (auto& [name, col] : columns) {
                if (i < col.size()) {
                    // 全精度输出（std::to_string 默认 6 位小数会丢失精度，
                    // 导致 XGBoost 特征经树分裂边界时与 Python 推理不一致）
                    std::ostringstream oss;
                    oss << std::setprecision(17) << col[i];
                    ofs << "," << oss.str();
                } else {
                    ofs << ",";
                }
            }
            ofs << "\n";
        }
        INFO("[DebugNode] CSV written successfully: {}", save_path);
    } else {
        WARN("[DebugNode] CSV NOT written: suffix='{}', ts.empty()={}", _suffix, ts.empty());
    }
}

Map<String, ArgType> DebugNode::out_elements() {
    if (_mode == "import") {
        return _outElements;
    }
    return QNode::out_elements();
}

const nlohmann::json DebugNode::getParams() {
    return {};
}
