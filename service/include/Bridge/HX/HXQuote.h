#pragma once
#include "Bridge/exchange.h"
#include "std_header.h"
#include "hx/TORATstpXMdApi.h"
#include <nng/nng.h>
#include <shared_mutex>
#include "Util/system.h"

class HXExchange;
class HXQuateSpi: public TORALEV1API::CTORATstpXMdSpi {
public:
    HXQuateSpi(TORALEV1API::CTORATstpXMdApi* api, HXExchange* exchange);
    ~HXQuateSpi();

    bool Init();

    virtual void OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField *pMarketDataField);

    virtual void OnFrontConnected();

    virtual void OnFrontDisconnected(int nReason);

    QuoteInfo GetQuote(symbol_t symbol);
private:
  bool _isInited;

  HXExchange* _exchange;
  
  nng_socket _sock;
  std::shared_mutex _mutex;
  Map<symbol_t, QuoteInfo> _tickers;
};
