#pragma once
#include "hx/TORATstpSPTraderApi.h"

class HXExchange;
class HXOptionTrade : public TORASPAPI::CTORATstpSPTraderSpi{
public:
    HXOptionTrade(HXExchange* exchange);
};
