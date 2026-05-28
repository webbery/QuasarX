#include "AgentSubSystem.h"
#include "Bridge/exchange.h"
#include "KBarBuilder.h"
#include "Nodes/FunctionNode.h"
#include "Nodes/PortfolioNode.h"
#include "Nodes/QuoteNode.h"
#include "StrategyNode.h"
#include "Util/log.h"
#include "server.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "Bridge/CTP/CTPSymbol.h"
#include "yas/serialize.hpp"
#include "BrokerSubSystem.h"
#include "Nodes/SignalNode.h"
#include <exception>
#include <stdexcept>
#include <typeinfo>
#include "RiskSubSystem.h"
#include "Nodes/ScriptNode.h"
#include "Nodes/ExecuteNode.h"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/HistorySimulationBase.h"

#include "Metric/Return.h"
#include "Metric/Sharp.h"
#include "Metric/Drawdown.h"
#include "Metric/RiskMetric.h"
#include "Metric/Covariance.h"
#include "Metric/MonteCarloSimulator.h"

FlowSubsystem::FlowSubsystem(Server* handle):_handle(handle) {
    auto default_config = handle->GetConfig().GetDefault();
    _stock_working_range = GetWorkingRange(ExchangeName::MT_Beijing);
}

FlowSubsystem::~FlowSubsystem() {
    for (auto& item : _flows) {
        if (item.second._worker) {
            item.second._running = false;
            if (item.second._worker->joinable()) item.second._worker->join();
            delete item.second._worker;
        }
        for (auto node: item.second._graph) {
            delete node;
        }
    }
    _flows.clear();
}

bool FlowSubsystem::LoadFlow(const String& strategy, const List<QNode*>& topo_flow) {
    bool status = true;
    _flows[strategy]._graph = topo_flow;
    return status;
}

void FlowSubsystem::ClearFlow(const String& strategy) {
    for (auto node: _flows[strategy]._graph) {
        delete node;
    }
    _flows[strategy]._graph.clear();
}

void FlowSubsystem::SetShadowMode(const String& strategy) {
    _flows[strategy].isShadowMode = true;
    INFO("[FlowSubsystem] Strategy '{}' configured as Shadow mode", strategy);
}

void FlowSubsystem::Start() {
    auto broker = _handle->GetBrokerSubSystem();
    auto strategySys = _handle->GetStrategySystem();
    for (auto& item : _flows) {
        auto name = item.first;
        auto symbols = strategySys->GetPools(name);
        Start(name, symbols);
    }
}

void FlowSubsystem::Stop(const String& strategy) {
    auto& flow = _flows.at(strategy);
    if (flow._worker) {
        flow._running = false;
        if (flow._worker->joinable()) flow._worker->join();
        delete flow._worker;
    }
    flow._worker = nullptr;
}

run_id_t FlowSubsystem::Start(const String& strategy, const Set<symbol_t>& symbols, double initialCapital) {
    Stop(strategy);

    RuningType mode = _handle->GetRunningMode();
    auto it = _flows.find(strategy);
    if (mode == RuningType::Backtest) {
        return StartBacktest(strategy, symbols, initialCapital);
    } else {
        return StartRealtime(strategy, symbols, initialCapital);
    }
}

