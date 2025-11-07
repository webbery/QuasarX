#pragma once
#include "hx/TORATstpSPTraderApi.h"

class HXExchange;
class HXOptionTrade : public TORASPAPI::CTORATstpSPTraderSpi{
public:
    HXOptionTrade(HXExchange* exchange);

    virtual void OnFrontDisconnected(int nReason);

private:
    HXExchange* _exchange;
};
