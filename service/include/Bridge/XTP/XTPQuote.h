#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "xtp/xtp_quote_api.h"
#include <map>
#include <string>
#include <atomic>
#include <nng/nng.h>
#include <mutex>

using namespace XTP::API;
namespace XTP {
  namespace API {
    class QuoteApi;
  }
}

class XTPExchange;

class XTPQuote :public XTP::API::QuoteSpi {
public:
  XTPQuote(QuoteApi* api, XTPExchange* exchange);

  bool Init();

  virtual ~XTPQuote();

  QuoteInfo* GetTickerInfo(const std::string& symbol);

  virtual void OnDisconnected(int reason);

  virtual void OnError(XTPRI *error_info);

  virtual void OnTickByTickLossRange(int begin_seq, int end_seq);

  virtual void OnUnSubMarketData(XTPST *ticker, XTPRI *error_info, bool is_last);

  virtual void OnSubMarketData(XTPST *ticker, XTPRI *error_info, bool is_last);

  virtual void OnDepthMarketData(XTPMD *market_data, int64_t bid1_qty[], int32_t bid1_count, int32_t max_bid1_count, int64_t ask1_qty[], int32_t ask1_count, int32_t max_ask1_count);

  virtual void OnQueryAllTickersFullInfo(XTPQFI* ticker_info, XTPRI *error_info, bool is_last);

  QuoteInfo GetQuoteInfo(symbol_t symbol);

  bool IsAllTickCapture() {return _is_all;}

  Set<symbol_t> GetAllSymbols();
private:
  void AddAndUpdateTicker(XTPQFI* ticker_info);

private:
  XTPExchange* _exchange;
  
  nng_socket _sock;
  bool _is_all;

  std::mutex _mutex;
  std::map<symbol_t, QuoteInfo> _tickers;

  std::mutex _lmt_mtx;
  Map<symbol_t, Pair<double, double>> _price_limits;
};