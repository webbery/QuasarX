#include "Handler/RecordHandler.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <nng/protocol/pubsub0/sub.h>
#include "Bridge/exchange.h"
#include <yas/serialize.hpp>
#include "HttpHandler.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "json.hpp"

#ifdef _DEBUG
#define MAX_FLUSH_CBOR_SIZE     5
#else
#define MAX_FLUSH_CBOR_SIZE     50
#endif

namespace {

// 根据 symbol 获取类型路径
String GetTypePath(symbol_t sym) {
  if (is_future(sym)) {
    return "/zh/future";
  }
  else if (is_stock(sym)) {
    return "/zh/stock";
  }
  else if (is_option(sym)) {
    return "/zh/option";
  }
  else if (is_fund(sym)) {
    return "/zh/fund";
  } else {
    WARN("not support type for symbol.");
    return "/zh/temp";
  }
}

}

RecordHandler::RecordHandler(Server* server):HttpHandler(server), _main(nullptr){

}

RecordHandler::~RecordHandler() {
    _close = true;
    if (_main) {
      _main->join();
      delete _main;
      _main = nullptr;
    }
    // 程序退出时 flush CBOR 缓冲区
    FlushCborBuffer();
}

void RecordHandler::StartRecord(bool enable) {
  if (enable && !_main) {
    _close = false;
    _main = new std::thread(&RecordHandler::run, this);
  }
}

void RecordHandler::post(const httplib::Request &req, httplib::Response &res) {
  HandleTicksQuery(req, res);
}

void RecordHandler::run() {
    if (!Subscribe(URI_RAW_QUOTE, sock)) {
        return;
    }
    SetCurrentThreadName("Recorder");
    _close = false;

    // CBOR 目录
    _cborBasePath = _server->GetConfig().GetDatabasePath() + "/daily";
    std::filesystem::create_directories(_cborBasePath);

    constexpr std::size_t flags = yas::mem | yas::binary;
    short rest_cnt = 0;
    while (!_close) {
        char* buff = NULL;
        size_t sz;
        int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            rest_cnt += 1;
            if (rest_cnt % 10 == 0) {
                // 定期 flush CBOR 缓冲区
                time_t now = std::time(nullptr);
                for (auto& [sym, buf] : _cborBuffers) {
                    if (!buf.ticks.empty() && (now - buf.lastFlush) >= 600) {
                        // 触发超时 flush
                        buf.lastFlush = 0; // 强制下一次循环 flush
                    }
                }
            }
            continue;
        }
        yas::shared_buffer buf;
        buf.assign(buff, sz);
        QuoteInfo quote;
        yas::load<flags>(buf, quote);
        nng_free(buff, sz);

        // symbol 过滤：空集合 = 全记录，非空 = 只记录匹配的
        if (!_symbols.empty()) {
            String symStr = get_symbol(quote._symbol);
            if (symStr == "0" || _symbols.find(symStr) == _symbols.end()) {
                continue;
            }
        }

        // CBOR 缓冲记录
        PushCborTick(quote);
        rest_cnt = 0;
    }
    nng_close(sock);
    sock.id = 0;

    printf("record stop.\n");
}

void RecordHandler::SetSymbols(const Set<String>& symbols) {
  _symbols = symbols;
}

// ============== CBOR Tick 记录 ==============

void RecordHandler::PushCborTick(const QuoteInfo& quote) {
    auto& buf = _cborBuffers[quote._symbol];
    buf.ticks.push_back(quote);

    // 50 条触发 flush
    if (buf.ticks.size() >= MAX_FLUSH_CBOR_SIZE) {
        FlushCborBuffer();
    }
}

String RecordHandler::CborFileName(const QuoteInfo& tick) {
    // 日期（本地时间）：YYYYMMDD
    std::time_t t = static_cast<std::time_t>(tick._time);
    std::tm* tm = localtime(&t);
    char dateBuf[16];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", tm);
    String date(dateBuf);

    // 代码（已包含交易所后缀，如 600000.SH）
    String code = format_symbol(get_symbol(tick._symbol));

    return date + "." + code + ".cbor";
}