run_id_t FlowSubsystem::StartBacktest(const String& strategy, const Set<symbol_t>& symbols, double initialCapital) {
    auto* exchange = dynamic_cast<HistorySimulationBase*>(_handle->GetAvaliableStockExchange());
    if (!exchange) {
        WARN("Failed to get stock exchange for backtest");
        return 0;
    }

    run_id_t runId = exchange->createBacktestContext(strategy, symbols, initialCapital);

    auto& flow = _flows.at(strategy);
    flow._running = true;
    flow._backtestRunId = runId;

    flow._worker = new std::thread([strategy, runId, this]() {
        DataContext context(strategy, _handle);
        context.setBacktestRunId(runId);

        // 设置 warmup epochs 到 context
        int warmupEpochs = _handle->GetStrategySystem()->GetWarmupEpochs(strategy);
        context.SetWarmupEpochs(warmupEpochs);

        if (warmupEpochs > 0) {
            INFO("[Backtest] Warmup period: {} epochs", warmupEpochs);
        }

        auto& flow = _flows[strategy];
        // 获取回测上下文
        auto* exchange = dynamic_cast<HistorySimulationBase*>(_handle->GetAvaliableStockExchange());
        BacktestContext* btContext = exchange ? exchange->getBacktestContext(runId) : nullptr;

        try {
            if (IsUseShareMemory(flow)) {
                context.EnableShareMemory(strategy);
            }
            for (auto node : flow._graph) {
                node->Prepare(strategy, context);
            }

            uint64_t epoch = 0;
            bool success = true;
            auto startTick = std::chrono::high_resolution_clock::now();

            // 使用 stepForward 推进回测时间
            while (flow._running || !Server::IsExit()) {
                context.SetEpoch(++epoch);

                // 推进回测时间
                if (btContext && !exchange->stepForward(btContext)) {
                    // 数据用完
                    INFO("Backtest data finished for strategy {}", strategy);
                    break;
                }
                if (!RunGraph(strategy, flow, context)) {
                    success = false;
                    break;
                }
            }

            auto endTick = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(endTick - startTick);

            if (success) {
                // 统计指标
                ExecuteNode* endNode = nullptr;
                for (auto node: flow._graph) {
                    auto n = dynamic_cast<ExecuteNode*>(node);
                    if (n) {
                        endNode = n;
                        break;
                    }
                }
                if (endNode) {
                    // 使用 BacktestContext 中的每日快照计算指标（替代 build_portfolio_values）
                    // 性能优化：避免重建组合价值，直接使用 stepForward 中记录的快照
                    Vector<double> portfolio_values;

                    if (btContext && btContext->dailySnapshotCount() > 0) {
                        // 直接 move 快照数据（SoA 布局，零拷贝）
                        portfolio_values = btContext->takePortfolioValues();
                        INFO("[Backtest] Using BacktestContext snapshots: {} days", portfolio_values.size());
                    } else {
                        // 降级：使用原有的 build_portfolio_values
                        auto& cash_flow = endNode->GetReports();
                        auto [pv, cf] = build_portfolio_values(cash_flow, context, _handle);
                        portfolio_values.assign(pv.begin(), pv.end());
                        INFO("[Backtest] Fallback to build_portfolio_values: {} days", portfolio_values.size());
                    }

                    auto daily_returns = simple_daily_return(portfolio_values);
                    auto total_return = simple_total_return(portfolio_values, context.getInitialCapital());
                    flow._collections[StatisticIndicator::TotalReturn] = (float)total_return;

                    // 将日收益率和日期存回 BacktestContext，供 BackTestHandler 返回给前端
                    // 注意：daily_returns 后面还要用，所以拷贝而非 move
                    // 先保存 dates（避免 takeDates move 后导致 dailySnapshotCount 为 0）
                    if (btContext && btContext->dailySnapshotCount() > 0) {
                        auto datesCopy = btContext->getDates();  // 拷贝日期
                        btContext->setDailyReturns(std::move(datesCopy), Vector<double>(daily_returns.begin(), daily_returns.end()));
                    }

                    // 计算年化收益率
                    int count = static_cast<int>(daily_returns.size());
                    auto annual_return = compute_annualized_return(total_return, count);
                    flow._collections[StatisticIndicator::AnualReturn] = annual_return;

                    double annual_vol = compute_annualized_volatility(daily_returns);

                    // 计算夏普比率
                    double risk_free_rate = 0.0;  // 无风险利率，可改为从配置读取
                    float sharp = compute_sharp_ratio(annual_return, annual_vol, risk_free_rate);
                    flow._collections[StatisticIndicator::Sharp] = sharp;

                    auto max_dd = max_drawdown_ratio(portfolio_values);
                    flow._collections[StatisticIndicator::MaxDrawDown] = max_dd;
                    flow._collections[StatisticIndicator::WinRate] = win_rate(daily_returns);
                    flow._collections[StatisticIndicator::Calmar] = calmar_ratio(annual_return, max_dd);

                    // 样本外拟合能力 R²
                    flow._collections[StatisticIndicator::R2] = static_cast<float>(compute_r_squared(portfolio_values));

                    // 尾部风险指标
                    flow._collections[StatisticIndicator::VaR] = compute_var(daily_returns, 0.95);
                    flow._collections[StatisticIndicator::ES]  = compute_cvar(daily_returns, 0.95);

                    // MonteCarlo Bootstrap 风险分析
                    auto* exchange = dynamic_cast<HistorySimulationBase*>(_handle->GetAvaliableStockExchange());
                    TradingMode trading_mode = exchange ? exchange->GetTradingMode() : TradingMode::T1;
                    int bars_per_year = (trading_mode == TradingMode::T1) ? 252 : 60480;

                    McSimConfig mcConfig;
                    mcConfig.trading_mode = trading_mode;  // 元信息，不改变抽样逻辑
                    mcConfig.bars_per_year = bars_per_year;
                    mcConfig.n_simulations = 20000;

                    MonteCarloSimulator mc;
                    mc.Init(mcConfig);
                    mc.FeedReturns(std::vector<double>(daily_returns.begin(), daily_returns.end()));
                    auto mcResult = mc.Run();

                    flow._collections[StatisticIndicator::BootRuinProb50]       = mcResult.ruin_prob_high;
                    flow._collections[StatisticIndicator::BootRuinProb30]       = mcResult.ruin_prob_low;
                    flow._collections[StatisticIndicator::BootReturnP5]         = mcResult.return_p5;
                    flow._collections[StatisticIndicator::BootReturnP50]        = mcResult.return_p50;
                    flow._collections[StatisticIndicator::BootReturnP95]        = mcResult.return_p95;
                    flow._collections[StatisticIndicator::BootMaxDDP50]         = mcResult.max_dd_p50;
                    flow._collections[StatisticIndicator::BootMaxDDP95]         = mcResult.max_dd_p95;
                    flow._collections[StatisticIndicator::BootMedianAnnualRet]  = mcResult.median_annual_ret;
                    flow._collections[StatisticIndicator::BootTail1PctAvgDD]    = mcResult.tail_1pct_avg_dd;
                    flow._collections[StatisticIndicator::BootMethod]           = static_cast<float>(mcResult.method);
                    flow._collections[StatisticIndicator::BootBlockSize]        = static_cast<float>(mcResult.block_size);
                    flow._collections[StatisticIndicator::BootAutocorrelation]  = mcResult.autocorrelation;
                    flow._collections[StatisticIndicator::BootNSimulations]     = static_cast<float>(mcResult.n_simulations);
                    // 压力测试（基础）
                    flow._collections[StatisticIndicator::BootStressRuinProb50] = mcResult.stress_ruin_prob_high;
                    flow._collections[StatisticIndicator::BootStressRuinProb30] = mcResult.stress_ruin_prob_low;
                    flow._collections[StatisticIndicator::BootStressReturnP5]   = mcResult.stress_return_p5;
                    flow._collections[StatisticIndicator::BootStressReturnP50]  = mcResult.stress_return_p50;
                    flow._collections[StatisticIndicator::BootStressMaxDDP50]   = mcResult.stress_max_dd_p50;
                    // 流动性压力测试
                    flow._collections[StatisticIndicator::BootLiqStressRuinProb50] = mcResult.liq_stress_ruin_prob_high;
                    flow._collections[StatisticIndicator::BootLiqStressReturnP5]   = mcResult.liq_stress_return_p5;
                    flow._collections[StatisticIndicator::BootLiqStressMaxDDP50]   = mcResult.liq_stress_max_dd_p50;
                    // 波动率聚集压力测试
                    flow._collections[StatisticIndicator::BootVolClusterStressRuinProb50] = mcResult.vol_cluster_stress_ruin_prob_high;
                    flow._collections[StatisticIndicator::BootVolClusterStressReturnP5]   = mcResult.vol_cluster_stress_return_p5;
                    flow._collections[StatisticIndicator::BootVolClusterStressMaxDDP50]   = mcResult.vol_cluster_stress_max_dd_p50;

                    // 输出极端场景信息到日志
                    INFO("MonteCarlo Analysis:\n{}", mcResult.toString());

                    // 保存路径数据供前端可视化
                    auto& mcPaths = flow._mcPaths;
                    // 最差路径
                    for (const auto& p : mcResult.worst_paths) {
                        FlowSubsystem::McPathDetail d;
                        d.total_return = p.total_return;
                        d.max_drawdown = p.max_drawdown;
                        d.win_rate = p.win_rate;
                        d.longest_win_streak = p.longest_win_streak;
                        d.longest_loss_streak = p.longest_loss_streak;
                        d.max_dd_bar_index = p.max_dd_bar_index;
                        d.vol_ratio = p.vol_ratio;
                        d.equity_curve.assign(p.equity_curve.begin(), p.equity_curve.end());
                        mcPaths.worst_paths.push_back(d);
                    }
                    // 最好路径
                    for (const auto& p : mcResult.best_paths) {
                        FlowSubsystem::McPathDetail d;
                        d.total_return = p.total_return;
                        d.max_drawdown = p.max_drawdown;
                        d.win_rate = p.win_rate;
                        d.longest_win_streak = p.longest_win_streak;
                        d.longest_loss_streak = p.longest_loss_streak;
                        d.max_dd_bar_index = p.max_dd_bar_index;
                        d.vol_ratio = p.vol_ratio;
                        d.equity_curve.assign(p.equity_curve.begin(), p.equity_curve.end());
                        mcPaths.best_paths.push_back(d);
                    }
                    // 基准路径
                    auto convertPathDetail = [](const PathDetail& p) -> FlowSubsystem::McPathDetail {
                        FlowSubsystem::McPathDetail d;
                        d.total_return = p.total_return;
                        d.max_drawdown = p.max_drawdown;
                        d.win_rate = p.win_rate;
                        d.longest_win_streak = p.longest_win_streak;
                        d.longest_loss_streak = p.longest_loss_streak;
                        d.max_dd_bar_index = p.max_dd_bar_index;
                        d.vol_ratio = p.vol_ratio;
                        d.equity_curve.assign(p.equity_curve.begin(), p.equity_curve.end());
                        return d;
                    };
                    mcPaths.median_path = convertPathDetail(mcResult.median_path);
                    mcPaths.p10_path = convertPathDetail(mcResult.p10_path);
                    mcPaths.p90_path = convertPathDetail(mcResult.p90_path);

                    // 多资产协方差诊断（仅资产数 > 1 时计算）
                    if (btContext && btContext->assetSnapshotCount() > 0) {
                        const auto& symbols = btContext->getSymbols();
                        int nAssets = static_cast<int>(symbols.size());
                        if (nAssets > 1) {
                            const auto& assetDates = btContext->getAssetSnapshotDates();
                            const auto& assetValues = btContext->getAssetValues();
                            int T = static_cast<int>(assetDates.size());

                            // 构建每个资产的日收益率序列
                            std::vector<std::vector<double>> assetReturns(nAssets);
                            for (int j = 0; j < nAssets; j++) {
                                auto it = symbols.begin();
                                std::advance(it, j);
                                symbol_t sym = *it;
                                assetReturns[j].reserve(T - 1);

                                for (int i = 1; i < T; i++) {
                                    double prevVal = assetValues[i - 1].count(sym) ? assetValues[i - 1].at(sym) : 0.0;
                                    double currVal = assetValues[i].count(sym) ? assetValues[i].at(sym) : 0.0;
                                    if (prevVal > 0.0) {
                                        assetReturns[j].push_back((currVal - prevVal) / prevVal);
                                    } else {
                                        assetReturns[j].push_back(0.0);
                                    }
                                }
                            }

                            // 计算协方差和质量评估
                            auto cov = compute_covariance(assetReturns);
                            if (cov.n_assets > 1) {
                                auto quality = evaluate_covariance_quality(cov);
                                flow._collections[StatisticIndicator::CovConditionNumber] = static_cast<float>(quality.conditionNumber);
                                flow._collections[StatisticIndicator::CovMinCorr] = static_cast<float>(cov.minCorrelation);
                                flow._collections[StatisticIndicator::CovMaxCorr] = static_cast<float>(cov.maxCorrelation);
                                flow._collections[StatisticIndicator::CovPositiveDefinite] = quality.isPositiveDefinite ? 1.0f : 0.0f;
                                flow._collections[StatisticIndicator::CovObservations] = static_cast<float>(cov.n_observations);
                                flow._collections[StatisticIndicator::CovNAzets] = static_cast<float>(cov.n_assets);
                                flow._collections[StatisticIndicator::CovNearCollinear] = static_cast<float>(cov.nearCollinearPairs);
                                INFO("[Backtest] Covariance: assets={}, obs={}, κ={:.1f}, grade={}, positive_definite={}",
                                     cov.n_assets, cov.n_observations, quality.conditionNumber, quality.gradeString(), quality.isPositiveDefinite);
                            }
                        }
                    }

                    // 输出极端场景信息到日志（已在上面通过 mcResult.toString() 输出）
                }
                for (auto node : flow._graph) {
                    node->Done(strategy);
                }
            }

            // 回测结束后登出
            if (_handle->GetRunningMode() == RuningType::Backtest) {
                auto broker = _handle->GetAvaliableStockExchange();
                broker->Logout();
            }

            auto info = fmt::format("backtest finish, cost {}s, {}ms/per datum", duration.count(), (epoch == 0? 0:duration.count()*1000.0/epoch));
            strategy_log(strategy, info);
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
        }

        // 清理回测上下文前，提取每日收益数据供 BackTestHandler 使用
        INFO("[Backtest] Extracting daily returns: snapshotCount={}, returnsSize={}",
             btContext ? btContext->dailySnapshotCount() : 0,
             btContext ? btContext->getDailyReturns().size() : 0);
        if (btContext && btContext->dailySnapshotCount() > 0 && btContext->getDailyReturns().size() > 0) {
            flow._returnDates = btContext->getReturnDates();
            flow._dailyReturns = btContext->getDailyReturns();
            INFO("[Backtest] Extracted {} daily returns to flow", flow._dailyReturns.size());
        } else {
            WARN("[Backtest] No daily returns to extract from context");
        }

        // 清理回测上下文
        if (exchange) {
            exchange->destroyBacktestContext(runId);
        }
        flow._running = false;
    });
    return runId;
}

