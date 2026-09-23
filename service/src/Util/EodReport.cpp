#include "Util/EodReport.h"
#include "server.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Nodes/XGBoostNode.h"
#include "Nodes/OnnxInferenceNode.h"
#include "Nodes/SignalNode.h"
#include "Interprecter/Stmt.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

// 辅助函数：从 context 获取 Vector<double> 的最后一个值
static double getLastValue(DataContext& context, const String& key) {
    if (!context.exist(key)) return std::numeric_limits<double>::quiet_NaN();
    
    const auto& val = context.get(key);
    if (auto* vec = std::get_if<Vector<double>>(&val)) {
        return vec->empty() ? std::numeric_limits<double>::quiet_NaN() : vec->back();
    } else if (auto* scalar = std::get_if<double>(&val)) {
        return *scalar;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// 辅助函数：从 context 获取 Vector<double> 的完整序列
static Vector<double> getVector(DataContext& context, const String& key) {
    if (!context.exist(key)) return {};
    
    const auto& val = context.get(key);
    if (auto* vec = std::get_if<Vector<double>>(&val)) {
        return *vec;
    } else if (auto* scalar = std::get_if<double>(&val)) {
        return {*scalar};
    }
    return {};
}

// 辅助函数：写宽表 CSV
static bool WriteWideCsv(const String& path,
                         const Vector<time_t>& times,
                         const Map<String, Vector<double>>& columns) {
    if (times.empty() || columns.empty()) return false;
    
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs) return false;
    
    // 写表头
    ofs << "datetime";
    for (const auto& [name, col] : columns) {
        ofs << "," << name;
    }
    ofs << "\n";
    
    // 写数据行
    for (size_t i = 0; i < times.size(); ++i) {
        ofs << ToString(times[i], "%Y-%m-%d %H:%M:%S");
        for (const auto& [name, col] : columns) {
            if (i < col.size()) {
                std::ostringstream oss;
                oss << std::setprecision(17) << col[i];
                ofs << "," << oss.str();
            } else {
                ofs << ",";
            }
        }
        ofs << "\n";
    }
    
    return true;
}

// 辅助函数：格式化数值
static String formatDouble(double v, int precision = 4) {
    if (std::isnan(v)) return "NaN";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

// 辅助函数：生成 HTML 表格行
static String htmlTableRow(const String& symbol, const String& decision,
                          const Map<String, double>& values, const Vector<String>& classNames) {
    String html = "<tr>";
    html += fmt::format("<td style='padding:8px 10px;border-bottom:1px solid #f1f5f9;font-weight:600;'>{}</td>", symbol);
    
    // 决策标签
    String tagColor = (decision == "BUY") ? "#dcfce7;color:#16a34a" :
                      (decision == "SELL") ? "#fee2e2;color:#dc2626" :
                      "#f1f5f9;color:#94a3b8";
    html += fmt::format("<td style='padding:8px 10px;border-bottom:1px solid #f1f5f9;'><span style='display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;background:{};'>{}</span></td>",
                        tagColor, decision);
    
    // 数值列
    for (const auto& [key, val] : values) {
        html += fmt::format("<td style='padding:8px 10px;border-bottom:1px solid #f1f5f9;text-align:right;'>{}</td>",
                            formatDouble(val, 3));
    }
    
    html += "</tr>";
    return html;
}

EodDebugReport BuildEodDebugReport(Server* server,
                                   const String& strategy,
                                   const List<QNode*>& graph,
                                   DataContext& context,
                                   const Set<symbol_t>& universe,
                                   const Map<symbol_t, DecisionSnapshot>& decisions,
                                   const EodDebugSpec& spec) {
    EodDebugReport report;
    
    try {
        auto& cfg = server->GetConfig();
        auto dataDir = cfg.GetDatabasePath();
        auto today = ToString(Now(), "%Y-%m-%d");
        auto reportDir = dataDir + "/reports/" + strategy + "/" + today;
        std::filesystem::create_directories(reportDir);
        
        // 1. 提取标签映射和模型特征顺序
        Map<String, String> keyToLabel;  // context key → 显示标签
        Vector<String> featureOrder;     // 模型特征顺序（用于 snapshot.csv 列序）
        
        for (auto* node : graph) {
            auto outs = node->out_elements();
            
            // 识别 XGBoost/ONNX 节点，提取特征顺序
            if (auto* xgb = dynamic_cast<XGBoostNode*>(node)) {
                for (const auto& [sym, keys] : xgb->resolvedFeatures()) {
                    for (size_t i = 0; i < keys.size(); ++i) {
                        const auto& key = keys[i];
                        if (i < xgb->featureKeys().size()) {
                            String featName = xgb->featureKeys()[i];
                            keyToLabel[key] = featName;
                            featureOrder.push_back(key);
                        }
                    }
                }
            } else if (auto* onnx = dynamic_cast<OnnxInferenceNode*>(node)) {
                for (const auto& [sym, keys] : onnx->resolvedFeatures()) {
                    for (size_t i = 0; i < keys.size(); ++i) {
                        const auto& key = keys[i];
                        if (i < onnx->featureKeys().size()) {
                            String featName = onnx->featureKeys()[i];
                            keyToLabel[key] = featName;
                            featureOrder.push_back(key);
                        }
                    }
                }
            }
        }
        
        // 2. 收集决策标的
        Vector<symbol_t> decisionSymbols;
        Vector<symbol_t> holdSymbols;
        
        for (const auto& [sym, snap] : decisions) {
            if (snap._action == TradeAction::BUY || snap._action == TradeAction::SELL) {
                decisionSymbols.push_back(sym);
            } else {
                holdSymbols.push_back(sym);
            }
        }
        
        // HOLD 标的取 TopN（简单按 symbol 排序，后续可改为按 strength 排序）
        std::sort(holdSymbols.begin(), holdSymbols.end(),
                  [](symbol_t a, symbol_t b) { return get_symbol(a) < get_symbol(b); });
        if ((int)holdSymbols.size() > spec.holdTopN) {
            holdSymbols.resize(spec.holdTopN);
        }
        
        // 合并决策标的
        Vector<symbol_t> allSymbols = decisionSymbols;
        allSymbols.insert(allSymbols.end(), holdSymbols.begin(), holdSymbols.end());
        
        if (allSymbols.empty()) {
            report.warnings.push_back("No decision symbols found");
            return report;
        }
        
        // 3. 构建 snapshot.csv 列
        Map<String, Vector<double>> snapshotColumns;
        
        // 模型特征列（按模型顺序）
        for (const auto& featKey : featureOrder) {
            if ((int)snapshotColumns.size() >= spec.maxSnapshotColumns) break;
            
            Vector<double> col;
            for (auto sym : allSymbols) {
                String fullKey = get_symbol(sym) + "." + featKey.substr(featKey.find('.') + 1);
                col.push_back(getLastValue(context, fullKey));
            }
            snapshotColumns[featKey] = col;
        }
        
        // 概率列
        for (int i = 0; i < 3; ++i) {  // 假设 3 分类
            String probKey = "xgb_probs_" + std::to_string(i);
            Vector<double> col;
            for (auto sym : allSymbols) {
                String fullKey = get_symbol(sym) + "." + probKey;
                col.push_back(getLastValue(context, fullKey));
            }
            if (!col.empty() && !std::isnan(col[0])) {
                snapshotColumns[probKey] = col;
            }
        }
        
        // 信号列
        {
            Vector<double> col;
            for (auto sym : allSymbols) {
                String fullKey = get_symbol(sym) + ".signal";
                col.push_back(getLastValue(context, fullKey));
            }
            snapshotColumns["signal"] = col;
        }
        
        // 4. 写 snapshot.csv
        if (spec.attachSnapshot) {
            auto snapshotPath = reportDir + "/snapshot.csv";
            auto times = context.GetTime();
            Vector<time_t> lastTime = times.empty() ? Vector<time_t>{} : Vector<time_t>{times.back()};
            
            if (WriteWideCsv(snapshotPath, lastTime, snapshotColumns)) {
                report.attachments.push_back(snapshotPath);
                auto size = std::filesystem::file_size(snapshotPath);
                report.summaryLine += fmt::format("snapshot.csv ({:.1f} KB)", size / 1024.0);
            }
        }
        
        // 5. 构建 series.csv 列（滚动窗口）
        if (spec.attachSeries) {
            Map<String, Vector<double>> seriesColumns;
            auto times = context.GetTime();
            size_t startIdx = times.size() > (size_t)spec.seriesBars ? 
                              times.size() - spec.seriesBars : 0;
            Vector<time_t> seriesTimes(std::next(times.begin(), startIdx), times.end());
            
            // OHLCV 列
            for (auto sym : allSymbols) {
                String symStr = get_symbol(sym);
                for (const auto& field : {"close", "open", "high", "low", "volume"}) {
                    String key = symStr + "." + field;
                    auto vec = getVector(context, key);
                    if (vec.size() > startIdx) {
                        seriesColumns[key] = Vector<double>(vec.begin() + startIdx, vec.end());
                    }
                }
            }
            
            // 概率列
            for (int i = 0; i < 3; ++i) {
                String probKey = "xgb_probs_" + std::to_string(i);
                for (auto sym : allSymbols) {
                    String fullKey = get_symbol(sym) + "." + probKey;
                    auto vec = getVector(context, fullKey);
                    if (vec.size() > startIdx) {
                        seriesColumns[fullKey] = Vector<double>(vec.begin() + startIdx, vec.end());
                    }
                }
            }
            
            // 信号列
            for (auto sym : allSymbols) {
                String fullKey = get_symbol(sym) + ".signal";
                auto vec = getVector(context, fullKey);
                if (vec.size() > startIdx) {
                    seriesColumns[fullKey] = Vector<double>(vec.begin() + startIdx, vec.end());
                }
            }
            
            auto seriesPath = reportDir + "/series.csv";
            if (WriteWideCsv(seriesPath, seriesTimes, seriesColumns)) {
                report.attachments.push_back(seriesPath);
                auto size = std::filesystem::file_size(seriesPath);
                if (!report.summaryLine.empty()) report.summaryLine += " · ";
                report.summaryLine += fmt::format("series.csv ({:.1f} KB)", size / 1024.0);
            }
        }
        
        // 6. 生成 HTML 片段
        String html;
        html += R"h(<div style="padding:16px 24px;">)h";
        html += R"h(<div style="font-size:12px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px;">决策解释</div>)h";
        
        // 表头
        html += R"h(<table style="width:100%;border-collapse:collapse;font-size:13px;">)h";
        html += R"h(<thead><tr>)h";
        html += R"h(<th style="text-align:left;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">标的</th>)h";
        html += R"h(<th style="text-align:left;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">决策</th>)h";
        
        // 概率列表头
        for (size_t i = 0; i < spec.classNames.size(); ++i) {
            html += fmt::format(R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">{}</th>)h",
                                spec.classNames[i]);
        }
        if (spec.classNames.empty()) {
            html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">P0</th>)h";
            html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">P1</th>)h";
            html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">P2</th>)h";
        }
        
        html += R"h(<th style="text-align:right;padding:8px 10px;font-weight:600;color:#64748b;font-size:11px;border-bottom:2px solid #e2e8f0;">信号</th>)h";
        html += "</tr></thead><tbody>";
        
        // 数据行
        for (auto sym : allSymbols) {
            String symStr = get_symbol(sym);
            auto it = decisions.find(sym);
            String decision = (it != decisions.end()) ? 
                              ((it->second._action == TradeAction::BUY) ? "BUY" :
                               (it->second._action == TradeAction::SELL) ? "SELL" : "HOLD") : "HOLD";
            
            Map<String, double> values;
            
            // 概率
            for (int i = 0; i < 3; ++i) {
                String key = symStr + ".xgb_probs_" + std::to_string(i);
                values["P" + std::to_string(i)] = getLastValue(context, key);
            }
            
            // 信号
            values["信号"] = getLastValue(context, symStr + ".signal");
            
            html += htmlTableRow(symStr, decision, values, spec.classNames);
        }
        
        html += "</tbody></table>";
        
        // 生成文件清单
        if (!report.summaryLine.empty()) {
            html += R"h(<div style="margin-top:12px;padding:12px 16px;background:#f8fafc;border-radius:8px;font-size:12px;color:#64748b;">)h";
            html += "📎 " + report.summaryLine;
            html += "</div>";
        }
        
        html += "</div>";
        report.htmlSection = html;
        
        // 7. 构建结构化 JSON
        report.rows = nlohmann::json::array();
        for (auto sym : allSymbols) {
            nlohmann::json row;
            row["symbol"] = get_symbol(sym);
            
            auto it = decisions.find(sym);
            if (it != decisions.end()) {
                row["action"] = (it->second._action == TradeAction::BUY) ? "BUY" :
                                (it->second._action == TradeAction::SELL) ? "SELL" : "HOLD";
                row["quantity"] = it->second._quantity;
                row["price"] = it->second._price;
            }
            
            nlohmann::json probs;
            for (int i = 0; i < 3; ++i) {
                String key = get_symbol(sym) + ".xgb_probs_" + std::to_string(i);
                probs["P" + std::to_string(i)] = getLastValue(context, key);
            }
            row["probabilities"] = probs;
            row["signal"] = getLastValue(context, get_symbol(sym) + ".signal");
            
            report.rows.push_back(row);
        }
        
    } catch (const std::exception& e) {
        WARN("[EodReport] BuildEodDebugReport failed: {}", e.what());
        report.warnings.push_back(String("Exception: ") + e.what());
        report.htmlSection = R"h(<div style="padding:16px 24px;color:#ef4444;">⚠️ 报告生成失败</div>)h";
    } catch (...) {
        WARN("[EodReport] BuildEodDebugReport failed: unknown error");
        report.warnings.push_back("Unknown exception");
        report.htmlSection = R"h(<div style="padding:16px 24px;color:#ef4444;">⚠️ 报告生成失败</div>)h";
    }
    
    return report;
}
