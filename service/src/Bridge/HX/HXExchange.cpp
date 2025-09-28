#include "Bridge/HX/HXExchange.h"
#include "hx/TORATstpXMdApi.h"
#include "Bridge/HX/HXQuote.h"
#include <cstring>
#include "server.h"

using namespace TORALEV1API;

HXExchange::HXExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _quoteAPI(nullptr)
, _login_status(false), _quote_inited(false), _requested(false) {
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
    _quote = new HXQuateSpi(_quoteAPI, this);

    _quoteAPI->RegisterSpi(_quote);
    String quote_ip("tcp://");
    quote_ip += std::string(handle._quote_addr) + ":" + std::to_string(handle._quote_port);
    _quoteAPI->RegisterFront((char*)quote_ip.c_str());
    _quoteAPI->Init();
    
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
    if (IsLogin())
        return true;

    bool status = _quote->Init();

    CTORATstpReqUserLoginField req_user_login_field;
    memset(&req_user_login_field, 0, sizeof(req_user_login_field));
    int ret = _quoteAPI->ReqUserLogin(&req_user_login_field, 0);
    if (ret != 0) {
        INFO("HX login fail.");
        return false;
    }
    if (status) {
        _login_status = true;
    }
    return true;
}
bool HXExchange::IsLogin(){
    return _login_status;
}

AccountPosition HXExchange::GetPosition(){
    AccountPosition ap;
    return ap;
}

AccountAsset HXExchange::GetAsset(){
    AccountAsset aa;
    return aa;
}

order_id HXExchange::AddOrder(const symbol_t& symbol, OrderContext* order){
    order_id oi;
    return oi;
}

void HXExchange::OnOrderReport(order_id id, const TradeReport& report){
}

bool HXExchange::CancelOrder(order_id id){
    return true;
}
// 获取当前尚未完成的所有订单
OrderList HXExchange::GetOrders(){
    OrderList ol;
    return ol;
}

void HXExchange::QueryQuotes(){
    // 订阅行情
    if (_filter._symbols.empty()) {

    } else {
        if (_quote_inited)
            return;
        _quote_inited = true;
        Map<char, Vector<String>> subscribe_map;
        for (auto symb: _filter._symbols) {
            auto symbol = to_symbol(symb);
            char type = 0;
            switch (symbol._exchange) {
            case MT_Shanghai: type = TORA_TSTP_EXD_SSE; break;
            case MT_Shenzhen: type = TORA_TSTP_EXD_SZSE; break;
            case MT_Beijing: type = TORA_TSTP_EXD_BSE; break;
            default:
                break;
            }
            if (type == 0) {
                WARN("unsupport exchange {}", (int)symbol._exchange);
                continue;
            }
            subscribe_map[type].emplace_back(symb);
        }
        // 补充指数
        subscribe_map['1'].emplace_back("000001");
        int ret = -1;
        for (auto& item : subscribe_map) {
            char** subscribe_array = new char* [item.second.size()];
            for (int i = 0; i < item.second.size(); ++i) {
                subscribe_array[i] = new char[item.second[i].size() + 1] {0};
                strcmp(subscribe_array[i], item.second[i].c_str());
            }
            ret = _quoteAPI->SubscribeMarketData(subscribe_array, item.second.size(), item.first);
            for (int j = 0; j < item.second.size(); ++j) {
                delete[] subscribe_array[j];
            }
            delete[] subscribe_array;
            if (ret != 0)
            {
                WARN("SubscribeMarketData {} fail, ret{}", item.second, ret);
                _quote_inited = false;
                continue;
            } else {
                LOG("SubscribeMarketData from symbol {}", item.second);
            }
        }
        _requested = true;
    }
}

void HXExchange::StopQuery(){
    if (!_requested)
        return;

    _requested = false;
    Map<char, Vector<String>> subscribe_map;
    for (auto symb : _filter._symbols) {
        auto symbol = to_symbol(symb);
        char type = 0;
        switch (symbol._exchange) {
        case MT_Shanghai: type = TORA_TSTP_EXD_SSE; break;
        case MT_Shenzhen: type = TORA_TSTP_EXD_SZSE; break;
        case MT_Beijing: type = TORA_TSTP_EXD_BSE; break;
        default:
            break;
        }
        if (type == 0) {
            WARN("unsupport exchange {}", (int)symbol._exchange);
            continue;
        }
        subscribe_map[type].emplace_back(symb);
    }
    for (auto& item : subscribe_map) {
        char** subscribe_array = new char* [item.second.size()];
        for (int i = 0; i < item.second.size(); ++i) {
            subscribe_array[i] = new char[item.second[i].size() + 1] {0};
            strcmp(subscribe_array[i], item.second[i].c_str());
        }
        int ret = _quoteAPI->UnSubscribeMarketData(subscribe_array, item.second.size(), item.first);
        for (int j = 0; j < item.second.size(); ++j) {
            delete[] subscribe_array[j];
        }
        delete[] subscribe_array;
        if (ret != 0)
        {
            WARN("UnSubscribeMarketData fail, ret{}", ret);
            continue;
        }
    }
}

QuoteInfo HXExchange::GetQuote(symbol_t symbol){
    QuoteInfo info = _quote->GetQuote(symbol);
    return info;
}