/**
 * @brief 启动实盘策略（K-bar 聚合驱动）
 *
 * 流程：
 * 1. 订阅 NNG 原始行情
 * 2. 逐 tick 送入 KBarBuilder 聚合
 * 3. 当多标的 bar 对齐时，拉动策略图执行
 */
run_id_t FlowSubsystem::StartRealtime(const String& strategy, const Set<symbol_t>& symbols, double initialCapital) {
    auto& flow = _flows.at(strategy);
    flow._running = true;

    // 从 QuoteInputNode 配置中读取频率
    BarFreq freq = BarFreq::Day;
    for (auto* node : flow._graph) {
        if (auto* qn = dynamic_cast<QuoteInputNode*>(node)) {
            // 默认日线频率，后续可从节点配置读取
            break;
        }
    }

    auto kbarBuilder = std::make_shared<KBarBuilder>(freq, 5);
    kbarBuilder->SetSymbols(symbols);
    flow._kbarBuilder = kbarBuilder;

    bool shadowMode = flow.isShadowMode;
    INFO("[Realtime] KBarBuilder: freq={}, symbols={}, tolerance=5s, shadow={}",
         KBarBuilder::FreqToString(freq), symbols.size(), shadowMode);

    flow._worker = new std::thread([strategy, symbols, kbarBuilder, shadowMode, this]() {
        // ★ 影子模式：临时设置全局运行模式（仅在当前 worker 线程生命周期内有效）
        RuningType originalMode = _handle->GetRunningMode();
        if (shadowMode) {
            _handle->SetRunningMode(RuningType::Shadow);
            INFO("[Realtime] Shadow mode activated for strategy '{}'", strategy);
        }

        DataContext context(strategy, _handle);
        auto& flow = _flows[strategy];

        // 订阅原始行情
        nng_socket recvSock;
        if (!Subscribe(URI_RAW_QUOTE, recvSock)) {
            WARN("[Realtime] Failed to subscribe to raw quote channel");
            flow._running = false;
            return;
        }

        try {
            // Prepare 阶段
            for (auto node : flow._graph) {
                node->Prepare(strategy, context);
            }

            Map<symbol_t, QuoteInfo> snapshot;
            uint64_t epoch = 0;

            while (flow._running || !Server::IsExit()) {
                // 阻塞读取 tick（Subscribe 默认 5s 超时）
                QuoteInfo tick;
                if (!ReadQuote(recvSock, tick)) continue;

                // 只处理策略涉及的标的
                if (!symbols.count(tick._symbol)) continue;

                // 聚合 tick
                kbarBuilder->OnTick(tick);

                // 拉动检查：是否有新的 bar 对齐快照
                if (!kbarBuilder->GetSnapshot(snapshot)) continue;

                // 将快照写入 context
                context.SetEpoch(++epoch);
                for (auto& [symbol, quote] : snapshot) {
                    context.SetQuote(symbol, quote);
                }
                time_t barTime = snapshot.begin()->second._time;
                context.SetTime(barTime);

                // 执行策略图
                if (!RunGraph(strategy, flow, context)) {
                    WARN("[Realtime] RunGraph failed for strategy {}", strategy);
                    break;
                }
            }
        } catch (const std::invalid_argument& e) {
            WARN("invalid argument error: {}", e.what());
        }

        nng_close(recvSock);
        flow._running = false;

        // ★ 恢复原始运行模式
        if (shadowMode) {
            _handle->SetRunningMode(originalMode);
            INFO("[Realtime] Shadow mode deactivated, restored to {}", (int)originalMode);
        }
    });

    return 0;  // 实盘无 runId
}

