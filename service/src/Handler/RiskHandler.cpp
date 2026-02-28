#include "Handler/RiskHandler.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "json.hpp"
#include "server.h"
#include "Risk/StopLoss.h"
#include "Util/system.h"
#include <cstdlib>
#include <mutex>
#include "Util/string_algorithm.h"

StopLossHandler::StopLossHandler(Server* handle)
  :HttpHandler(handle),_main(nullptr), _exit(false)
{}

StopLossHandler::~StopLossHandler()
{
  _exit = true;
  if (_main) {
    _main->join();
    delete _main;
    _main = nullptr;
  }
}

void StopLossHandler::doWork(const std::vector<std::string>& params) {
  if (!_main) {
    auto& config = _server->GetConfig();
    _sender = config.GetSMTPSender();
    _pwd = config.GetSMTPPasswd();
    _main = new std::thread(&StopLossHandler::run, this);
  }
}

void StopLossHandler::get(const httplib::Request& req, httplib::Response& res)
{
  auto& config = _server->GetConfig();
  auto& stloss = config.GetAllStopLoss();
  res.status = 200;
  res.set_content(stloss.dump(), "application/json");
}

void StopLossHandler::del(const httplib::Request& req, httplib::Response& res)
{
    auto str_id = req.get_param_value("id");
    if (str_id.empty()) {
        return;
    }
    // TODO: check string is int

    int id = atoi(str_id.c_str());
    auto& config = _server->GetConfig();
    auto loss = config.GetStopLoss(id);
    res.status = 200;
    if (loss.empty()) {
        res.set_content("{status: 'OK'}", "application/json");
        return;
    }
    config.DeleteStopLoss(id);

    int type = loss["type"];
    auto sl = GetOrGenerate((StopLossType)type);
    List<symbol_t> dels;
    for (auto& item : loss["target"]) {
        String code = item["symbol"];
        dels.emplace_back(to_symbol(code));
    }
    sl->del(dels);

    res.set_content("{status: 'OK'}", "application/json");
}

void StopLossHandler::post(const httplib::Request& req, httplib::Response& res)
{
  nlohmann::json jsn = nlohmann::json::parse(req.body);
  auto& config = _server->GetConfig();
  auto new_id = config.AddStopLoss(jsn);

  auto email = jsn["email"].dump();
  int type = jsn["type"];
  auto sl = GetOrGenerate((StopLossType)type);
  for (auto& item: jsn["target"]) {
    StopLossInfo info;
    info._price = item["price"];
    switch ((StopLossType)type) {
      case StopLossType::Percentage:
        info._percent = item["percent"];
      break;
      default:
      break;
    }
    String code = item["symbol"];
    auto symbol = to_symbol(code);
    sl->add(symbol, info);
    if (_mail_map.count(symbol) && _mail_map[symbol] != email) {
      WARN("{} has email {} but current is {}", get_symbol(symbol), _mail_map[symbol], email);
      continue;
    }
    std::unique_lock<std::mutex> lock(_mutex);
    _mail_map[symbol] = email;
  }

  nlohmann::json response;
  response["id"] = new_id;
  res.set_content(response.dump(), "application/json");
}

IStopLoss* StopLossHandler::Switch(const String& name) {
  static const Map<String, StopLossType> mt{
    {"fix", StopLossType::Fix},
    {"percent", StopLossType::Percentage},
    {"move", StopLossType::Move},
    {"atr", StopLossType::ATR},
    {"sar", StopLossType::SAR},
    {"key", StopLossType::Key},
    {"step", StopLossType::Step},
    {"time", StopLossType::Time},
  };
  auto itr = mt.find(name);
  if (itr == mt.end() || itr->second == StopLossType::Percentage) {
    auto sl = _stloss_map[StopLossType::Percentage];
    if (!sl) {
    //   auto broker = _server->GetBroker();
    //   auto holds = broker->Holds();
    //   Map<String, double> org_price;
    //   for (auto& symbol: holds) {
    //     auto price = broker->LastPrice(symbol);
    //     org_price[symbol] = price;
    //   }
    //   // sl = new SLPercentage(org_price, 0.05);
    }
    return sl;
  }
  switch (itr->second)
  {
  default:
    WARN("not implement stop loss: {}", name);
    break;
  }
  return nullptr;
}

IStopLoss* StopLossHandler::GetOrGenerate(StopLossType type) {
  auto sl = _stloss_map[type];
  if (!sl) {
     switch(type) {
       case StopLossType::Percentage:
         sl = new SLPercentage();
         break;
       case StopLossType::Step:
         break;
       case StopLossType::ATR:
         sl = new ATR();
         break;
       default:
         sl = new SLPercentage();
         break;
     }
    _stloss_map[type] = sl;
  }
  return sl;
}

