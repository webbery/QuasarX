#include "Handler/TimerHandler.h"
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
    bool is_backup = false;
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
            if (!is_backup) {
            // if (rest_cnt > 720 && !is_backup) {
              // 1个小时无更新,刷新并备份到bak
              auto data_path = _server->GetConfig().GetDatabasePath() + "/zh";
              auto dst_path = _server->GetConfig().GetDatabasePath() + "/zh.backup";
              if (MergeBackup(data_path, dst_path)) {
                RunCommand("rm -rf " + data_path);
              }
              // PackageData();
              is_backup = true;
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
        is_backup = false;
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
      for (int i = 0; i < 5; ++i) {
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

bool RecordHandler::MergeBackup(const String& src, const String& dst) {
  std::filesystem::path src_root(src);
  std::filesystem::path dst_root(src);
  for (auto& entry: std::filesystem::recursive_directory_iterator(src_root)) {
    if (entry.is_directory()) {
      if (!std::filesystem::exists(dst_root / entry)) {
        std::filesystem::create_directories(dst_root / entry);
      }
      MergeBackup(dst_root / entry, dst_root / entry);
    }
    else if (entry.is_regular_file()) {
      if (!MergeCSV(src_root/entry, dst_root/entry))
        return false;
    }
  }
  return true;
}

bool RecordHandler::MergeCSV(const String& src, const String& dst) {
  std::ifstream ifs;
  ifs.open(src.c_str(), std::ifstream::in);

  if (!ifs.is_open()) {
    return false;
  }

  std::fstream ofs;
  ofs.open(dst.c_str(), std::ios::app|std::ios::ate);
  if (!ofs.is_open()) {
    ifs.close();
    return false;
  }

  auto lambda_readLines = []<typename FS>(FS& ifs, List<String>& lines) {
    String line;
    while (std::getline(ifs, line)) {
      if (line.empty())
        break;
      if (line.find("date") != String::npos)
        continue;

      lines.emplace_back(std::move(line));
    }
  };
  // 定位到ofs末尾行
  List<String> new_lines, org_lines;
  lambda_readLines(ifs, new_lines);
  lambda_readLines(ofs, org_lines);
  time_t last_t = 0;
  if (org_lines.empty()) {

  } else {
    auto& last_line = org_lines.back();
    List<String> last_info;
    split(last_line, last_info, ",");
    last_t = FromStr(last_info.front());
  }
  
  List<String> append_lines;
  for (auto& line: new_lines) {
    List<String> info;
    split(new_lines.front(), info, ",");
    auto t = FromStr(info.front());
    if (t > last_t) {
      append_lines.emplace_back(std::move(line));
    }
  }
  for (auto& line: append_lines) {
    ofs.write(line.c_str(), line.size());
  }

  ifs.close();
  ofs.close();
  return true;
}
