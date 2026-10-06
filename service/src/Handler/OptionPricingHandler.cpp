#include "Handler/OptionPricingHandler.h"
#include "Derivative/OptionPricer.h"
#include "Derivative/IVSurface.h"
#include "Derivative/OptionContractFilter.h"
#include "Util/OptionDataDB.h"
#include "Util/QuoteDB.h"
#include "Util/data.h"
#include "Util/system.h"
#include "Util/finance.h"
#include "Util/HolidayCalendar.h"
#include "Bridge/OptionSymbolMacros.h"
#include "server.h"
#include <chrono>
#include <regex>

using namespace std::chrono;

// ── 工具: 从 contract_name 解析到期年月 ──
// 格式: "IO2401-C-3800", "50ETF2401C3200", "10007187" (SSE 8位合约码)
// 返回 {year, month}，失败返回 {0, 0}
static std::pair<int, int> parseExpiryFromName(const String& contract_name, const String& product) {
    // 尝试匹配 YYMM 模式: 产品名 + 4~6位数字
    std::regex re("(\\d{2})(\\d{2})");
    // 跳过产品前缀，找年月
    auto pos = contract_name.find_first_of("0123456789");
    if (pos == String::npos) return {0, 0};
    String digit_part = contract_name.substr(pos);
    std::smatch m;
    if (std::regex_search(digit_part, m, re)) {
        int year = std::stoi(m[1].str());
        int month = std::stoi(m[2].str());
        if (month >= 1 && month <= 12) return {year + 2000, month};
    }
    return {0, 0};
}

// ── 工具: 计算到期天数 ──
static int daysToExpiry(int expiry_year, int expiry_month, int trade_year, int trade_month, int trade_day,
                        const String& exchange) {
    auto trade = sys_days{year_month_day{year{trade_year}, month{static_cast<unsigned>(trade_month)}, day{static_cast<unsigned>(trade_day)}}};
    auto rule = finance::exerciseRuleForExchange(exchange);
    auto& cal = HolidayCalendar::instance();
    auto ed = finance::computeExerciseDate(expiry_year, expiry_month, rule, cal);
    auto expiry = sys_days{year_month_day{year{ed.year}, month{static_cast<unsigned>(ed.month)}, day{static_cast<unsigned>(ed.day)}}};
    long days = (expiry - trade).count();
    return static_cast<int>(std::max(days, 1L));
}

// ═══════════════════════════════════════════════════════════
//  POST: /v0/option/pricing + /v0/option/pricing/multi
// ═══════════════════════════════════════════════════════════