void RecordHandler::FlushCborBuffer() {
    time_t now = std::time(nullptr);

    for (auto& [sym, buf] : _cborBuffers) {
        if (buf.ticks.empty() || is_null(sym)) continue;

        // 10 分钟超时或缓冲区满，触发 flush
        bool timeout = (now - buf.lastFlush) >= 600;
        if (!timeout && buf.ticks.size() < MAX_FLUSH_CBOR_SIZE) continue;

        auto symbol = get_symbol(sym);

        // 按类型分目录: /daily/zh/stock/
        String typePath = GetTypePath(sym);
        String dirPath = _cborBasePath + typePath;
        std::filesystem::create_directories(dirPath);

        // 按日期分组
        Map<String, Vector<QuoteInfo>> dateGroups;
        for (auto& tick : buf.ticks) {
            String fname = CborFileName(tick);
            dateGroups[fname].push_back(tick);
        }

        // 每组写入对应日期的文件
        for (auto& [fname, ticks] : dateGroups) {
            String filepath = dirPath + "/" + fname;
            std::fstream fs(filepath, std::ios::app | std::ios::binary);
            if (!fs.is_open()) continue;

            for (auto& tick : ticks) {
                nlohmann::json j;
                j["time"] = (int64_t)tick._time;
                j["open"] = tick._open;
                j["close"] = tick._close;
                j["high"] = tick._high;
                j["low"] = tick._low;
                j["volume"] = (int64_t)tick._volume;
                j["turnover"] = (int64_t)tick._turnover;
                j["value"] = tick._value;
                j["upper"] = tick._upper;
                j["lower"] = tick._lower;
                j["source"] = tick._source;
                j["confidence"] = tick._confidence;

                // L2 买卖盘
                nlohmann::json bids = nlohmann::json::array();
                nlohmann::json asks = nlohmann::json::array();
                for (int i = 0; i < MAX_ORDER_SIZE_LVL2; ++i) {
                    if (tick._bidPrice[i] > 0 || tick._askPrice[i] > 0) {
                        bids.push_back({{"price", tick._bidPrice[i]}, {"volume", (int64_t)tick._bidVolume[i]}});
                        asks.push_back({{"price", tick._askPrice[i]}, {"volume", (int64_t)tick._askVolume[i]}});
                    }
                }
                if (!bids.empty()) j["bids"] = bids;
                if (!asks.empty()) j["asks"] = asks;

                auto cbor = nlohmann::json::to_cbor(j);
                uint32_t len = cbor.size();
                fs.write(reinterpret_cast<const char*>(&len), sizeof(len));
                fs.write(reinterpret_cast<const char*>(cbor.data()), len);
            }

            fs.flush();
            fs.close();
        }

        buf.ticks.clear();
        buf.lastFlush = now;
    }
}

// ============== HTTP 查询接口 ==============

void RecordHandler::HandleTicksQuery(const httplib::Request &req, httplib::Response &res) {
    String symbol = req.get_param_value("symbol");
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");

    if (symbol.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "symbol is required"}}.dump(), "application/json");
        return;
    }

    // 规范 code：去掉交易所后缀，补齐 6 位
    String code = format_symbol(symbol.substr(0, symbol.find('.')));

    // 解析时间范围
    time_t start = startStr.empty() ? 0 : std::stoll(startStr);
    time_t end = endStr.empty() ? std::numeric_limits<time_t>::max() : std::stoll(endStr);

    // 根据 code 推断类型目录
    String typePath;
    if (code.starts_with("60") || code.starts_with("68")) typePath = "/zh/stock";
    else if (code.starts_with("00") || code.starts_with("30")) typePath = "/zh/stock";
    else if (code.starts_with("8") || code.starts_with("4")) typePath = "/zh/stock";
    else { typePath = "/zh/temp"; }

    String dirPath = _cborBasePath + typePath;
    if (!std::filesystem::exists(dirPath)) {
        res.status = 404;
        res.set_content(nlohmann::json{{"error", "no data for symbol"}}.dump(), "application/json");
        return;
    }

    // 查找匹配的文件：YYYYMMDD.sh.600000.cbor
    Vector<String> files;
    String suffix = "." + code + ".cbor";
    for (auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            String fname = entry.path().filename().string();
            if (!fname.ends_with(".cbor")) continue;
            // 匹配 .CODE.cbor 结尾（交易所前缀如 sh/sz 在中间）
            if (fname.size() > suffix.size() + 1 && fname.substr(fname.size() - suffix.size() - 1) == "." + suffix) {
                files.push_back(entry.path().string());
            }
        }
    }

    if (files.empty()) {
        res.status = 404;
        res.set_content(nlohmann::json{{"error", "no data for symbol"}}.dump(), "application/json");
        return;
    }

    // 按文件名排序（YYYYMMDD 前缀自然排序 = 时间顺序）
    std::sort(files.begin(), files.end());

    nlohmann::json result = nlohmann::json::array();
    for (auto& filepath : files) {
        std::fstream fs(filepath, std::ios::in | std::ios::binary);
        if (!fs.is_open()) continue;

        while (fs.good()) {
            uint32_t len = 0;
            fs.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len == 0 || len > 4096) break;

            Vector<uint8_t> cbor(len);
            fs.read(reinterpret_cast<char*>(cbor.data()), len);
            if (!fs.good()) break;

            try {
                auto j = nlohmann::json::from_cbor(cbor);
                int64_t t = j.value("time", (int64_t)0);
                if (t >= start && t <= end) {
                    result.push_back(j);
                }
            } catch (...) {
                continue;
            }
        }
    }

    res.set_content(result.dump(), "application/json");
}
