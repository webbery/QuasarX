#include "Handler/OptionPricingHandler.h"
#include "Derivative/OptionPricer.h"
#include "Derivative/IVSurface.h"
#include "Util/OptionDataDB.h"
#include "Util/system.h"
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
static int daysToExpiry(int expiry_year, int expiry_month, int trade_year, int trade_month, int trade_day) {
    // 简化: 到期日取第三个周五的近似（月度期权）= 月份第 15~21 天
    // 用月中 17 日近似
    auto trade = sys_days{year{trade_year} / month{trade_month} / day{trade_day}};
    auto expiry = sys_days{year{expiry_year} / month{expiry_month} / day{17}};
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
                        auto exp_tp = sys_days{year{ey} / month{em} / day{ed}};
                        auto today = floor<days>(system_clock::now());
                        T = std::max((exp_tp - today).count(), 1L) / 365.0;
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
                    auto exp_tp = sys_days{year{ey} / month{em} / day{ed}};
                    auto today = floor<days>(system_clock::now());
                    T = std::max((exp_tp - today).count(), 1L) / 365.0;
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

        if (exchange.empty() || product.empty()) {
            nlohmann::json err;
            err["error"] = "exchange and product parameters required";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        auto& db = OptionDataDB::instance();
        if (!db.isInitialized()) {
            nlohmann::json err;
            err["error"] = "OptionDataDB not initialized";
            res.status = 500;
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 查询该产品最新日期的所有合约
        String sql = fmt::format(
            "SELECT contract_name, call_put, strike_price, implied_volatility, "
            "       trade_date, underlying "
            "FROM option_daily "
            "WHERE exchange = '{}' AND product = '{}' "
            "  AND trade_date = (SELECT MAX(trade_date) FROM option_daily "
            "                    WHERE exchange = '{}' AND product = '{}') "
            "  AND implied_volatility > 0 "
            "ORDER BY contract_name",
            exchange, product, exchange, product);

        // 构建 IV 数据点
        Vector<IVSurface::IVPoint> points;
        nlohmann::json raw_points = nlohmann::json::array();

        // 获取今天日期用于计算到期天数
        auto today = floor<days>(system_clock::now());
        auto today_ymd = year_month_day{today};
        int ty = (int)today_ymd.year();
        int tm = (unsigned)today_ymd.month();
        int td = (unsigned)today_ymd.day();

        bool ok = db.query(sql, [&](duckdb_result& result) -> bool {
            idx_t row_count = duckdb_row_count(&result);
            for (idx_t i = 0; i < row_count; ++i) {
                String contract_name = duckdb_value_varchar(&result, 0, i);
                String call_put = duckdb_value_varchar(&result, 1, i);
                double strike = duckdb_value_double(&result, 2, i);
                double iv = duckdb_value_double(&result, 3, i);

                if (strike <= 0 || iv <= 0) continue;

                auto [ey, em] = parseExpiryFromName(contract_name, product);
                if (ey == 0) continue;

                int expiry_days = daysToExpiry(ey, em, ty, tm, td);
                points.push_back({strike, expiry_days, iv});
                raw_points.push_back({
                    {"strike", strike}, {"expiry_days", expiry_days},
                    {"iv", iv}, {"contract_name", contract_name},
                    {"call_put", call_put}
                });
            }
            return true;
        });

        if (!ok) {
            nlohmann::json err;
            err["error"] = "IV surface query failed";
            res.status = 500;
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 构建 IV 曲面
        IVSurface surface;
        surface.build(points);

        // 生成网格
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

        auto grid = surface.generateSurface(strikes, expiry_list);

        nlohmann::json response;
        response["raw_points"] = std::move(raw_points);
        response["strikes"] = strikes;
        response["expiry_days"] = expiry_list;
        // grid: [expiry_idx][strike_idx]
        nlohmann::json grid_json = nlohmann::json::array();
        for (size_t i = 0; i < grid.size(); ++i) {
            grid_json.push_back(grid[i]);
        }
        response["surface"] = std::move(grid_json);
        response["count"] = (int)points.size();

        res.set_content(response.dump(), "application/json");
    } catch (const std::exception& e) {
        nlohmann::json err;
        err["error"] = e.what();
        res.status = 500;
        res.set_content(err.dump(), "application/json");
    }
}