void StopLossHandler::run()
{
    if (!Subscribe(URI_RAW_QUOTE, _sock)) {
        return;
    }
    auto& config = _server->GetConfig();
    auto& stoplosses = config.GetAllStopLoss();
    for (auto& item : stoplosses) {
        String email = item["email"].dump();
        int type = item["type"];
        auto loss = GetOrGenerate((StopLossType)type);
        for (auto& target : item["target"]) {
            String symbol = target["symbol"];
            auto id = to_symbol(symbol);
            _mail_map[id] = email;

            StopLossInfo info;
            info._price = target["price"];
            switch ((StopLossType)type) {
            case StopLossType::Percentage:
                info._percent = target["percent"];
                break;
            default:
                break;
            }
            loss->add(id, info);
        }
    }

    constexpr std::size_t flags = yas::mem | yas::binary;
    while (!_exit) {
        char* buff = NULL;
        size_t sz = 0;
        int rv = nng_recv(_sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            continue;
        }
        yas::shared_buffer buf;
        buf.assign(buff, sz);
        QuoteInfo quote;
        yas::load<flags>(buf, quote);
        nng_free(buff, sz);

        for (auto& sl : _stloss_map) {
            List<symbol_t> sells;
            Map<symbol_t, QuoteInfo> tickers{{quote._symbol, quote}};
            sl.second->check(tickers, std::move(sells));

            if (!sells.empty()) {
                // TODO: sell symbols or send to porfolio mananger to make decision

                SendEmail(false, sells);
                // 
                sl.second->del(sells);
            }
        }
    }
    nng_close(_sock);
    _sock.id = 0;
}

bool StopLossHandler::SendEmail(bool buy_sell, const List<symbol_t>& symbols) {
    String prefix = "python mail.py ";
    prefix += _sender + " " + _pwd;
    String op = buy_sell ? "buy" : "sell";
    auto now = ToString(Now());

    Map<String, List<symbol_t>> same_dst_symbols;
    {
        std::unique_lock<std::mutex> lock(_mutex);
        for (auto& symbol : symbols) {
            auto itr = _mail_map.find(symbol);
            if (itr == _mail_map.end()) {
                WARN("{} not config email address.", get_symbol(symbol));
                continue;
            }
            same_dst_symbols[itr->second].push_back(symbol);
        }
    }

    bool ret = true;
    for (auto& item : same_dst_symbols) {
        auto syms = concat(item.second, ",");
        String msg = now + ": " + op + " " + syms;

        String cmd = prefix + " " + item.first + " \"" + msg + "\"";
        if (!RunCommand(cmd)) {
            ret = false;
        }
    }
    return ret;
}

void VaRHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    String start = params["start"];
    String end = params["end"];
    auto& portfolio = params["portfolio"];
    String strategy = portfolio["strategy"];
    auto& pool = portfolio["pool"];
    String data_type = portfolio["data_type"];
    DataFrequencyType dft = DataFrequencyType::Day;
    if (data_type == "5min") {

    }
    float confidence = params["confidence"];
    int N = params["days"];
    auto& commission = params["commission"];
    String type = commission["type"];
    double slip = commission["slip"];
    double min_price = commission["min_price"];
    double trade_price = commission["trade_price"];
    double tax = commission["tax"];
    double capital = params["capital"];

    //auto strategyInstance = _server->GetOrCreateStrategy(strategy);
    //if (!strategyInstance) {
    //    WARN("not implement strategy: {}", strategy);
    //    res.set_content("{}", "application/json");
    //    return;
    //}

    // auto broker = _server->GetVirtualSubSystem();
    //strategyInstance->Init(broker);
    // 数据准备
    // auto group = _server->PrepareStockData(pool, dft);
    //if (!group || !group->IsValid()) {
    //    res.status = 400;
    //}
    //else {
    //    res.status = 200;
    //    while (group->MoveNext()) {
    //        strategyInstance->Next(group.get());
    //    }
    //}
    
    nlohmann::json result;
    // auto id = broker->Statistic(confidence, N, group, result);
    // result["id"] = id;
    res.set_content(result.dump(), "application/json");
}

double VaRHandler::ExpectedShortFall() {
  return 0;
}

void VaRHandler::run() {

}

void VaRHandler::get(const httplib::Request& req, httplib::Response& res) {

}

void DrawdownHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    int type = params["range"];
    String symbol = params["symbol"];
}

void VolatilityHandler::get(const httplib::Request& req, httplib::Response& res) {

}