bool FlowSubsystem::IsUseShareMemory(const StrategyFlowInfo& flow) {
    auto& graph = flow._graph;
    for (auto node: graph) {
        if (dynamic_cast<ScriptNode*>(node)) {
            return true;
        }
    }
    return false;
}

bool FlowSubsystem::IsRunning(const String& strategy) const {
    auto itr = _flows.find(strategy);
    if (itr == _flows.end()) {
        return false;
    }
    const auto& flow = itr->second;
    if (!flow._running.load()) {
        if (flow._worker && flow._worker->joinable()) {
            flow._worker->join();
            return false;
        }
    }
    return true;
}

Set<symbol_t> FlowSubsystem::GetPools(const String& strategy) {
    auto& flow = _flows[strategy];
    for (auto& node: flow._graph) {
        if (typeid(*node) == typeid(SignalNode)) {
            auto p = (SignalNode*)node;
            return p->GetPool();
        }
    }
    return Set<symbol_t>();
}

FlowSubsystem::BacktestDailyReturns FlowSubsystem::GetBacktestDailyReturns(const String& strategy) const {
    auto it = _flows.find(strategy);
    if (it == _flows.end()) {
        return {};
    }
    return { it->second._returnDates, it->second._dailyReturns };
}

FlowSubsystem::BacktestMcPaths FlowSubsystem::GetBacktestMcPaths(const String& strategy) const {
    auto it = _flows.find(strategy);
    if (it == _flows.end()) {
        return {};
    }
    return it->second._mcPaths;
}

