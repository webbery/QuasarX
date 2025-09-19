#include "Bridge/HX/HXExchange.h"
#include "hx/TORATstpXMdApi.h"
#include "Bridge/HX/HXQuote.h"
#include <cstring>

using namespace TORALEV1API;

HXExchange::HXExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _quoteAPI(nullptr) {
}
HXExchange::~HXExchange(){
    if (_quote) {
        delete _quote;
    }
}

const char* HXExchange::Name(){
    return "HX";
}

bool HXExchange::Init(const ExchangeInfo& handle){
    _quoteAPI = CTORATstpXMdApi::CreateTstpXMdApi();
    _quote = new HXQuateSpi(_quoteAPI);

    _quoteAPI->RegisterSpi(_quote);
    _quoteAPI->RegisterFront("tcp://210.14.72.16:9402");
    _quoteAPI->Init();

    CTORATstpReqUserLoginField req_user_login_field;
    memset(&req_user_login_field, 0, sizeof(req_user_login_field));
    int ret = _quoteAPI->ReqUserLogin(&req_user_login_field, 0);
    if (ret != 0) {
        INFO("HX login fail.");
        return false;
    }
    return true;
}

void HXExchange::SetFilter(const QuoteFilter& filter){
    _filter = filter;
}

bool HXExchange::Release(){
    if (_quote) {
        delete _quote;
        _quote = nullptr;
    }
    return true;
}

bool HXExchange::Login(){
    return true;
}
bool HXExchange::IsLogin(){
    return true;
}

AccountPosition HXExchange::GetPosition(){
}

AccountAsset HXExchange::GetAsset(){
}

order_id HXExchange::AddOrder(const symbol_t& symbol, OrderContext* order){
}

void HXExchange::OnOrderReport(order_id id, const TradeReport& report){
}

bool HXExchange::CancelOrder(order_id id){
    return true;
}
// 获取当前尚未完成的所有订单
OrderList HXExchange::GetOrders(){
}

void HXExchange::QueryQuotes(){
    // 订阅行情
    if (_filter._symbols.empty()) {

    } else {
        char **subscribe_array = new char*[_filter._symbols.size()];
        int i = 0;
        for (auto symbol: _filter._symbols) {
            subscribe_array[i] = new char[symbol.size() + 1];
            strcpy(subscribe_array[i], symbol.c_str());
            ++i;
        }
        int ret = _quoteAPI->SubscribeMarketData(subscribe_array, _filter._symbols.size(), TORA_TSTP_EXD_SSE);
        for (int j = 0; j < _filter._symbols.size(); ++j) {
            delete[] subscribe_array[j];
        }
        delete[] subscribe_array;
        if (ret != 0)
        {
            WARN("SubscribeMarketData fail, ret{}", ret);
            return;
        }
    }
}

void HXExchange::StopQuery(){
}

QuoteInfo HXExchange::GetQuote(symbol_t symbol){
}