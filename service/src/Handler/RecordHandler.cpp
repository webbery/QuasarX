#include "Handler/RecordHandler.h"
#include <filesystem>
#include <ios>
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
#include <fstream>
#include "json.hpp"

namespace {
  
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
    Map<symbol_t, uint32_t> ticks;
    short rest_cnt = 0;
    while (!_close) {
        char* buff = NULL;
        size_t sz;
        int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            rest_cnt += 1;
            if (rest_cnt % 10 == 0) {
              for (auto& item : _fs_map) {
                item.second.flush();
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

        ++ticks[quote._symbol];
        auto& fstr = GetFileStream(quote._symbol);
        WriteCSV(fstr, quote);
        if (ticks[quote._symbol] % 10 == 0) {
            // 刷一次数据到磁盘
            if (fstr.good()) {
              fstr.flush();
            }
        }
        // CBOR 缓冲记录
        PushCborTick(quote);
        rest_cnt = 0;
    }
    nng_close(sock);
    sock.id = 0;

    //关闭文件
    for (auto& item : _fs_map) {
        item.second.flush();
        item.second.close();
    }
    printf("record stop.\n");
}

String RecordHandler::GetTypePath(symbol_t sym) {
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

std::fstream& RecordHandler::GetFileStream(const symbol_t& name) {
  auto itr = _fs_map.find(name);
  if (itr == _fs_map.end()) {
    auto& config = _server->GetConfig();
    auto datapath = config.GetDatabasePath() + GetTypePath(name);
    if (!std::filesystem::exists(datapath)) {
      std::filesystem::create_directories(datapath);
    }
    // 打开csv文件并定位到末尾
    auto& fs = _fs_map[name];
    auto symbol = get_symbol(name);
    if (symbol == "0")
      return fs;

    String filepath = datapath + "/" + format_symbol(symbol) + ".csv";
    if (!std::filesystem::exists(filepath)) {
      fs.open(filepath, std::ios_base::app | std::ios_base::ate | std::ios_base::out);
      String header("datetime,open,close,high,low,volume,turnover,");
      for (int i = 0; i < MAX_ORDER_SIZE_LVL2; ++i) {
        header += "bid" + std::to_string(i+1) + ",";
        header += "ask" + std::to_string(i+1) + ",";
        header += "ask_volume" + std::to_string(i+1) + ",";
        header += "bid_volume" + std::to_string(i+1) + ",";
      }
      header += "\n";
      fs.write(header.c_str(), header.size());
    } else {
      fs.open(filepath, std::ios_base::app | std::ios_base::ate | std::ios_base::out);
    }
    return fs;
  }
  return itr->second;
}

void RecordHandler::SetSymbols(const Set<String>& symbols) {
  _symbols = symbols;
}

void RecordHandler::WriteCSV(std::fstream& fs, const QuoteInfo& infos) {
  if (!fs.is_open())
    return;

  String line;
  // line += get_symbol(infos._symbol);
  // line += ",";
  line += ToString(infos._time);
  line += ",";
  line += std::format("{:.4f}", infos._open);
  line += ",";
  line += std::format("{:.4f}", infos._close);
  line += ",";
  line += std::format("{:.4f}", infos._high);
  line += ",";
  line += std::format("{:.4f}", infos._low);
  line += ",";
  line += std::format("{}", infos._volume);
  line += ",";
  line += std::format("{}", infos._turnover);
  for (int i = 0; i < MAX_ORDER_SIZE_LVL2; ++i) {
    if (infos._bidPrice[i] > std::numeric_limits<double>::epsilon() || infos._askPrice[i] > std::numeric_limits<double>::epsilon()) {
      line += ",";
      line += std::format("{:.4f}", infos._bidPrice[i]);
      line += ",";
      line += std::format("{:.4f}", infos._askPrice[i]);
      line += ",";
      line += std::format("{}", infos._askVolume[i]);
      line += ",";
      line += std::format("{}", infos._bidVolume[i]);
    } else {
      break;
    }
  }
  
  line += "\n";
  fs.write(line.c_str(), line.size());
}

// ============== CBOR Tick 记录 ==============

void RecordHandler::PushCborTick(const QuoteInfo& quote) {
    std::lock_guard<std::mutex> lock(_cborMtx);
    auto& buf = _cborBuffers[quote._symbol];
    buf.ticks.push_back(quote);

    // 50 条触发 flush
    if (buf.ticks.size() >= 50) {
        FlushCborBuffer();
    }
}

String RecordHandler::CborFileName(const QuoteInfo& tick) {
    // 日期
    std::tm* tm = localtime(&tick._time);
    char dateBuf[16];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", tm);
    String date(dateBuf);

    // 市场（如 SH/SZ/BJ）
    char market[3] = "SH";
    const Map<char, String> marketMap = {
        {MT_Shenzhen, "SZ"}, {MT_Shanghai, "SH"}, {MT_Beijing, "BJ"}
    };
    auto it = marketMap.find(tick._symbol._exchange);
    if (it != marketMap.end()) {
        market[0] = it->second[0];
        market[1] = it->second[1];
    }

    return date + "." + market + "." + format_symbol(get_symbol(tick._symbol)) + ".cbor";
}

void RecordHandler::FlushCborBuffer() {
    std::lock_guard<std::mutex> lock(_cborMtx);
    time_t now = std::time(nullptr);

    for (auto& [sym, buf] : _cborBuffers) {
        if (buf.ticks.empty()) continue;

        // 10 分钟超时或缓冲区满，触发 flush
        bool timeout = (now - buf.lastFlush) >= 600;
        if (!timeout && buf.ticks.size() < 50) continue;

        auto symbol = get_symbol(sym);
        if (symbol == "0") continue;

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

    symbol_t sym = to_symbol(symbol);
    if (is_null(sym)) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "invalid symbol format"}}.dump(), "application/json");
        return;
    }

    time_t start = startStr.empty() ? 0 : std::stoll(startStr);
    time_t end = endStr.empty() ? std::numeric_limits<time_t>::max() : std::stoll(endStr);

    String typePath = GetTypePath(sym);
    String dirPath = _cborBasePath + typePath;

    if (!std::filesystem::exists(dirPath)) {
        res.status = 404;
        res.set_content(nlohmann::json{{"error", "no data for symbol"}}.dump(), "application/json");
        return;
    }

    // 查找匹配的文件：*.SZ.600000.cbor 或 *.SH.symbol.cbor
    String searchPattern = "*." + format_symbol(symbol) + ".cbor";
    Vector<String> files;
    for (auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            String fname = entry.path().filename().string();
            // 匹配末尾 .symbol.cbor
            if (fname.size() > format_symbol(symbol).size() + 5 &&
                fname.substr(fname.size() - format_symbol(symbol).size() - 5) == "." + format_symbol(symbol) + ".cbor") {
                files.push_back(entry.path().string());
            }
        }
    }

    if (files.empty()) {
        res.status = 404;
        res.set_content(nlohmann::json{{"error", "no data for symbol"}}.dump(), "application/json");
        return;
    }

    nlohmann::json result = nlohmann::json::array();
    for (auto& filepath : files) {
        std::fstream fs(filepath, std::ios::in | std::ios::binary);
        if (!fs.is_open()) continue;

        while (fs.good()) {
            uint32_t len = 0;
            fs.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len == 0 || len > 1024 * 1024) break;

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