bool FlowSubsystem::RunGraph(const String& strategy, const StrategyFlowInfo& flow, DataContext& context) {
    bool shouldSkipEpoch = false;

    // 回测模式下，预热期内跳过 Signal、Execution、Portfolio 节点
    bool inWarmup = context.IsInWarmup();

    // 根据策略图生成信号
    for (auto node: flow._graph) {
        // 跳过预热期的信号、执行和投资组合节点
        if (inWarmup) {
            if (dynamic_cast<SignalNode*>(node) || 
                dynamic_cast<ExecuteNode*>(node) || 
                dynamic_cast<PortfolioNode*>(node)) {
                continue;
            }
        }
        auto result = node->Process(strategy, context);
        
        switch (result) {
            case NodeProcessResult::Success:
                // 继续下一个节点
                break;
                
            case NodeProcessResult::Skip:
                // 时间不对齐等场景，跳过本轮
                shouldSkipEpoch = true;
                break;
                
            case NodeProcessResult::Finished:
                // 回测模式下，QuoteInputNode 数据正常结束
                if (_handle->GetRunningMode() == RuningType::Backtest) {
                    if (auto quoteNode = dynamic_cast<QuoteInputNode*>(node)) {
                        INFO("{} data finished, backtest completed normally", node->id());
                        context.SetEpoch(0);
                        return true;
                    }
                }
                // 其他情况视为错误
                INFO("{} process finished unexpectedly", node->id());
                return false;
                
            case NodeProcessResult::Error:
                INFO("{} process failed with error", node->id());
                return false;
        }
        
        // 如果标记为跳过，提前终止后续节点执行
        if (shouldSkipEpoch) break;
    }
    
    if (shouldSkipEpoch) {
        return true;  // 返回 true 继续运行，但不执行风控
    }
    
    // 仅在本轮未跳过时执行风控检查
    auto risk = _handle->GetRiskSubSystem();
    if (risk) {
        //risk->Metric(context);
    }
    return true;
}