void OptionPricingHandler::post(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);

        // 判断是单合约还是多合约
        bool is_multi = req.path == "/v0/option/pricing_multi";

        double spot = body.value("spot", 0.0);
        double r = body.value("risk_free_rate", 0.015);
        double q = body.value("dividend_yield", 0.0);
        String method = body.value("method", "black_scholes");
        int n_paths = body.value("n_paths", 100000);
        int n_steps = body.value("n_steps", 252);

        auto priceToJson = [](const PricingResult& pr, const String& m) {
            nlohmann::json j;
            j["price"] = pr.price;
            j["intrinsic_value"] = pr.intrinsic_value;
            j["time_value"] = pr.time_value;
            j["lower_bound"] = pr.lower_bound;  // 理论下限
            j["moneyness"] = pr.moneyness;
            j["greeks"] = {
                {"delta", pr.delta}, {"gamma", pr.gamma},
                {"theta", pr.theta}, {"vega", pr.vega}, {"rho", pr.rho}
            };
            // payoff curve
            nlohmann::json pc = nlohmann::json::array();
            for (auto& p : pr.payoff_curve) {
                pc.push_back({{"spot", p.spot}, {"payoff_at_expiry", p.payoff_at_expiry}, {"payoff_now", p.payoff_now}});
            }
            j["payoff_curve"] = std::move(pc);
            if (m == "monte_carlo") {
                j["mc_std_error"] = pr.mc_std_error;
            }
            if (m == "binomial") {
                j["early_exercise_premium"] = pr.early_exercise_premium;
            }
            return j;
        };

        if (is_multi) {
            auto contracts = body["contracts"].get<std::vector<nlohmann::json>>();
            nlohmann::json results = nlohmann::json::array();
            for (auto& c : contracts) {
                double K = c.value("strike", 0.0);
                bool is_call = c.value("is_call", true);
                String expiry_str = c.value("expiry", "");
                double sigma = c.value("volatility", body.value("volatility", 0.2));
                double T = c.value("T", body.value("T", 0.0));
                if (T <= 0 && !expiry_str.empty()) {
                    // 从 expiry 字符串计算 T
                    auto exp_date = system_clock::from_time_t(0);
                    // 简单解析 YYYY-MM-DD
                    int ey, em, ed;
                    if (sscanf(expiry_str.c_str(), "%d-%d-%d", &ey, &em, &ed) == 3) {
                        auto exp_tp = sys_days{year_month_day{year{ey}, month{static_cast<unsigned>(em)}, day{static_cast<unsigned>(ed)}}};
                        auto today = floor<days>(system_clock::now());
                        T = std::max<long long>((exp_tp - today).count(), 1LL) / 365.0;
                    }
                }
                bool is_american = c.value("is_american", false);
                auto pr = OptionPricer::price(method, spot, K, T, sigma, r, q, is_call, is_american, n_paths, n_steps);
                auto j = priceToJson(pr, method);
                j["strike"] = K;
                j["is_call"] = is_call;
                results.push_back(std::move(j));
            }
            res.set_content(results.dump(), "application/json");
        } else {
            // 单合约
            double K = body.value("strike", 0.0);
            bool is_call = body.value("is_call", true);
            double sigma = body.value("volatility", 0.2);
            double T = body.value("T", 0.0);
            String expiry_str = body.value("expiry", "");
            bool is_american = body.value("is_american", false);

            if (T <= 0 && !expiry_str.empty()) {
                int ey, em, ed;
                if (sscanf(expiry_str.c_str(), "%d-%d-%d", &ey, &em, &ed) == 3) {
                    auto exp_tp = sys_days{year_month_day{year{ey}, month{static_cast<unsigned>(em)}, day{static_cast<unsigned>(ed)}}};
                    auto today = floor<days>(system_clock::now());
                    T = std::max<long long>((exp_tp - today).count(), 1LL) / 365.0;
                }
            }

            auto pr = OptionPricer::price(method, spot, K, T, sigma, r, q, is_call, is_american, n_paths, n_steps);
            auto j = priceToJson(pr, method);
            res.set_content(j.dump(), "application/json");
        }
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["error"] = e.what();
        res.status = 400;
        res.set_content(err.dump(), "application/json");
    }
}

// ═══════════════════════════════════════════════════════════
//  GET: /v0/option/iv_surface
// ═══════════════════════════════════════════════════════════

