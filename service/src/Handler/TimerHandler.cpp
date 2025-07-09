#include "Handler/TimerHandler.h"
#include <filesystem>
#include <ios>
#include <limits>
#include <memory>
#include <nng/protocol/pubsub0/sub.h>
#include "Bridge/exchange.h"
#include <yas/serialize.hpp>
#include "HttpHandler.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include <fstream>

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
}

void RecordHandler::StartRecord(bool enable) {
  if (enable && !_main) {
    _close = false;
    _main = new std::thread(&RecordHandler::run, this);
  }
}

void RecordHandler::post(const httplib::Request &req, httplib::Response &res) {
  // Add record symbols
}

void RecordHandler::run() {
    if (!Subscribe(URI_RAW_QUOTE, sock)) {
        return;
    }
    SetCurrentThreadName("Recorder");
    _close = false;
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
            if (rest_cnt > 720) {
              // 1个小时无更新
              // PackageData();
              rest_cnt = 0;
            }
            continue;
        }
        yas::shared_buffer buf;
        buf.assign(buff, sz);
        QuoteInfo quote;
        yas::load<flags>(buf, quote);
        nng_free(buff, sz);
        ++ticks[quote._symbol];
        auto& fstr = GetFileStream(quote._symbol);
        WriteCSV(fstr, quote);
        if (ticks[quote._symbol] % 10 == 0) {
            // 刷一次数据到磁盘
            if (fstr.good()) {
              fstr.flush();
            }
        }
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
      String header("datetime,open,close,volumn,turnover,");
      for (int i = 0; i < 5; ++i) {
        header += "bid" + std::to_string(i+1) + ",";
        header += "ask" + std::to_string(i+1) + ",";
        header += "ask_volumn" + std::to_string(i+1) + ",";
        header += "bid_volumn" + std::to_string(i+1) + ",";
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
  line += std::format("{}", infos._volumn);
  line += ",";
  line += std::format("{}", infos._turnover);
  for (int i = 0; i < 5; ++i) {
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
