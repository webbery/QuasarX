#include "Bridge/HX/HXExchange.h"
#include "Util/system.h"
#include "hx/TORATstpXMdApi.h"
#include "hx/TORATstpTraderApi.h"
#include "Bridge/HX/HXQuote.h"
#include "Bridge/HX/HXTrade.h"
#include <cstring>
#include "server.h"
#include "Util/string_algorithm.h"

using namespace TORALEV1API;
#define USER_PRODUCT_INFO "HX5ZWWQ4VI"

HXExchange::HXExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _quoteAPI(nullptr), _tradeAPI(nullptr), _trade(nullptr)
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
    _user = handle._username;
    _pwd = handle._passwd;

    _quoteAPI = CTORATstpXMdApi::CreateTstpXMdApi();
    _quote = new HXQuateSpi(_quoteAPI, this);

    _quoteAPI->RegisterSpi(_quote);
    String quote_ip("tcp://");
    quote_ip += std::string(handle._quote_addr) + ":" + std::to_string(handle._quote_port);
    _quoteAPI->RegisterFront((char*)quote_ip.c_str());
    _quoteAPI->Init();
    
    using namespace TORASTOCKAPI;
    _tradeAPI = CTORATstpTraderApi::CreateTstpTraderApi();
    _trade = new HXTrade(this);
    _tradeAPI->RegisterSpi(_trade);
    String trade_ip("tcp://");
    trade_ip += std::string(handle._trade_addr) + ":" + std::to_string(handle._trade_port);
    _tradeAPI->RegisterFront((char*)trade_ip.c_str());
    _tradeAPI->Init();
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
    strncpy(req_user_login_field.LogInAccount, _user.c_str(), _user.size());
    strncpy(req_user_login_field.Password, _pwd.c_str(), _pwd.size());
    memcpy(req_user_login_field.UserProductInfo, USER_PRODUCT_INFO, strlen(USER_PRODUCT_INFO));
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

order_id HXExchange::AddOrder(const symbol_t& symbol, OrderContext* ctx){
    order_id oid{++_reqID};
    using namespace TORASTOCKAPI;
    CTORATstpInputOrderField* order = new CTORATstpInputOrderField;
    auto& o = ctx->_order;
    order->Direction = o._side;
    order->UserRequestID = oid._id;
    auto strCode = format_symbol(std::to_string(symbol._symbol));
    strncpy(order->SecurityID, strCode.c_str(), strCode.size());
    switch (symbol._exchange) {
        case MT_Shanghai: order->ExchangeID = TORALEV1API::TORA_TSTP_EXD_SSE; break;
        case MT_Shenzhen: order->ExchangeID = TORALEV1API::TORA_TSTP_EXD_SZSE; break;
        case MT_Beijing: order->ExchangeID = TORALEV1API::TORA_TSTP_EXD_BSE; break;
        case MT_Hongkong: order->ExchangeID = TORALEV1API::TORA_TSTP_EXD_HK; break;
        default:
            return order_id{0};
    }

    _tradeAPI->ReqOrderInsert(order, oid._id);

    ctx->_symbol = symbol;
    auto pr = std::make_pair(order, ctx);
    _orders.emplace(oid, std::move(pr));
    return oid;
}

void HXExchange::OnOrderReport(order_id id, const TradeReport& report){
    assert(_orders.count(id._id) != 0);

    _orders.visit(id._id, [&report](concurrent_order_map::value_type& value) {
        auto ctx = value.second.second;
        ctx->_trades._reports.emplace_back(report);
        ctx->Update(report);
      });
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
            case MT_Hongkong: type = TORA_TSTP_EXD_HK; break;
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
                strncpy(subscribe_array[i], item.second[i].c_str(), item.second[i].size());
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
        case MT_Hongkong: type = TORA_TSTP_EXD_HK; break;
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