void OptionPricingHandler::get(const httplib::Request& req, httplib::Response& res) {
    try {
        String exchange = req.get_param_value("exchange");
        String product = req.get_param_value("product");

        WARN("[IVSurface] Request: exchange={}, product={}", exchange, product);

        if (exchange.empty() || product.empty()) {
            nlohmann::json err;
            err["error"] = "exchange and product parameters required";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        auto& db = OptionDataDB::instance();
        if (!db.isInitialized()) {
            // 懒加载：尝试初始化 OptionDataDB
            String db_path = _server->GetConfig().GetDatabasePath();
            if (!db.init(db_path + "/option", "option.db")) {
                nlohmann::json err;
                err["error"] = "OptionDataDB initialization failed";
                res.status = 500;
                res.set_content(err.dump(), "application/json");
                return;
            }
        }

        // 查询该产品最新日期的所有合约 (扩展字段用于异常报价过滤)
        // 新增 symbol_id 用于解码到期年月
        String sql = fmt::format(
            "SELECT symbol_id, contract_name, call_put, strike_price, implied_volatility, "
            "       trade_date, underlying, close, settlement, open, high, low, "
            "       volume, turnover, open_interest "
            "FROM option_daily "
            "WHERE exchange = '{}' AND product = '{}' "
            "  AND trade_date = (SELECT MAX(trade_date) FROM option_daily "
            "                    WHERE exchange = '{}' AND product = '{}') "
            "ORDER BY contract_name",
            exchange, product, exchange, product);

        // 获取标的现货价格 (用于从 close 反算 IV)
        String underlying_code;
        double spot_price = 0.0;
        {
            String spot_sql = fmt::format(
                "SELECT DISTINCT underlying FROM option_daily "
                "WHERE exchange = '{}' AND product = '{}' AND underlying IS NOT NULL "
                "AND underlying != '' LIMIT 1",
                exchange, product);
            db.query(spot_sql, [&](duckdb_result& result) -> bool {
                if (duckdb_row_count(&result) > 0)
                    underlying_code = duckdb_value_varchar(&result, 0, 0);
                return true;
            });
        }
        if (!underlying_code.empty()) {
            spot_price = ResolveUnderlying(underlying_code, exchange, product).latest_close;
        }

        double risk_free_rate = 0.015;

        // 解析 TIMESTAMP 字符串 (形如 "2024-06-28 00:00:00") 为三元组
        auto parseYMD = [](const char* s, int& y, int& m, int& d) -> bool {
            if (!s) return false;
            return sscanf(s, "%d-%d-%d", &y, &m, &d) == 3;
        };

        // 构建 OptionContractView 列表 (用于过滤器)
        Vector<OptionContractView> contracts;
        int skip_strike = 0, skip_date = 0, skip_expiry = 0, skip_iv = 0;

        bool ok = db.query(sql, [&](duckdb_result& result) -> bool {
            idx_t row_count = duckdb_row_count(&result);
            WARN("[IVSurface] Query returned {} rows", row_count);
            for (idx_t i = 0; i < row_count; ++i) {
                OptionContractView c;
                // symbol_id (idx 0) 用于解码到期年月
                int64_t symbol_id = duckdb_value_int64(&result, 0, i);
                c.contract_name = duckdb_value_varchar(&result, 1, i);
                c.opt_type = toOptionType(duckdb_value_varchar(&result, 2, i));
                c.strike = duckdb_value_double(&result, 3, i);
                c.iv = duckdb_value_double(&result, 4, i);
                // trade_date (idx 5) 作为 today 基准
                c.close = duckdb_value_double(&result, 7, i);
                c.settlement = duckdb_value_double(&result, 8, i);
                c.open = duckdb_value_double(&result, 9, i);
                c.high = duckdb_value_double(&result, 10, i);
                c.low = duckdb_value_double(&result, 11, i);
                c.volume = duckdb_value_int64(&result, 12, i);
                c.turnover = duckdb_value_int64(&result, 13, i);
                c.open_interest = duckdb_value_int64(&result, 14, i);
                c.spot = spot_price;
                c.risk_free_rate = risk_free_rate;

                if (c.strike <= 0) { ++skip_strike; continue; }

                int ty = 0, tm = 0, td = 0;
                if (!parseYMD(duckdb_value_varchar(&result, 5, i), ty, tm, td)) { ++skip_date; continue; }

                // 从 symbol_id 解码到期年月 (symbol_t 位域: _year 6bits, _month 4bits)
                symbol_t sym;
                std::memcpy(&sym, &symbol_id, sizeof(symbol_t));
                int ey = 2000 + sym._year;
                int em = sym._month;
                if (ey == 2000 || em < 1 || em > 12) { ++skip_expiry; continue; }

                int expiry_days = daysToExpiry(ey, em, ty, tm, td, exchange);

                // IV 缺失时从 close 价格反算
                if (c.iv <= 0 && c.close > 0 && spot_price > 0) {
                    double T = std::max(expiry_days, 1) / 365.0;
                    bool is_call = (c.opt_type == OptionType::Call);
                    c.iv = computeIVFromPrice(c.close, spot_price, c.strike, T, risk_free_rate, is_call);
                }

                if (c.iv <= 0) { ++skip_iv; continue; }

                c.expiry_days = expiry_days;
                contracts.push_back(std::move(c));
            }
            return true;
        });

        WARN("[IVSurface] Parse stats: skip_strike={}, skip_date={}, skip_expiry={}, skip_iv={}, contracts={}", 
             skip_strike, skip_date, skip_expiry, skip_iv, contracts.size());

        if (!ok) {
            nlohmann::json err;
            err["error"] = "IV surface query failed";
            res.status = 500;
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 应用异常报价过滤器
        FilterConfig filter_cfg;
        filter_cfg.is_index_option = (exchange == "CFFEX");
        OptionContractFilter filter(filter_cfg);
        auto filter_result = filter.apply(std::move(contracts));

        WARN("[IVSurface] After filter: kept={}, removed_total={}, L1={}, L2={}, L3={}, L4={}, L5={}", 
             filter_result.kept.size(),
             filter_result.stats.filtered_count,
             filter_result.stats.removed_L1_hard,
             filter_result.stats.removed_L2_liquidity,
             filter_result.stats.removed_L3_spread_proxy,
             filter_result.stats.removed_L4_moneyness,
             filter_result.stats.removed_L5_parity);

        // 构建 IV 曲面 (使用过滤后的合约)
        Vector<IVSurface::IVPoint> points;
        nlohmann::json raw_points = nlohmann::json::array();
        for (auto& c : filter_result.kept) {
            points.push_back({c.strike, c.expiry_days, c.iv, c.opt_type});
            raw_points.push_back({
                {"strike", c.strike}, {"expiry_days", c.expiry_days},
                {"iv", c.iv}, {"contract_name", c.contract_name},
                {"call_put", fromOptionType(c.opt_type)}
            });
        }

        WARN("[IVSurface] Points size: {}", points.size());

        IVSurface surface;
        surface.build(points);

        // 生成网格 (call/put 分离)
        Vector<double> strikes;
        Vector<int> expiry_list;
        {
            std::set<double> k_set;
            std::set<int> e_set;
            for (auto& p : points) {
                k_set.insert(p.strike);
                e_set.insert(p.expiry_days);
            }
            strikes.assign(k_set.begin(), k_set.end());
            expiry_list.assign(e_set.begin(), e_set.end());
        }

        auto [call_surface, put_surface] = surface.generateSurface(strikes, expiry_list);

        nlohmann::json response;
        response["raw_points"] = std::move(raw_points);
        response["strikes"] = strikes;
        response["expiry_days"] = expiry_list;

        // 曲面数据: 优先使用 call_surface (向后兼容),新增 call_surface/put_surface
        nlohmann::json grid_json = nlohmann::json::array();
        for (size_t i = 0; i < call_surface.size(); ++i) {
            grid_json.push_back(call_surface[i]);
        }
        response["surface"] = std::move(grid_json);

        // 新增: call/put 分离曲面
        nlohmann::json call_grid = nlohmann::json::array();
        for (size_t i = 0; i < call_surface.size(); ++i) {
            call_grid.push_back(call_surface[i]);
        }
        response["call_surface"] = std::move(call_grid);

        if (!put_surface.empty() && !put_surface[0].empty()) {
            nlohmann::json put_grid = nlohmann::json::array();
            for (size_t i = 0; i < put_surface.size(); ++i) {
                put_grid.push_back(put_surface[i]);
            }
            response["put_surface"] = std::move(put_grid);
        }

        response["count"] = (int)points.size();
        response["filter_stats"] = filter_result.stats.toJson();

        WARN("[IVSurface] Response: count={}, strikes={}, expiry_days={}", 
             (int)points.size(), strikes.size(), expiry_list.size());

        res.set_content(response.dump(), "application/json");
    } catch (const std::exception& e) {
        ERROR("[IVSurface] Exception: {}", e.what());
        nlohmann::json err;
        err["error"] = e.what();
        res.status = 500;
        res.set_content(err.dump(), "application/json");
    }
}
