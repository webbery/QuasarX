#include "Bridge/HX/HXOptionTrade.h"
#include "Util/datetime.h"
#include "Util/log.h"

HXOptionTrade::HXOptionTrade(HXExchange* exchange): _exchange(exchange) {

}

void HXOptionTrade::OnFrontConnected()
{
    INFO("HX Option Connected.");
}

void HXOptionTrade::OnFrontDisconnected(int nReason) {
    INFO("HX Option Disconnected.");

}