void FlowSubsystem::RegistIndicator(const String& strategy) {
    auto broker = _handle->GetBrokerSubSystem();
    broker->CleanAllIndicators(strategy);
    broker->RegistIndicator(strategy, StatisticIndicator::VaR);
    broker->RegistIndicator(strategy, StatisticIndicator::Sharp);
}

bool FlowSubsystem::ImmediatelyBuy(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._time = Now();
    order._side = 0;
    order._type = type;
    order._volume = 0;
    order._price = price;
    TradeInfo dd;
    auto id = broker->Buy(strategy, symbol, order, dd);
    if (id._id != 0) {
        LOG("buy order: {}, result: {}", order, dd);
        return true;
    }
    return false;
}

bool FlowSubsystem::ImmediatelySell(const String& strategy, symbol_t symbol, double price, OrderType type) {
    auto broker = _handle->GetBrokerSubSystem();
    Order order;
    order._volume = 0;
    order._side = true;
    order._price = price;
    TradeInfo dd;
    auto id = broker->Sell(strategy, symbol, order, dd);
    return true;
}

// bool FlowSubsystem::GenerateSignal(symbol_t symbol, const DataFeatures& features) {
//     float vwap = -1;
    // for (int i = 0; i < features._data.size(); ++i) {
    //     if (features._names[i] == VWAPFeature::name()) {
    //         vwap = std::get<double>(features._data[i]);
    //         break;
    //     }
    // }
    // return features._price < vwap || IsNearClose(symbol);
