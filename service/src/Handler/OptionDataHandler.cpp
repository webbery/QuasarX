#include "Handler/OptionDataHandler.h"
#include "Util/OptionDataDB.h"
#include "Util/PythonRunner.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <thread>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

// CFFEX/SSE/SZSE 脚本名 → exchange 映射
static const std::map<String, String> EXCHANGE_SCRIPT_MAP = {
    {"CFFEX", "fetch_cffex_option.py"},
    {"SSE",   "fetch_sse_option.py"},
    {"SZSE",  "fetch_szse_option.py"},
};

// 每个交易所支持的品种列表（与脚本内 CFFEX_PRODUCTS / SSE_PRODUCTS / SZSE_PRODUCTS 对齐）
static const std::map<String, std::set<String>> EXCHANGE_PRODUCTS = {
    {"CFFEX", {"IO", "HO", "MO"}},
    {"SSE",   {"50ETF", "300ETF", "500ETF", "STAR50ETF"}},
    {"SZSE",  {"159919", "159915", "159922", "159901"}},
};

static bool isValidExchange(const String& ex) {
    return EXCHANGE_SCRIPT_MAP.count(ex) > 0;
}

static bool isValidProduct(const String& exchange, const String& product) {
    auto it = EXCHANGE_PRODUCTS.find(exchange);
    return it != EXCHANGE_PRODUCTS.end() && it->second.count(product) > 0;
}

// ═══════════════════════════════════════════════════════════
//  POST /v0/option/data — 触发下载 + 导入
//
//  body:
//    {
//      "exchange": "CFFEX" / "SSE" / "SZSE",
//      "products": ["IO","HO"],            // 至少一个
//      "start":    "2024-01-01",           // 可选, 缺省 today-30d
//      "end":      "2024-01-31",           // 可选, 缺省 today
//      "env":      "default"               // 可选, Python 环境
//    }
// ═══════════════════════════════════════════════════════════

void OptionDataHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json params;
    try {
        params = nlohmann::json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"message":"Invalid JSON"})", "application/json");
        return;
    }

    auto exchange = params.value("exchange", std::string(""));
    if (!isValidExchange(exchange)) {
        res.status = 400;
        res.set_content(
            fmt::format(R"msg({{"message":"Invalid exchange: {} [must be CFFEX/SSE/SZSE]"}})msg", exchange),
            "application/json");
        return;
    }

    // 解析品种列表
    Vector<String> products;
    if (params.contains("products") && params["products"].is_array()) {
        for (auto& p : params["products"]) {
            String prod = p.get<String>();
            if (!isValidProduct(exchange, prod)) {
                res.status = 400;
                res.set_content(
                    fmt::format(R"({{"message":"Invalid product '{}' for exchange {}"}})", prod, exchange),
                    "application/json");
                return;
            }
            products.push_back(prod);
        }
    }
    if (products.empty()) {
        // 缺省: 该交易所的全部品种
        for (const auto& p : EXCHANGE_PRODUCTS.at(exchange)) {
            products.push_back(p);
        }
    }

    auto start_date = params.value("start", std::string(""));
    auto end_date   = params.value("end",   std::string(""));
    auto env_name   = params.value("env",   std::string(""));

    // 保存 CSV 的目录 (与 fetch_*.py 默认 --save-dir 一致: data/option_<exchange>)
    auto db_path = _server->GetConfig().GetDatabasePath();
    String exchange_subdir;
    if (exchange == "CFFEX")      exchange_subdir = "cffex";
    else if (exchange == "SSE")   exchange_subdir = "sse";
    else                          exchange_subdir = "szse";
    auto csv_dir = db_path + "/option/" + exchange_subdir;
    fs::create_directories(csv_dir);

    nng_socket sse_sock = _server->GetSocket();
    auto pyEnv = PythonEnv::fromConfig(_server->GetConfig().GetRawConfig());
    auto interpreter = pyEnv.resolve(env_name);

    // 后台线程: 按品种循环 (失败跳过, 不中断)
    std::thread([sse_sock, exchange, products, start_date, end_date, interpreter, csv_dir, db_path]() {
        SetCurrentThreadName("OptionDataDownload");

        SendSSE(sse_sock, "option_data_download", {
            {"status", "started"},
            {"exchange", exchange},
            {"products", std::to_string(products.size())},
            {"start", start_date},
            {"end", end_date}
        });

        // 初始化 OptionDataDB
        auto& optDB = OptionDataDB::instance();
        if (!optDB.isInitialized()) {
            if (!optDB.init(db_path + "/option", "option.db")) {
                SendSSE(sse_sock, "option_data_download", {
                    {"status", "aborted"},
                    {"reason", "OptionDataDB init failed"}
                });
                return;
            }
        }

        String script_name = EXCHANGE_SCRIPT_MAP.at(exchange);
        int total_imported = 0;
        int success_products = 0, fail_products = 0;

        for (const auto& prod : products) {
            std::string cmd = interpreter + " tools/" + script_name
                            + " --product " + prod
                            + " --save-dir " + csv_dir + "/" + prod;
            if (!start_date.empty()) cmd += " --start " + start_date;
            if (!end_date.empty())   cmd += " --end "   + end_date;

            SendSSE(sse_sock, "option_data_download", {
                {"status", "downloading"},
                {"exchange", exchange},
                {"product", prod},
                {"command", cmd}
            });

            String output;
            bool ok = RunCommand(cmd, output);

            if (!ok) {
                WARN("[OptionDataHandler] {} {} download failed: {}", exchange, prod, output);
                fail_products++;
                SendSSE(sse_sock, "option_data_download", {
                    {"status", "product_failed"},
                    {"exchange", exchange},
                    {"product", prod},
                    {"output", output}
                });
                continue;   // 失败跳过, 不中断
            }
            success_products++;

            // 扫描该品种目录下所有 CSV 并 import
            int imported_for_prod = 0;
            String prod_csv_dir = csv_dir + "/" + prod;
            if (fs::exists(prod_csv_dir)) {
                for (auto& entry : fs::directory_iterator(prod_csv_dir)) {
                    if (!entry.is_regular_file()) continue;
                    auto ext = entry.path().extension().string();
                    if (ext != ".csv") continue;
                    int n = optDB.importCsv(entry.path().string());
                    if (n > 0) {
                        imported_for_prod += n;
                        total_imported += n;
                    }
                    // 导入成功后删除 CSV, 避免重复导入 (OptionDataDB 用 ON CONFLICT upsert,
                    // 但清理可减少磁盘占用, 与 FinanceHandler 模式一致)
                    fs::remove(entry.path());
                }
            }
            // 尝试清理空的品种子目录
            std::error_code ec;
            fs::remove(prod_csv_dir, ec);

            SendSSE(sse_sock, "option_data_download", {
                {"status", "imported"},
                {"exchange", exchange},
                {"product", prod},
                {"rows", std::to_string(imported_for_prod)}
            });
        }

        SendSSE(sse_sock, "option_data_download", {
            {"status", "done"},
            {"exchange", exchange},
            {"success_products", std::to_string(success_products)},
            {"fail_products", std::to_string(fail_products)},
            {"total_rows", std::to_string(total_imported)}
        });
    }).detach();

    nlohmann::json resp;
    resp["status"] = "started";
    resp["exchange"] = exchange;
    resp["products"] = products;
    resp["start"] = start_date;
    resp["end"] = end_date;
    resp["csv_dir"] = csv_dir;
    res.set_content(resp.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  GET /v0/option/data
//
//  参数:
//    contract=XXX                → 该合约历史日线
//    exchange=CFFEX & product=IO → 该品种合约概要
//    exchange=CFFEX              → 该交易所合约概要
//    无参数                      → 全部合约概要
// ═══════════════════════════════════════════════════════════

void OptionDataHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& optDB = OptionDataDB::instance();
    if (!optDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!optDB.init(db_path + "/option", "option.db")) {
            res.status = 500;
            res.set_content(R"({"message":"OptionDataDB not initialized"})", "application/json");
            return;
        }
    }

    auto contract  = req.get_param_value("contract");
    auto exchange  = req.get_param_value("exchange");
    auto product   = req.get_param_value("product");
    auto start_dt  = req.get_param_value("start");
    auto end_dt    = req.get_param_value("end");
    auto limit_str = req.get_param_value("limit");
    int limit = limit_str.empty() ? 500 : std::atoi(limit_str.c_str());

    // 按合约查历史
    if (!contract.empty()) {
        auto result = optDB.queryByContract(contract, start_dt, end_dt, limit);
        res.set_content(result.dump(), "application/json");
        return;
    }

    // 列合约概要（带过滤）
    auto result = optDB.listContracts(exchange, product);
    res.set_content(result.dump(), "application/json");
}

// ═══════════════════════════════════════════════════════════
//  DELETE /v0/option/data
//
//  参数:
//    contract=XXX  → 删除该合约
//    无参数        → 清空所有
// ═══════════════════════════════════════════════════════════

void OptionDataHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto& optDB = OptionDataDB::instance();
    if (!optDB.isInitialized()) {
        auto db_path = _server->GetConfig().GetDatabasePath();
        if (!optDB.init(db_path + "/option", "option.db")) {
            res.status = 500;
            res.set_content(R"({"message":"OptionDataDB not initialized"})", "application/json");
            return;
        }
    }

    auto contract = req.get_param_value("contract");

    nlohmann::json resp;
    if (contract.empty()) {
        // 清空
        bool ok = optDB.deleteAll();
        resp["status"] = ok ? "cleared" : "failed";
    } else {
        bool ok = optDB.deleteContract(contract);
        resp["status"] = ok ? "deleted" : "failed";
        resp["contract"] = contract;
    }
    res.set_content(resp.dump(), "application/json");
}
