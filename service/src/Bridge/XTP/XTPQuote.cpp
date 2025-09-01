#include "Bridge/XTP/XTPQuote.h"
#include "Util/string_algorithm.h"
#include "xtp/xtp_quote_api.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <nng/protocol/pubsub0/pub.h>
#include "Bridge/exchange.h"
#include "yas/detail/type_traits/flags.hpp"
#include "Util/datetime.h"
#include "Util/system.h"
#include "Bridge/XTP/XTPExchange.h"

XTPQuote::XTPQuote(QuoteApi* api, XTPExchange* exchange): _is_all(false), _exchange(exchange) {
  _sock.id = 0;
}

XTPQuote::~XTPQuote() {
  if (_sock.id != 0) {
    nng_close(_sock);
  }
  // for (auto& item : _tickers) {
  //   if (item.second)
  //     delete item.second;
  // }
  _tickers.clear();
}

bool XTPQuote::Init() {
  return Publish(URI_RAW_QUOTE, _sock);
}

void XTPQuote::OnDisconnected(int reason) {
  INFO("XTP Disconnect: {}", reason);
  _exchange->_login_status = false;
  _exchange->Login();
}

void XTPQuote::OnError(XTPRI *error_info) {
  WARN("XTP Error: {}", error_info->error_msg);
}

void XTPQuote::OnTickByTickLossRange(int begin_seq, int end_seq) {}

void XTPQuote::OnUnSubMarketData(XTPST *ticker, XTPRI *error_info, bool is_last) {}

void XTPQuote::OnDepthMarketData(XTPMD *market_data, int64_t bid1_qty[], int32_t bid1_count, int32_t max_bid1_count, int64_t ask1_qty[], int32_t ask1_count, int32_t max_ask1_count) {
  auto status = market_data->ticker_status;
  if (status[1] == '0') {
    return;
  }

  symbol_t symb;
  // if (strcmp("001318", market_data->ticker) == 0) {
  //   printf("recieve \n");
  // }

  if (XTP_MARKETDATA_V2_INDEX == market_data->data_type_v2) {
    // 指数
  }
  else if (XTP_MARKETDATA_V2_ACTUAL == market_data->data_type_v2) {
    // 
  }
  switch (market_data->exchange_id) {
  case XTP_EXCHANGE_SH:
      if (status[2] == '0')
        return;

      symb = to_symbol(market_data->ticker, "SH");
    break;
  case XTP_EXCHANGE_SZ:
    symb = to_symbol(market_data->ticker, "SZ");
    break;
  default:
    symb = to_symbol(market_data->ticker);
    break;
  }
  {
    // std::unique_lock<std::mutex> lck(_lmt_mtx);
    // auto itr = _price_limits.find(symb);
    // if (itr == _price_limits.end())
    //   return;

    // auto& limit = itr->second;
    // // 过滤非法数据
    // if (limit.first >= market_data->last_price || limit.second <= market_data->last_price)
    //   return;
  }
  
  QuoteInfo& info = _tickers[symb];
  {
    std::unique_lock<std::mutex> lock(_mutex);
    info._time = FromStr(std::to_string(market_data->data_time/1000), "%Y%m%d%H%M%S");
    info._symbol = symb;
    info._open = market_data->open_price;
    info._close = market_data->last_price;
    info._volume = market_data->qty;
    info._value = market_data->turnover;
    info._high = market_data->high_price;
    info._low = market_data->low_price;
    for (int i = 0; i < 10; ++i) {
      info._bidPrice[i] = market_data->bid[i];
      info._askPrice[i] = market_data->ask[i];
      info._bidVolume[i] = market_data->bid_qty[i];
      info._askVolume[i] = market_data->ask_qty[i];
    }
    // if (strcmp(market_data->ticker, "000001") == 0) {
    //   LOG("XTP update");
    // }
  }
  yas::shared_buffer buf = yas::save<flags>(info);
  if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
    printf("send quote message fail.\n");
    return;
  }
}

void XTPQuote::OnQueryAllTickersFullInfo(XTPQFI* ticker_info, XTPRI *error_info, bool is_last) {
  AddAndUpdateTicker(ticker_info);
  if (is_last) {
    _is_all = true;
    SetCurrentThreadName("XTPQuote");
  }
}

QuoteInfo XTPQuote::GetQuoteInfo(symbol_t symbol)
{
  std::unique_lock<std::mutex> lock(_mutex);
  return _tickers[symbol];
}

void XTPQuote::AddAndUpdateTicker(XTPQFI* ticker_info) {
  // auto itr = _tickers.find(ticker_info->ticker);
  // if (itr == _tickers.end()) {
    auto str_symbol = format_symbol(ticker_info->ticker);
    symbol_t symbol;
    memset(&symbol, 0, sizeof(symbol_t));
    switch (ticker_info->exchange_id) {
    case XTP_EXCHANGE_SH:
      symbol = to_symbol(ticker_info->ticker, "SH");
      break;
    case XTP_EXCHANGE_SZ:
      symbol = to_symbol(ticker_info->ticker, "SZ");
      break;
    default:
      symbol = to_symbol(ticker_info->ticker);
      break;
    }
    if (symbol._symbol == 0)
      return;

    std::unique_lock<std::mutex> lock(_mutex);
    QuoteInfo& info = _tickers[symbol];
    info._symbol = symbol;
    info._time = Now();
    info._open = ticker_info->pre_close_price;
    info._close = ticker_info->pre_close_price;
    // info._volume = ticker_info->
    {
      std::unique_lock<std::mutex> lck(_lmt_mtx);
      _price_limits[symbol].first = ticker_info->lower_limit_price;
      _price_limits[symbol].second = ticker_info->upper_limit_price;
    }
    
    yas::shared_buffer buf = yas::save<flags>(info);
    if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
      printf("send quote message fail.\n");
      return;
    }
}

Set<symbol_t> XTPQuote::GetAllSymbols() {
  Set<symbol_t> all_symbols;
  for (auto& item: _tickers) {
    all_symbols.insert(item.first);
  }
  return all_symbols;
}

void XTPQuote::OnSubMarketData(XTPST *ticker, XTPRI *error_info, bool is_last) {
  if (error_info && error_info->error_id != 0) {
    WARN("XTP OnSubMarketData[{}]: {} {}", ticker->ticker, error_info->error_id, error_info->error_msg);
  }
}