// }

// bool FlowSubsystem::DailyBuy(const String& strategy, symbol_t symbol, const DataFeatures& features) {
    // if (_handle->GetRunningMode() == RuningType::Backtest) {
    //     // 如果是天级数据,则使用收盘价
    //     return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
    // }
    // else {
    //     // TODO: 分批多次入场

    //     if (GenerateSignal(symbol, features)) {
    //         return ImmediatelyBuy(strategy, symbol, features._price, OrderType::Market);
    //     }
    // }
    // return false;
// }

// bool FlowSubsystem::DailySell(const String& strategy, symbol_t symbol, const DataFeatures& features)
// {
//     if (_handle->GetRunningMode() == RuningType::Backtest) {
//         // return ImmediatelySell(strategy, symbol, features._price, OrderType::Market);
//     }
//     return StrategySell(strategy, symbol, features);
// }

// bool FlowSubsystem::StrategySell(const String& strategyName, symbol_t symbol, const DataFeatures& features) {
    // float vwap = std::numeric_limits<float>::max();
    // for (int i = 0; i < features._data.size(); ++i) {
    //     if (features._features[i] == std::hash<StringView>()(VWAPFeature::name())) {
    //         vwap = features._data[i];
    //         break;
    //     }
    // }
    // if (features._price > vwap || IsNearClose(symbol)) {
    //     return ImmediatelySell(strategyName, symbol, features._price, OrderType::Market);
    // }
//     return false;
// }

bool FlowSubsystem::IsNearClose(symbol_t symb) {
    auto current = Now();
    auto name = GetExchangeName(get_symbol(symb));
    auto final_time = *_stock_working_range.rbegin();
    if (current == final_time && final_time.near_end(current)) {
        INFO("force buy because near trade end.");
        return true;
    }
    return false;
}

const Map<StatisticIndicator, std::variant<float, List<float>>>& FlowSubsystem::GetCollection(const String& strategy) const {
    auto itr = _flows.find(strategy);
    if (itr == _flows.end()) {
        WARN("strategy {} not exist", strategy);
    }
    // 等待对应线程完成
    auto& flow = itr->second;
    if (flow._worker && flow._worker->joinable()) {
        flow._worker->join();
    }
    return flow._collections;
}

// void FlowSubsystem::Create(const String& strategy, SignalGeneratorType type, const nlohmann::json& params) {
//     if (_flows.count(strategy)) {
//         return;
//     }
//     // 默认为模拟盘
//     auto& pipeline = _flows[strategy];
//     switch (type) {
//     case SignalGeneratorType::XGBoost:
//         // pipeline._agent = new XGBoostAgent(params, "");
//     break;
//     default:
//     break;
//     }
// }
