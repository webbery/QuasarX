#include "Handler/SignalHandler.h"
#include "Util/system.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "Algorithms/EMD_SIMD.h"
#include "Algorithms/CEEMDAN.h"
#include "Algorithms/VMD.h"
#include "Util/datetime.h"
#include "server.h"
#include <sstream>

void SignalHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        auto db_path = _server->GetConfig().GetDatabasePath();
        auto symbols_param = req.get_param_value("symbols");
        auto start_date = req.get_param_value("start_date");
        auto end_date = req.get_param_value("end_date");
        auto field = req.get_param_value("field");
        auto method = req.get_param_value("method");
        auto num_imfs_param = req.get_param_value("num_imfs");
        auto fill_param = req.get_param_value("fill_method");
        auto rolling_window_param = req.get_param_value("rolling_window");

        if (symbols_param.empty()) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "symbols parameter is required";
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (field.empty()) field = "close";
        if (method.empty()) method = "emd";
        FillMethod fill = fill_param.empty() ? FillMethod::None : parseFillMethod(fill_param);

        int num_imfs = 5;
        if (!num_imfs_param.empty()) {
            try { num_imfs = std::stoi(num_imfs_param); } catch (...) {}
        }
        if (num_imfs < 1 || num_imfs > 20) num_imfs = 5;

        int rolling_window = 0;
        if (!rolling_window_param.empty()) {
            try { rolling_window = std::stoi(rolling_window_param); } catch (...) {}
        }
        if (rolling_window < 0) rolling_window = 0;

        // 解析 symbols（逗号分隔）
        std::vector<String> symbols;
        std::istringstream ss(symbols_param);
        String sym;
        while (std::getline(ss, sym, ',')) {
            if (!sym.empty()) symbols.push_back(sym);
        }

        // 构建响应
        nlohmann::json json;
        json["symbols"] = symbols;
        json["field"] = field;
        json["method"] = method;

        Vector<String> dates;
        Vector<double> original;

        // 只处理第一个 symbol（单标的分析）
        if (!symbols.empty()) {
            // 检测是否为宏观指标 (格式: country/indicator)
            auto slash = symbols[0].find('/');
            bool is_macro = (slash != std::string::npos && symbols[0].size() > slash + 1 && symbols[0].find('.', slash) == std::string::npos);

            if (is_macro) {
                // 宏观指标数据
                Vector<String> macro_dates;
                Vector<double> macro_prices;
                if (!FetchMacroData(symbols[0], db_path, macro_dates, macro_prices)) {
                    res.status = 400;
                    nlohmann::json err;
                    err["error"] = fmt::format("No macro data for {}", symbols[0]);
                    res.set_content(err.dump(), "application/json");
                    return;
                }
                dates = macro_dates;
                original = macro_prices;
            } else {
                // 股票/ETF行情数据
                auto multi = LoadHistoryData(symbols[0], {"close", "open", "high", "low", "volume", "turnover"},
                                              start_date, end_date, &dates, fill);
                auto it = multi.find(field);
                if (it != multi.end() && !it->second.empty()) {
                    original = it->second;
                }
            }
            json["dates"] = dates;
            json["original"] = original;
        }

        if (original.size() < 10) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "Insufficient data (need at least 10 points)";
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 执行 EMD / CEEMDAN / VMD 分解
        nlohmann::json info_json = nlohmann::json::array();
        if (method == "ceemdan") {
            CEEMDAN ceemdan;
            CEEMDAN::Config cfg;
            cfg.numIMFs = num_imfs;
            cfg.ensembles = 30;
            cfg.noiseStd = 0.2;
            cfg.maxSiftingIter = 10;
            cfg.sdThreshold = 0.02;

            auto result = ceemdan.decompose(original, cfg);

            nlohmann::json imf_json = nlohmann::json::array();
            for (size_t i = 0; i < result.imfs.size(); ++i) {
                imf_json.push_back(result.imfs[i]);

                nlohmann::json info;
                info["index"] = static_cast<int>(i) + 1;
                info["mean_period"] = finance::estimateMeanPeriod(result.imfs[i]);
                info["energy_pct"] = finance::computeEnergyPct(result.imfs[i], original);
                info_json.push_back(info);
            }

            json["imf_components"] = imf_json;
            json["residual"] = result.residual;
            json["imf_info"] = info_json;
            json["reconstruction_error"] = result.reconstructionError;
        } else if (method == "vmd") {
            // VMD:频域变分分解(ADMM 求解),使用 FFTW3 后端
            VMD vmd;
            VMD::Config vcfg;
            vcfg.K = num_imfs;
            vcfg.alpha = 2000.0;
            vcfg.tau = 0.0;
            vcfg.tol = 1e-6;
            vcfg.maxIter = 200;
            vcfg.symmetricPad = true;

            auto vresult = vmd.decompose(original, vcfg);

            nlohmann::json imf_json = nlohmann::json::array();
            for (size_t i = 0; i < vresult.imfs.size(); ++i) {
                imf_json.push_back(vresult.imfs[i]);

                nlohmann::json info;
                info["index"] = static_cast<int>(i) + 1;
                info["mean_period"] = finance::estimateMeanPeriod(vresult.imfs[i]);
                info["energy_pct"] = finance::computeEnergyPct(vresult.imfs[i], original);
                info["center_freq"] = vresult.centerFreqs[i];
                info_json.push_back(info);
            }

            json["imf_components"] = imf_json;
            json["residual"] = vresult.residual;
            json["imf_info"] = info_json;
            json["reconstruction_error"] = 0.0; // VMD 残差即 residual
            {
                double rms = 0;
                for (size_t i = 0; i < vresult.residual.size(); ++i) {
                    rms += vresult.residual[i] * vresult.residual[i];
                }
                json["reconstruction_error"] = std::sqrt(rms / vresult.residual.size());
            }
            json["vmd_meta"] = {
                {"actual_k", vresult.actualK},
                {"iterations", vresult.iterations},
                {"converged", vresult.converged},
                {"center_freqs", vresult.centerFreqs}
            };

            // ─── VMD 滚动窗口稳定性分析 ───
            if (rolling_window > 0 && (int)original.size() > rolling_window) {
                int rw = std::min(rolling_window, (int)original.size() / 2);
                int step = 1;
                int n_windows = ((int)original.size() - rw) / step + 1;

                if (n_windows >= 2) {
                    // 存储每个窗口的中心频率和能量
                    Vector<Vector<double>> center_freq_series(num_imfs);  // [K][n_windows]
                    Vector<Vector<double>> energy_series(num_imfs);       // [K][n_windows]
                    Vector<double> convergence_errors(n_windows, 0.0);
                    Vector<int> actual_k_series(n_windows, 0);
                    Vector<String> window_dates;

                    for (int w = 0; w < n_windows; ++w) {
                        int start_idx = w * step;
                        int end_idx = start_idx + rw;
                        Vector<double> window_data(original.begin() + start_idx, original.begin() + end_idx);

                        // 对每个窗口执行 VMD
                        VMD window_vmd;
                        VMD::Config wcfg;
                        wcfg.K = num_imfs;
                        wcfg.alpha = vcfg.alpha;
                        wcfg.tau = vcfg.tau;
                        wcfg.tol = vcfg.tol;
                        wcfg.maxIter = vcfg.maxIter;
                        wcfg.symmetricPad = vcfg.symmetricPad;

                        auto wresult = window_vmd.decompose(window_data, wcfg);

                        // 记录收敛信息
                        convergence_errors[w] = wresult.convergenceError;
                        actual_k_series[w] = wresult.actualK;

                        // 记录每个模态的中心频率和能量
                        for (int k = 0; k < num_imfs && k < (int)wresult.imfs.size(); ++k) {
                            center_freq_series[k].push_back(wresult.centerFreqs[k]);

                            // 计算该窗口内 IMF 的能量占比
                            double energy = 0.0;
                            for (double v : wresult.imfs[k]) {
                                energy += v * v;
                            }
                            double total_energy = 0.0;
                            for (double v : window_data) {
                                total_energy += v * v;
                            }
                            double energy_pct = (total_energy > 1e-10) ? (energy / total_energy * 100.0) : 0.0;
                            energy_series[k].push_back(energy_pct);
                        }

                        // 填充不足的模态（如果 actualK < num_imfs）
                        for (int k = (int)wresult.imfs.size(); k < num_imfs; ++k) {
                            center_freq_series[k].push_back(0.0);
                            energy_series[k].push_back(0.0);
                        }

                        // 记录窗口日期（使用窗口中心点）
                        int center_idx = start_idx + rw / 2;
                        if (center_idx < (int)dates.size()) {
                            window_dates.push_back(dates[center_idx]);
                        }
                    }

                    // 构建输出 JSON
                    nlohmann::json vmd_rolling;
                    vmd_rolling["window"] = rw;
                    vmd_rolling["dates"] = window_dates;

                    // 中心频率轨迹
                    nlohmann::json freq_traj = nlohmann::json::array();
                    for (int k = 0; k < num_imfs; ++k) {
                        freq_traj.push_back(center_freq_series[k]);
                    }
                    vmd_rolling["center_freq_trajectory"] = freq_traj;

                    // 能量占比轨迹
                    nlohmann::json energy_traj = nlohmann::json::array();
                    for (int k = 0; k < num_imfs; ++k) {
                        energy_traj.push_back(energy_series[k]);
                    }
                    vmd_rolling["energy_trajectory"] = energy_traj;

                    // 收敛误差序列
                    vmd_rolling["convergence_errors"] = convergence_errors;

                    // 实际 K 值序列
                    vmd_rolling["actual_k_series"] = actual_k_series;

                    // ─── 稳定性指标 ───
                    nlohmann::json stability;

                    // 1. 中心频率平滑度（相邻窗口变化率的平均值）
                    Vector<double> freq_smoothness(num_imfs, 0.0);
                    for (int k = 0; k < num_imfs; ++k) {
                        double total_change = 0.0;
                        int count = 0;
                        for (size_t i = 1; i < center_freq_series[k].size(); ++i) {
                            double change = std::abs(center_freq_series[k][i] - center_freq_series[k][i-1]);
                            total_change += change;
                            count++;
                        }
                        freq_smoothness[k] = (count > 0) ? (total_change / count) : 0.0;
                    }
                    stability["freq_smoothness"] = freq_smoothness;

                    // 2. 模态间最小频率距离（检测模态混叠）
                    Vector<double> min_freq_distances;
                    for (size_t i = 0; i < window_dates.size(); ++i) {
                        Vector<double> freqs_at_t;
                        for (int k = 0; k < num_imfs; ++k) {
                            if (i < center_freq_series[k].size()) {
                                freqs_at_t.push_back(center_freq_series[k][i]);
                            }
                        }
                        // 排序并计算最小间距
                        std::sort(freqs_at_t.begin(), freqs_at_t.end());
                        double min_dist = 1.0;
                        for (size_t k = 1; k < freqs_at_t.size(); ++k) {
                            double dist = freqs_at_t[k] - freqs_at_t[k-1];
                            if (dist < min_dist) min_dist = dist;
                        }
                        min_freq_distances.push_back(min_dist);
                    }
                    stability["min_freq_distance"] = min_freq_distances;

                    // 3. 频率跳变检测（变化率超过历史均值 3 倍）
                    Vector<double> freq_jump_ratio(num_imfs, 0.0);
                    for (int k = 0; k < num_imfs; ++k) {
                        Vector<double> changes;
                        for (size_t i = 1; i < center_freq_series[k].size(); ++i) {
                            changes.push_back(std::abs(center_freq_series[k][i] - center_freq_series[k][i-1]));
                        }
                        if (changes.empty()) continue;

                        // 计算历史均值和标准差
                        double mean = 0.0;
                        for (double c : changes) mean += c;
                        mean /= changes.size();

                        double std_dev = 0.0;
                        for (double c : changes) {
                            double diff = c - mean;
                            std_dev += diff * diff;
                        }
                        std_dev = std::sqrt(std_dev / changes.size());

                        // 跳变比例 = 超过 mean + 3*std 的次数
                        int jump_count = 0;
                        double threshold = mean + 3.0 * std_dev;
                        for (double c : changes) {
                            if (c > threshold) jump_count++;
                        }
                        freq_jump_ratio[k] = (double)jump_count / changes.size() * 100.0;
                    }
                    stability["freq_jump_ratio"] = freq_jump_ratio;

                    // 4. 整体稳定性评分（0-100，越高越稳定）
                    double smoothness_score = 0.0;
                    for (int k = 0; k < num_imfs; ++k) {
                        // 平滑度越低越好
                        smoothness_score += (1.0 - std::min(freq_smoothness[k] * 100.0, 1.0));
                    }
                    smoothness_score /= num_imfs;

                    double distance_score = 0.0;
                    for (double d : min_freq_distances) {
                        // 距离越大越好（无混叠）
                        distance_score += std::min(d * 10.0, 1.0);
                    }
                    distance_score /= min_freq_distances.size();

                    double jump_score = 0.0;
                    for (int k = 0; k < num_imfs; ++k) {
                        // 跳变比例越低越好
                        jump_score += (1.0 - freq_jump_ratio[k] / 100.0);
                    }
                    jump_score /= num_imfs;

                    stability["overall_score"] = (smoothness_score + distance_score + jump_score) / 3.0 * 100.0;

                    // ─── 能量稳定性指标（新增） ───
                    
                    // 5. 能量占比一阶差分（检测能量突变）
                    Vector<Vector<double>> energy_diff(num_imfs);
                    for (int k = 0; k < num_imfs; ++k) {
                        for (size_t i = 1; i < energy_series[k].size(); ++i) {
                            double diff = std::abs(energy_series[k][i] - energy_series[k][i-1]);
                            energy_diff[k].push_back(diff);
                        }
                    }
                    
                    // 计算每个模态的能量突变程度（平均差分）
                    Vector<double> energy_volatility(num_imfs, 0.0);
                    for (int k = 0; k < num_imfs; ++k) {
                        if (energy_diff[k].empty()) continue;
                        double sum = 0.0;
                        for (double d : energy_diff[k]) sum += d;
                        energy_volatility[k] = sum / energy_diff[k].size();
                    }
                    stability["energy_volatility"] = energy_volatility;
                    
                    // 6. 能量熵（检测能量分布重构）
                    Vector<double> energy_entropy;
                    for (size_t t = 0; t < window_dates.size(); ++t) {
                        // 计算该时刻的能量分布熵
                        double total_energy = 0.0;
                        Vector<double> energy_dist;
                        for (int k = 0; k < num_imfs; ++k) {
                            if (t < energy_series[k].size()) {
                                double e = energy_series[k][t];
                                energy_dist.push_back(e);
                                total_energy += e;
                            }
                        }
                        
                        // 归一化为概率分布
                        if (total_energy > 1e-10) {
                            double entropy = 0.0;
                            for (double e : energy_dist) {
                                double p = e / total_energy;
                                if (p > 1e-10) {
                                    entropy -= p * std::log(p);
                                }
                            }
                            energy_entropy.push_back(entropy);
                        } else {
                            energy_entropy.push_back(0.0);
                        }
                    }
                    stability["energy_entropy"] = energy_entropy;
                    
                    // 7. 能量熵突变检测（熵的一阶差分）
                    Vector<double> entropy_diff;
                    for (size_t i = 1; i < energy_entropy.size(); ++i) {
                        entropy_diff.push_back(std::abs(energy_entropy[i] - energy_entropy[i-1]));
                    }
                    stability["entropy_diff"] = entropy_diff;
                    
                    // 8. 能量熵稳定性评分（熵变化越小越稳定）
                    double entropy_stability = 0.0;
                    if (!entropy_diff.empty()) {
                        double avg_entropy_diff = 0.0;
                        for (double d : entropy_diff) avg_entropy_diff += d;
                        avg_entropy_diff /= entropy_diff.size();
                        // 熵变化 < 0.1 认为稳定
                        entropy_stability = std::max(0.0, 1.0 - avg_entropy_diff * 10.0) * 100.0;
                    }
                    stability["entropy_stability_score"] = entropy_stability;

                    // ─── 模态相似性分析（新增） ───
                    // 计算相邻窗口对应IMF的相关系数 ρ_k(t) = corr(u_k(t), u_k(t-1))
                    
                    // 存储每个窗口所有IMF的时域数据（用于计算相关性）
                    Vector<Vector<Vector<double>>> window_imfs(n_windows);  // [n_windows][K][window_size]
                    
                    // 重新执行VMD以获取IMF时域数据（之前的循环只记录了频率和能量）
                    for (int w = 0; w < n_windows; ++w) {
                        int start_idx = w * step;
                        int end_idx = start_idx + rw;
                        Vector<double> window_data(original.begin() + start_idx, original.begin() + end_idx);
                        
                        VMD window_vmd;
                        VMD::Config wcfg;
                        wcfg.K = num_imfs;
                        wcfg.alpha = vcfg.alpha;
                        wcfg.tau = vcfg.tau;
                        wcfg.tol = vcfg.tol;
                        wcfg.maxIter = vcfg.maxIter;
                        wcfg.symmetricPad = vcfg.symmetricPad;
                        
                        auto wresult = window_vmd.decompose(window_data, wcfg);
                        window_imfs[w] = wresult.imfs;
                    }
                    
                    // 计算相邻窗口对应IMF的相关系数
                    Vector<Vector<double>> imf_correlations(num_imfs);  // [K][n_windows-1]
                    for (int k = 0; k < num_imfs; ++k) {
                        for (int w = 1; w < n_windows; ++w) {
                            // 获取两个相邻窗口的第k个IMF
                            if (w - 1 >= 0 && 
                                k < (int)window_imfs[w].size() && 
                                k < (int)window_imfs[w-1].size()) {
                                
                                const auto& imf_curr = window_imfs[w][k];
                                const auto& imf_prev = window_imfs[w-1][k];
                                
                                // 计算相关系数
                                int n = std::min(imf_curr.size(), imf_prev.size());
                                if (n > 1) {
                                    double mean1 = 0.0, mean2 = 0.0;
                                    for (int i = 0; i < n; ++i) {
                                        mean1 += imf_curr[i];
                                        mean2 += imf_prev[i];
                                    }
                                    mean1 /= n;
                                    mean2 /= n;
                                    
                                    double var1 = 0.0, var2 = 0.0, cov = 0.0;
                                    for (int i = 0; i < n; ++i) {
                                        double d1 = imf_curr[i] - mean1;
                                        double d2 = imf_prev[i] - mean2;
                                        var1 += d1 * d1;
                                        var2 += d2 * d2;
                                        cov += d1 * d2;
                                    }
                                    var1 /= n;
                                    var2 /= n;
                                    cov /= n;
                                    
                                    double corr = 0.0;
                                    if (var1 > 1e-10 && var2 > 1e-10) {
                                        corr = cov / std::sqrt(var1 * var2);
                                    }
                                    imf_correlations[k].push_back(corr);
                                }
                            }
                        }
                    }
                    
                    // 输出模态相似性指标
                    nlohmann::json modal_similarity;
                    
                    // 1. 相关系数序列
                    nlohmann::json corr_series = nlohmann::json::array();
                    for (int k = 0; k < num_imfs; ++k) {
                        corr_series.push_back(imf_correlations[k]);
                    }
                    modal_similarity["correlation_series"] = corr_series;
                    
                    // 2. 平均相关系数（每个IMF的稳定性指标）
                    Vector<double> avg_correlation(num_imfs, 0.0);
                    for (int k = 0; k < num_imfs; ++k) {
                        if (imf_correlations[k].empty()) continue;
                        double sum = 0.0;
                        for (double c : imf_correlations[k]) sum += c;
                        avg_correlation[k] = sum / imf_correlations[k].size();
                    }
                    modal_similarity["avg_correlation"] = avg_correlation;
                    
                    // 3. 相关系数下降检测（突然下降 > 0.3 认为不稳定）
                    Vector<int> correlation_drops(num_imfs, 0);
                    for (int k = 0; k < num_imfs; ++k) {
                        for (size_t i = 1; i < imf_correlations[k].size(); ++i) {
                            double drop = imf_correlations[k][i-1] - imf_correlations[k][i];
                            if (drop > 0.3) {
                                correlation_drops[k]++;
                            }
                        }
                    }
                    modal_similarity["correlation_drops"] = correlation_drops;
                    
                    // 4. 相似性稳定性评分（平均相关系数越高越稳定）
                    double similarity_score = 0.0;
                    for (int k = 0; k < num_imfs; ++k) {
                        similarity_score += std::max(0.0, avg_correlation[k]);
                    }
                    similarity_score /= num_imfs;
                    similarity_score *= 100.0;  // 转换为0-100分
                    modal_similarity["similarity_score"] = similarity_score;
                    
                    stability["modal_similarity"] = modal_similarity;

                    vmd_rolling["stability"] = stability;
                    json["vmd_rolling"] = vmd_rolling;
                }
            }
        } else {
            // 默认 EMD
            auto imfs = simd_emd(original, num_imfs, 10, 0.02);

            nlohmann::json imf_json = nlohmann::json::array();

            Vector<double> residual = original;
            for (size_t i = 0; i < imfs.size(); ++i) {
                imf_json.push_back(imfs[i]);

                // 计算残差
                int sz = static_cast<int>(residual.size());
                for (int j = 0; j < sz; ++j) {
                    residual[j] -= imfs[i][j];
                }

                nlohmann::json info;
                info["index"] = static_cast<int>(i) + 1;
                info["mean_period"] = finance::estimateMeanPeriod(imfs[i]);
                info["energy_pct"] = finance::computeEnergyPct(imfs[i], original);
                info_json.push_back(info);
            }

            json["imf_components"] = imf_json;
            json["residual"] = residual;
            json["imf_info"] = info_json;

            // 重建误差
            Vector<double> recon = residual;
            for (const auto& imf : imfs) {
                int sz = static_cast<int>(recon.size());
                for (int j = 0; j < sz; ++j) {
                    recon[j] += imf[j];
                }
            }
            double rms = 0;
            for (size_t i = 0; i < original.size(); ++i) {
                double d = recon[i] - original[i];
                rms += d * d;
            }
            json["reconstruction_error"] = std::sqrt(rms / original.size());
        }

        // ─── 滚动 EMD 能量分析（信号结构稳定性追踪） ───
        if (rolling_window > 0 && (int)original.size() > rolling_window) {
            int rw = std::min(rolling_window, (int)original.size() / 2);
            auto energy_matrix = finance::computeRollingEMDEnergy(original, rw, num_imfs, dates);

            if (!energy_matrix.empty() && !energy_matrix[0].empty()) {
                int out_len = (int)energy_matrix[0].size();
                nlohmann::json rolling;
                rolling["window"] = rw;
                rolling["dates"] = Vector<String>(
                    dates.begin() + (rw - 1),
                    dates.end()
                );

                // 每个 IMF 的能量占比时间序列
                nlohmann::json by_imf = nlohmann::json::array();
                for (int i = 0; i < num_imfs; ++i) {
                    by_imf.push_back(energy_matrix[i]);
                }
                rolling["by_imf_energy"] = by_imf;
                rolling["residual_energy"] = energy_matrix[num_imfs];

                // 总能量（≈1.0，用于验证）+ 总能量滚动变化率
                Vector<double> total_energy(out_len, 0.0);
                for (int i = 0; i < num_imfs + 1 && i < (int)energy_matrix.size(); ++i) {
                    for (int j = 0; j < out_len; ++j) {
                        total_energy[j] += energy_matrix[i][j];
                    }
                }
                rolling["total_energy"] = total_energy;

                // 总能量变化率（相邻窗口的变化百分比）
                Vector<double> change_rate(out_len, 0.0);
                for (int j = 1; j < out_len; ++j) {
                    if (total_energy[j - 1] > 1e-10) {
                        change_rate[j] = (total_energy[j] - total_energy[j - 1]) / total_energy[j - 1] * 100.0;
                    }
                }
                rolling["change_rate"] = change_rate;

                json["rolling"] = rolling;
            }
        }

        // ─── 最低频 IMF 能量 / 成交量（量价分离信号） ───
        // 找最低频 IMF（mean_period 最大）
        if (!info_json.empty()) {
            int lowest_idx = 0;
            double max_period = 0;
            for (size_t i = 0; i < info_json.size(); ++i) {
                double p = info_json[i].value("mean_period", 0.0);
                if (p > max_period) {
                    max_period = p;
                    lowest_idx = (int)i;
                }
            }

            // 拉取成交量数据
            Vector<double> volume_series;
            try {
                Vector<String> vol_dates;
                auto vol_multi = LoadHistoryData(symbols[0], {"volume"},
                                                  start_date, end_date,
                                                  &vol_dates, fill);
                auto vit = vol_multi.find("volume");
                if (vit != vol_multi.end()) {
                    volume_series = vit->second;
                }
            } catch (...) {}

            // 如果 rolling 已存在，从其中提取最低频 IMF 的能量时间序列
            if (json.contains("rolling") && json["rolling"].contains("by_imf_energy")
                && lowest_idx < (int)json["rolling"]["by_imf_energy"].size()
                && !volume_series.empty()) {

                const auto& energy_series = json["rolling"]["by_imf_energy"][lowest_idx];
                int energy_len = (int)energy_series.size();

                // 成交量标准化（min-max to [0, 1]）
                double vmin = volume_series[0], vmax = volume_series[0];
                for (auto v : volume_series) {
                    if (v < vmin) vmin = v;
                    if (v > vmax) vmax = v;
                }
                double vrange = vmax - vmin;
                Vector<double> vol_norm(volume_series.size(), 0.0);
                if (vrange > 1e-10) {
                    for (size_t i = 0; i < volume_series.size(); ++i) {
                        vol_norm[i] = (volume_series[i] - vmin) / vrange;
                    }
                }

                // 对齐：能量序列对应 window-1..N-1，成交量取 [window-1..N-1]
                int offset = rolling_window - 1;
                int ratio_len = energy_len;
                Vector<double> ratio(ratio_len, 0.0);
                for (int i = 0; i < ratio_len; ++i) {
                    int vol_idx = offset + i;
                    if (vol_idx >= 0 && vol_idx < (int)vol_norm.size() && vol_norm[vol_idx] > 1e-10) {
                        ratio[i] = energy_series[i].get<double>() / vol_norm[vol_idx];
                    }
                }

                nlohmann::json lowest_freq;
                lowest_freq["imf_index"] = lowest_idx;
                lowest_freq["imf_mean_period"] = max_period;
                lowest_freq["energy_series"] = energy_series;
                lowest_freq["volume_normalized"] = Vector<double>(
                    vol_norm.begin() + offset,
                    vol_norm.end()
                );
                lowest_freq["energy_to_volume_ratio"] = ratio;

                json["lowest_freq"] = lowest_freq;
            }
        }

        res.set_content(json.dump(), "application/json");

    } catch (const std::exception& e) {
        FATAL("[SignalHandler] Error: {}", e.what());
        res.status = 500;
        nlohmann::json err;
        err["error"] = e.what();
        res.set_content(err.dump(), "application/json");
    }
}
