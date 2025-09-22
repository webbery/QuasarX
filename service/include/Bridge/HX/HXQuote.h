#pragma once
#include "std_header.h"
#include "hx/TORATstpXMdApi.h"
#include <nng/nng.h>
#include <mutex>
#include "Util/system.h"

class HXQuateSpi: public TORALEV1API::CTORATstpXMdSpi {
public:
    HXQuateSpi(TORALEV1API::CTORATstpXMdApi* api);
    ~HXQuateSpi();

    bool Init();

    virtual void OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField *pMarketDataField);

    virtual void OnFrontConnected();

    virtual void OnFrontDisconnected(int nReason);
private:
  nng_socket _sock;
  std::mutex _mutex;
  Map<symbol_t, QuoteInfo> _tickers;
};
