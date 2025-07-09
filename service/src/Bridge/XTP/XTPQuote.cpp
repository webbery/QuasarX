#include "Bridge/XTP/XTPQuote.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "xtp/xtp_quote_api.h"
#include <cstdio>
#include <cstring>
#include <nng/protocol/pubsub0/pub.h>
#include "Bridge/exchange.h"
#include "yas/detail/type_traits/flags.hpp"
#include "Util/datetime.h"
#include "Util/system.h"

static constexpr std::size_t flags = yas::mem|yas::binary;
XTPQuote::XTPQuote(QuoteApi* api): _is_all(false) {
  sock.id = 0;
}

XTPQuote::~XTPQuote() {
  if (sock.id != 0) {
    nng_close(sock);
  }
  // for (auto& item : _tickers) {
  //   if (item.second)
  //     delete item.second;
  // }
  _tickers.clear();
}

bool XTPQuote::Init() {
  return Publish(URI_RAW_QUOTE, sock);
}

void XTPQuote::OnDisconnected(int reason) {
  INFO("XTP Disconnect: {}", reason);
}

void XTPQuote::OnError(XTPRI *error_info) {
  WARN("XTP Error: {}", error_info->error_msg);
}

void XTPQuote::OnTickByTickLossRange(int begin_seq, int end_seq) {}

void XTPQuote::OnUnSubMarketData(XTPST *ticker, XTPRI *error_info, bool is_last) {}

void XTPQuote::OnDepthMarketData(XTPMD *market_data, int64_t bid1_qty[], int32_t bid1_count, int32_t max_bid1_count, int64_t ask1_qty[], int32_t ask1_count, int32_t max_ask1_count) {
  QuoteInfo& info = _tickers[market_data->ticker];
  {
    std::unique_lock<std::mutex> lock(_mutex);
    info._time = FromStr(std::to_string(market_data->data_time/1000), "%Y%m%d%H%M%S");
    switch (market_data->exchange_id) {
    case XTP_EXCHANGE_SH:
      info._symbol = to_symbol(market_data->ticker, "SH");
      break;
    case XTP_EXCHANGE_SZ:
      info._symbol = to_symbol(market_data->ticker, "SZ");
      break;
    default:
      info._symbol = to_symbol(market_data->ticker);
      break;
    }
    info._open = market_data->open_price;
    info._close = market_data->last_price;
    info._volumn = market_data->qty;
    info._value = market_data->turnover;
    info._high = market_data->high_price;
    info._low = market_data->low_price;
    for (int i = 0; i < 5; ++i) {
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
  if (0 != nng_send(sock, buf.data.get(), buf.size, 0)) {
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

QuoteInfo XTPQuote::GetQuoteInfo(const String& symbol)
{
  std::unique_lock<std::mutex> lock(_mutex);
  return _tickers[symbol];
}

void XTPQuote::AddAndUpdateTicker(XTPQFI* ticker_info) {
  // auto itr = _tickers.find(ticker_info->ticker);
  // if (itr == _tickers.end()) {
    auto symbol = format_symbol(ticker_info->ticker);
    std::unique_lock<std::mutex> lock(_mutex);
    QuoteInfo& info = _tickers[symbol];
    switch (ticker_info->exchange_id) {
    case XTP_EXCHANGE_SH:
      info._symbol = to_symbol(ticker_info->ticker, "SH");
      break;
    case XTP_EXCHANGE_SZ:
      info._symbol = to_symbol(ticker_info->ticker, "SZ");
      break;
    default:
      info._symbol = to_symbol(ticker_info->ticker);
      break;
    }
    info._time = Now();
    info._open = ticker_info->pre_close_price;
    info._close = ticker_info->pre_close_price;
  // }
  // else {

  // }
}

Set<String> XTPQuote::GetAllSymbols() {
  Set<String> all_symbols;
  for (auto& item: _tickers) {
    all_symbols.insert(item.first);
  }
  return all_symbols;
}
