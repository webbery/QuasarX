#include "Bridge/HX/HXExchange.h"
#include "Util/system.h"
#include "hx/TORATstpXMdApi.h"
#include "hx/TORATstpTraderApi.h"
#include "hx/TORATstpSPTraderApi.h"
#include "Bridge/HX/HXQuote.h"
#include "Bridge/HX/HXTrade.h"
#include "Bridge/HX/HXOptionTrade.h"
#include <cstring>
#include <future>
#include "server.h"
#include "Util/string_algorithm.h"
#include "Bridge/ETFOptionSymbol.h"
#include "Bridge/OrderLimit.h"
#include "BrokerSubSystem.h"

using namespace TORALEV1API;
#define USER_PRODUCT_INFO "HX5ZWWQ4VI"

namespace {
    // 转交易所类型
    char toExchangeID(ExchangeName name) {
        switch (name) {
        case MT_Shanghai: return TORALEV1API::TORA_TSTP_EXD_SSE;
        case MT_Shenzhen: return TORALEV1API::TORA_TSTP_EXD_SZSE;
        case MT_Beijing: return TORALEV1API::TORA_TSTP_EXD_BSE;
        case MT_Hongkong: return TORALEV1API::TORA_TSTP_EXD_HK;
        default:
            return 0;
        }
    }

    // 转订单类型
    char toPriceType(OrderType type) {
        switch (type)
        {
        case OrderType::Market:
            return TORASTOCKAPI::TORA_TSTP_OPT_AnyPrice;
        case OrderType::Limit:
            return TORASTOCKAPI::TORA_TSTP_OPT_LimitPrice;
        case OrderType::Condition:
            return TORASTOCKAPI::TORA_TSTP_OPT_LimitPrice;
        default:
            return TORASTOCKAPI::TORA_TSTP_OPT_AnyPrice;
        }
    }

    char toValidTime(OrderTimeValid otv) {
        switch (otv)
        {
        case OrderTimeValid::Today:
            return TORASTOCKAPI::TORA_TSTP_TC_GFD;
        case OrderTimeValid::Future:
            return TORASTOCKAPI::TORA_TSTP_TC_GTD;
        default:
            return TORASTOCKAPI::TORA_TSTP_TC_GFD;
        }
    }

    void convertOrderType(const OrderContext& ctx, TORASTOCKAPI::CTORATstpInputOrderField& order) {
        auto exchange = (ExchangeName)ctx._order._symbol._exchange;
        switch (ctx._order._type) {
        case OrderType::Market: // 市价单,以对手方最优价格操作,抢筹速度最快
            if (exchange == ExchangeName::MT_Shanghai || exchange == ExchangeName::MT_Shenzhen ||exchange == ExchangeName::MT_Beijing) {
                order.OrderPriceType = TORASTOCKAPI::TORA_TSTP_OPT_BestPrice;
                order.TimeCondition = TORASTOCKAPI::TORA_TSTP_TC_GFD;
                order.VolumeCondition = TORASTOCKAPI::TORA_TSTP_VC_AV;
            }
            else {
                WARN("not support order type: {}", ctx._order._symbol);
            }
            break;
        case OrderType::Limit:  // 限价单
            if (exchange == ExchangeName::MT_Shanghai || exchange == ExchangeName::MT_Shenzhen || exchange == ExchangeName::MT_Beijing) {
                order.OrderPriceType = TORASTOCKAPI::TORA_TSTP_OPT_LimitPrice;
                order.TimeCondition = TORASTOCKAPI::TORA_TSTP_TC_GFD;
                order.VolumeCondition = TORASTOCKAPI::TORA_TSTP_VC_AV;
            }
            else {
                WARN("not support order type: {}", ctx._order._symbol);
            }
            break;
        default:
            WARN("not support order type: {}", ctx._order._symbol);
            break;
        }
        
    }

    template<typename T>
    bool getFuture(std::shared_ptr<std::promise<T>> prom, std::future<T>& fut) {
        try{
            fut = prom->get_future();
            auto status = fut.wait_for(std::chrono::seconds(10));
            if (status == std::future_status::timeout) {
                return false;
            }
        } catch (...) {
            return false;
        }
        return true;
    }
}
HXExchange::HXExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _quoteAPI(nullptr)
, _login_status(false), _quote_inited(false), _requested(false), _reqID(0)
, _trader_login(false), _quote_login(false), _current(0) {
    _optionHandle._trade = nullptr;
    _optionHandle._tradeAPI = nullptr;
    _stockHandle._trade = nullptr;
    _stockHandle._tradeAPI = nullptr;
}

HXExchange::~HXExchange(){
    if (_quote) {
        delete _quote;
    }
}

const char* HXExchange::Name(){
    return "HX";
}

void HXExchange::addPromise(uint64_t reqID, std::shared_ptr<void> promise) {
    std::lock_guard<std::mutex> lock(_mutex);
    _promises.emplace(reqID, promise);
}

bool HXExchange::InitStockHandle()
{

    return true;
}

bool HXExchange::InitQuote() {
    _quoteAPI->RegisterSpi(_quote);
    String quote_ip("tcp://");
    quote_ip += std::string(_brokerInfo._quote_addr) + ":" + std::to_string(_brokerInfo._quote_port);
    _quoteAPI->RegisterFront((char*)quote_ip.c_str());
    _quoteAPI->Init();
    return true;
}

bool HXExchange::InitStockTrade() {
    _stockHandle._tradeAPI->RegisterSpi(_stockHandle._trade);
    String trade_ip("tcp://");
    trade_ip += std::string(_brokerInfo._trade_addr) + ":" + std::to_string(_brokerInfo._trade_port);
    _stockHandle._tradeAPI->RegisterFront((char*)trade_ip.c_str());
    _stockHandle._tradeAPI->SubscribePrivateTopic(TORASTOCKAPI::TORA_TERT_QUICK);
    _stockHandle._tradeAPI->SubscribePublicTopic(TORASTOCKAPI::TORA_TERT_RESTART);
    _stockHandle._tradeAPI->Init();
    return true;
}

bool HXExchange::InitOptionTrade() {
    _optionHandle._tradeAPI->RegisterSpi(_optionHandle._trade);
    String trade_ip("tcp://");
    trade_ip += std::string(_brokerInfo._trade_addr) + ":" + std::to_string(_brokerInfo._trade_port);
    _optionHandle._tradeAPI->RegisterFront((char*)trade_ip.c_str());
    _optionHandle._tradeAPI->SubscribePrivateTopic(TORASPAPI::TORA_TERT_QUICK);
    _optionHandle._tradeAPI->SubscribePublicTopic(TORASPAPI::TORA_TERT_RESTART);
    _optionHandle._tradeAPI->Init();
    return true;
}

bool HXExchange::InitOptionHandle()
{
    return true;
}

order_id HXExchange::AddStockOrder(const symbol_t& symbol, OrderContext* ctx)
{
    order_id oid;
    String& shareholder = _shareholder[symbol._exchange];
    if (shareholder.empty()) {
        WARN("shareholder is empty");
        return oid;
    }

    oid._id = ++_reqID;
    using namespace TORASTOCKAPI;
    CTORATstpInputOrderField order;
    memset(&order, 0, sizeof(CTORATstpInputOrderField));

    auto& o = ctx->_order;
    order.OrderRef = oid._id;
    order.UserRequestID = oid._id;

    strcpy(order.ShareholderID, shareholder.c_str());
    auto strCode = format_symbol(std::to_string(symbol._symbol));
    strncpy(order.SecurityID, strCode.c_str(), strCode.size());
    order.ExchangeID = toExchangeID((ExchangeName)symbol._exchange);
    order.VolumeTotalOriginal = ctx->_order._volume;
    order.LimitPrice = o._order[0]._price;

    order.ForceCloseReason = TORA_TSTP_FCC_NotForceClose;
    order.Direction = (o._side == 0 ? TORA_TSTP_D_Buy : TORA_TSTP_D_Sell);
    convertOrderType(*ctx, order);

    _stockHandle._tradeAPI->ReqOrderInsert(&order, oid._id);

    ctx->_order._symbol = symbol;
    ctx->_order._id = oid._id;
    //auto pr = std::make_pair(order, ctx);
    _orders.emplace(oid._id, ctx);
    strcpy(oid._sysID, ctx->_order._sysID.c_str());
    return oid;
}

order_id HXExchange::AddOptionOrder(const symbol_t& symbol, OrderContext* ctx)
{
    order_id oid;
    String& shareholder = _shareholder[symbol._exchange];
    if (shareholder.empty()) {
        WARN("shareholder is empty");
        return oid;
    }
    auto strCode = get_etf_option_code(symbol);
    if (strCode.empty()) {
        WARN("can't find option code {}", ETFOptionSymbol(symbol).name());
        return oid;
    }

    oid._id = ++_reqID;
    TORASPAPI::CTORATstpSPInputOrderField pInputOrderField;
    memset(&pInputOrderField, 0, sizeof(TORASPAPI::CTORATstpSPInputOrderField));
    auto& o = ctx->_order;
    pInputOrderField.OrderRef = oid._id;

    strcpy(pInputOrderField.ShareholderID, shareholder.c_str());
    strncpy(pInputOrderField.SecurityID, strCode.c_str(), strCode.size());
    pInputOrderField.ExchangeID = toExchangeID((ExchangeName)symbol._exchange);
    pInputOrderField.VolumeTotalOriginal = ctx->_order._volume;
    pInputOrderField.LimitPrice = o._order[0]._price;

    pInputOrderField.ForceCloseReason = TORASPAPI::TORA_TSTP_SP_FCC_NotForceClose;
    pInputOrderField.Direction = (ctx->_order._side == 0 ? TORASPAPI::TORA_TSTP_SP_D_Buy : TORASPAPI::TORA_TSTP_SP_D_Sell);
    
    // 构建订单类型

    _optionHandle._tradeAPI->ReqOrderInsert(&pInputOrderField, oid._id);
    return oid;
}

void HXExchange::SubscribeStockQuote(const Map<char, Vector<String>>& stocks)
{
    int ret = -1;
    for (auto& item : stocks) {
        char** subscribe_array = new char* [item.second.size()];
        for (int i = 0; i < item.second.size(); ++i) {
            subscribe_array[i] = new char[item.second[i].size() + 1] {0};
            strncpy(subscribe_array[i], item.second[i].c_str(), item.second[i].size());
        }
        // 非交易时段无数据
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
        }
        else {
            LOG("SubscribeMarketData from symbol {}", item.second);
        }
    }
}

void HXExchange::SubscribeOptionQuote(const Map<char, Vector<String>>& options)
{
    for (auto& item : options) {
        char** subscribe_array = new char* [item.second.size()];
        for (int i = 0; i < item.second.size(); ++i) {
            subscribe_array[i] = new char[item.second[i].size() + 1] {0};
            strncpy(subscribe_array[i], item.second[i].c_str(), item.second[i].size());
        }
        // 非交易时段无数据
        int ret = _quoteAPI->SubscribeSPMarketData(subscribe_array, item.second.size(), item.first);
        for (int j = 0; j < item.second.size(); ++j) {
            delete[] subscribe_array[j];
        }
        delete[] subscribe_array;
        if (ret != 0)
        {
            WARN("SubscribeSPMarketData {} fail, ret{}", item.second, ret);
            _quote_inited = false;
            continue;
        }
        else {
            LOG("SubscribeSPMarketData from symbol {}", item.second);
        }
    }
}

bool HXExchange::QueryStockOrders(uint64_t reqID)
{
    TORASTOCKAPI::CTORATstpQryOrderField qry_orders;
    memset(&qry_orders, 0, sizeof(qry_orders));
    strcpy(qry_orders.SecurityID, "");
    strcpy(qry_orders.ShareholderID, "");
    strcpy(qry_orders.OrderSysID, "");
    // "09:36:00"
    //strcpy(qry_orders.InsertTimeStart, "");
    //strcpy(qry_orders.InsertTimeEnd, "");
    //strcpy(qry_orders.SInfo, "");
    qry_orders.IInfo = INT_NULL_VAL;
    qry_orders.IsCancel = INT_NULL_VAL;
    strcpy(qry_orders.InvestorID, _brokerInfo._account);
    int ret = _stockHandle._tradeAPI->ReqQryOrder(&qry_orders, reqID);
    if (ret != 0) {
        return false;
    }
    return true;
}

bool HXExchange::QueryOptionOrders(uint64_t reqID) {
    TORASPAPI::CTORATstpSPQryOrderField pQryOrderField;
    memset(&pQryOrderField, 0, sizeof(pQryOrderField));
    strcpy(pQryOrderField.SecurityID, "");
    strcpy(pQryOrderField.ShareholderID, "");
    strcpy(pQryOrderField.OrderSysID, "");
    pQryOrderField.IInfo = INT_NULL_VAL;
    strcpy(pQryOrderField.InvestorID, _brokerInfo._account);

    int ret = _optionHandle._tradeAPI->ReqQryOrder(&pQryOrderField, reqID);
    if (ret != 0) {
        return false;
    }
    return true;
}

bool HXExchange::Init(const ExchangeInfo& handle){
    _brokerInfo = handle;
    // _user = handle._username;
    // _pwd = handle._passwd;
    // _account = handle._account;
    // _accpwd = handle._accpwd;
    // _port = handle._localPort;

    _quoteAPI = CTORATstpXMdApi::CreateTstpXMdApi();
    _quote = new HXQuateSpi(_quoteAPI, this);

    InitQuote();
    
    using namespace TORASTOCKAPI;
    _stockHandle._tradeAPI = CTORATstpTraderApi::CreateTstpTraderApi();
    _stockHandle._trade = new HXTrade(this);
    InitStockTrade();

    using namespace TORASPAPI;
    _optionHandle._tradeAPI = CTORATstpSPTraderApi::CreateTstpSPTraderApi();
    _optionHandle._trade = new HXOptionTrade(this);
    InitOptionTrade();
    return true;
}

void HXExchange::SetFilter(const QuoteFilter& filter){
    _filter = filter;
}

bool HXExchange::Release(){
    if (_quote) {
        _quoteAPI->Release();
        delete _quote;
        _quote = nullptr;
    }
    if (_stockHandle._trade) {
        _stockHandle._tradeAPI->Release();
        delete _stockHandle._trade;
        _stockHandle._trade = nullptr;

        if (_stockHandle._cancelLimit) delete _stockHandle._cancelLimit;
        if (_stockHandle._insertLimit) delete _stockHandle._insertLimit;
    }
    if (_optionHandle._trade) {
        _optionHandle._tradeAPI->Release();
        _optionHandle._trade->Release();
        delete _optionHandle._trade;
        _optionHandle._trade = nullptr;
    }
    return true;
}

bool HXExchange::Login(AccountType t){
    if (IsLogin())
        return true;

    bool status = _quote->Init();

    if (!_quote_login) {
        CTORATstpReqUserLoginField req_user_login_field;
        memset(&req_user_login_field, 0, sizeof(req_user_login_field));
        strncpy(req_user_login_field.LogInAccount, _brokerInfo._username, strlen(_brokerInfo._username));
        strncpy(req_user_login_field.Password, _brokerInfo._passwd, strlen(_brokerInfo._passwd));
        memcpy(req_user_login_field.UserProductInfo, USER_PRODUCT_INFO, strlen(USER_PRODUCT_INFO));
        int ret = _quoteAPI->ReqUserLogin(&req_user_login_field, _reqID);
        if (ret != 0) {
            INFO("HX login fail.");
            return false;
        }
        _quote_login = true;
    }
    
    // 
    if (!_trader_login) {
        TORASTOCKAPI::CTORATstpReqUserLoginField tradeUser;
        memset(&tradeUser, 0, sizeof(tradeUser));
        strncpy(tradeUser.LogInAccount, _brokerInfo._account, strlen(_brokerInfo._account));
        strncpy(tradeUser.Password, _brokerInfo._accpwd, strlen(_brokerInfo._accpwd));
        tradeUser.LogInAccountType = TORA_TSTP_LACT_AccountID;  // 
        tradeUser.AuthMode = TORA_TSTP_AM_Password;
        memcpy(tradeUser.UserProductInfo, USER_PRODUCT_INFO, strlen(USER_PRODUCT_INFO));

        // 
        String termInfo("PC;");
        termInfo += GetIP() + ";IPORT=" + std::to_string(_brokerInfo._localPort);
        strcpy(tradeUser.TerminalInfo,
            (termInfo + ";LIP=192.168.118.107;MAC=54EE750B1713FCF8AE5CBD58;HD=TF655AY91GHRVL").c_str());

        auto reqID = ++_reqID;
        auto promise = std::make_shared<std::promise<TORASTOCKAPI::CTORATstpRspUserLoginField>>();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _promises.emplace(reqID, promise);
        }
        _stockHandle._tradeAPI->ReqUserLogin(&tradeUser, reqID);

        std::future<TORASTOCKAPI::CTORATstpRspUserLoginField> fut;
        if (!getFuture(promise, fut)) {
            return false;
        }
        _trader_login = true;
        // 获取股东账户信息
        QueryShareHolder(ExchangeName::MT_Shanghai);
        QueryShareHolder(ExchangeName::MT_Shenzhen);
    }
    
    if (status) {
        _login_status = true;
    }
    return true;
}
bool HXExchange::IsLogin(){
    return _login_status;
}

void HXExchange::Logout(AccountType t) {
    if (_trader_login) {
        auto reqID = ++_reqID;
        TORASTOCKAPI::CTORATstpUserLogoutField field;
        _stockHandle._tradeAPI->ReqUserLogout(&field, reqID);
        _trader_login = false;
    }
}

bool HXExchange::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info)
{
    order_id id;
    id._id = ++_reqID;
    char* all[1] = { 0 };
    strcpy(all[0], "00000000");
    _quoteAPI->SubscribeSimplifyMarketData(all, 1, 0);

    auto promise = std::make_shared<std::promise<CTORATstpSpecificSecurityField>>();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _promises.emplace(id._id, promise);
    }

    try {
        auto fut = promise->get_future();
        auto status = fut.wait_for(std::chrono::seconds(10));
        _quoteAPI->UnSubscribeSimplifyMarketData(all, 1, 0);

        if (status == std::future_status::timeout) {
            return false;
        }
    }
    catch (const std::exception& e) {
        return false;
    }
    return true;
}

bool HXExchange::GetPosition(AccountPosition& ap){
    order_id oid;
    oid._id = ++_reqID;
    auto promise = initPromise<bool>(oid._id);
    auto& positions = _stockHandle._trade->GetPositions();
    positions.clear();

    TORASTOCKAPI::CTORATstpQryPositionField field;
    memset(&field, 0, sizeof(field));
    strcpy(field.InvestorID, _brokerInfo._account);
    int ret = _stockHandle._tradeAPI->ReqQryPosition(&field, oid._id);

    std::future<bool> fut;
    if (!getFuture(promise, fut)) {
        return false;
    }
    // 更新当前价格
    for (auto& item : positions) {
        auto qt = _quote->GetQuote(item._symbol);
        item._curPrice = qt._askPrice[0];
    }
    ap._positions = std::move(positions);
    return true;
}

AccountAsset HXExchange::GetAsset(){
    AccountAsset aa;
    return aa;
}

order_id HXExchange::AddOrder(const symbol_t& symbol, OrderContext* ctx){
    order_id id;
    if (is_stock(symbol)) {
        if (!_stockHandle._insertLimit->tryConsume()) {
            id._error = ERROR_INSERT_LIMIT;
            return id;
        }
        return AddStockOrder(symbol, ctx);
    }
    else if (is_option(symbol)) {
        if (symbol._exchange == ExchangeName::MT_Shanghai || symbol._exchange == ExchangeName::MT_Shenzhen) {
            return AddOptionOrder(symbol, ctx);
        }
    }
    return id;
}

void HXExchange::OnOrderReport(order_id id, const TradeReport& report){
    auto broker = _server->GetBrokerSubSystem();
    _orders.visit(id._id, [&report,broker](concurrent_order_map::value_type& value) {
        auto ctx = value.second;
        broker->RecordTrade(*ctx);
        ctx->_trades._reports.emplace_back(report);
        ctx->Update(report);
        ctx->_flag = true;
        //delete ctx;
      });
    if (report._status == OrderStatus::CancelSuccess) {
        _orders.erase(id._id);
    }
}

bool HXExchange::CancelOrder(order_id id, OrderContext* ctx){
    if (is_stock(ctx->_order._symbol)) {
        CancelStockOrder(id, ctx);
    } else {
        CancelOptionOrder(id, ctx);
    }
    return true;
}

void HXExchange::CancelStockOrder(order_id id, OrderContext* ctx) {
    auto reqID = ++_reqID;
    id._id = reqID;
    //auto promise = initPromise<TORASTOCKAPI::CTORATstpInputOrderActionField>(reqID);

    TORASTOCKAPI::CTORATstpInputOrderActionField pInputOrderActionField;
    memset(&pInputOrderActionField, 0, sizeof(TORASTOCKAPI::CTORATstpInputOrderActionField));
    pInputOrderActionField.ExchangeID = toExchangeID((ExchangeName)ctx->_order._symbol._exchange);
    pInputOrderActionField.ActionFlag = TORASTOCKAPI::TORA_TSTP_AF_Delete;
    strcpy(pInputOrderActionField.OrderSysID, id._sysID);
    strcpy(pInputOrderActionField.SInfo, id._sysID);
    pInputOrderActionField.IInfo = id._id;

    if (!_orders.empty()) {
        _orders.visit_while([&id](concurrent_order_map::value_type& value) {
            auto ctx = value.second;
            if (ctx->_order._sysID != id._sysID) {
                return true;
            }
            id._id = value.first;
            return false;
            });
    }
    else {
        _orders.emplace(id._id, ctx);
    }
    _stockHandle._tradeAPI->ReqOrderAction(&pInputOrderActionField, reqID);

}

void HXExchange::CancelOptionOrder(order_id id, OrderContext* order) {

}

// 获取当前尚未完成的所有订单
bool HXExchange::GetOrders(SecurityType type, OrderList& ol){
    auto reqID = ++_reqID;
    auto promise = initPromise<bool>(reqID);
    
    switch (type)
    {
    case SecurityType::Stock:
        if (!QueryStockOrders(reqID)) {
            return false;
        }
        _stockHandle._trade->GetOrders().clear();
        break;
    case SecurityType::Option:
        if (!QueryOptionOrders(reqID)) {
            return false;
        }
        _optionHandle._trade->GetOrders().clear();
        break;
    case SecurityType::Future:
        break;
    default:
        break;
    }
    std::future<bool> fut;
    if (!getFuture(promise, fut)) {
        return false;
    }

    switch (type)
    {
    case SecurityType::Stock:
        ol = std::move(_stockHandle._trade->GetOrders());
        break;
    case SecurityType::Option:
        ol = std::move(_optionHandle._trade->GetOrders());
        break;
    case SecurityType::Future:
        break;
    default:
        break;
    }
    return true;
}

bool HXExchange::GetOrder(const String& sysID, Order& ol){
    auto reqID = ++_reqID;
    auto promise = initPromise<bool>(reqID);
    auto& orders = _stockHandle._trade->GetOrders();
    orders.clear();

    TORASTOCKAPI::CTORATstpQryOrderField qry_orders;
    memset(&qry_orders, 0, sizeof(qry_orders));
    qry_orders.IInfo = INT_NULL_VAL;
    qry_orders.IsCancel = INT_NULL_VAL;
    strcpy(qry_orders.InvestorID, _brokerInfo._account);
    strcpy(qry_orders.OrderSysID, sysID.c_str());
    int ret = _stockHandle._tradeAPI->ReqQryOrder(&qry_orders, reqID);
    if (ret != 0) {
        return false;
    }

    std::future<bool> fut;
    if (!getFuture(promise, fut) || orders.size() != 1) {
        return false;
    }

    ol = std::move(orders.front());
    return true;
}

void HXExchange::QueryQuotes(){
    if (_quote_inited)
        return;
    _quote_inited = true;
    // 订阅行情
    if (_filter._symbols.empty()) {
        // 订阅全市场行情,流量?
        SubscribeStockQuote({ {0, {"000000"}} });
    }
    else {
        Map<char, Vector<String>> subscribe_map, option_map;
        for (auto symb: _filter._symbols) {
            symbol_t symbol;
            if (symb.size() != 6) {
                List<String> info;
                split(symb, info, ",");
                symbol = ETFOptionSymbol(info.front(), info.back());
            }
            else {
                symbol = to_symbol(symb);
            }
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
            if (is_stock(symbol)) {
                subscribe_map[type].emplace_back(symb);
            }
            else {
                option_map[type].emplace_back(std::move(symb));
            }
        }
        // 补充指数
        subscribe_map['1'].emplace_back("000001");

        SubscribeStockQuote(subscribe_map);
        //SubscribeOptionQuote(option_map);
        _requested = true;
    }
    SubscribeOptionQuote({ {0, {"000000"}} });
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
            strncpy(subscribe_array[i], item.second[i].c_str(), item.second[i].size());
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

bool HXExchange::GetCommission(symbol_t symbol, List<Commission>& comms) {
    if (_commissions.count(symbol)) {
        for (auto p: _commissions[symbol]) {
            comms.push_back(*p);
        }
        return true;
    }
    auto reqID = ++_reqID;
    Commission* comm = new Commission;
    memset(comm, 0, sizeof(Commission));
    if (is_stock(symbol)) {
        auto promise =  initPromise<TORASTOCKAPI::CTORATstpInvestorTradingFeeField>(reqID);

        TORASTOCKAPI::CTORATstpQryTradingFeeField fee;
        memset(&fee, 0, sizeof(fee));
        _stockHandle._tradeAPI->ReqQryTradingFee(&fee, reqID);

        std::future<TORASTOCKAPI::CTORATstpInvestorTradingFeeField> fut;
        if (!getFuture(promise, fut)) {
            return false;
        }

        auto field = fut.get();
        comm->_status = 1;
        comms.emplace_back(*comm);

        _commissions[symbol].insert(comm);
        return true;
    }
    
    
    // _tradeAPI->ReqQryRationalInfo(, reqID);
  return false;
}

double HXExchange::GetAvailableFunds()
{
    auto reqID = ++_reqID;
    auto promise = initPromise<TORASTOCKAPI::CTORATstpTradingAccountField>(reqID);
    
    TORASTOCKAPI::CTORATstpQryTradingAccountField qry_trading_account_field;
    memset(&qry_trading_account_field, 0, sizeof(qry_trading_account_field));
    strcpy(qry_trading_account_field.AccountID, _brokerInfo._account);

    int ret = _stockHandle._tradeAPI->ReqQryTradingAccount(&qry_trading_account_field, reqID);
    if (ret != 0) {
        WARN("ReqQryTradingAccount fail, ret {}", ret);
        _promises.erase(reqID);
        return 0;
    }

    std::future<TORASTOCKAPI::CTORATstpTradingAccountField> fut;
    if (!getFuture(promise, fut)) {
        return 0;
    }
    return fut.get().UsefulMoney;
}

bool HXExchange::QueryShareHolder(ExchangeName name)
{
    order_id id;
    id._id = ++_reqID;
    auto promise = initPromise<String>(id._id);

    TORASTOCKAPI::CTORATstpQryShareholderAccountField qry_shr_account;
    memset(&qry_shr_account, 0, sizeof(qry_shr_account));
    strcpy(qry_shr_account.InvestorID, _brokerInfo._account);
    switch (name) {
    case ExchangeName::MT_Shanghai:
        qry_shr_account.ExchangeID = TORA_TSTP_EXD_SSE;
    break;
    case ExchangeName::MT_Shenzhen:
        qry_shr_account.ExchangeID = TORA_TSTP_EXD_SZSE;
    default:
    break;
    }
    
    _stockHandle._tradeAPI->ReqQryShareholderAccount(&qry_shr_account, id._id);

    std::future<String> fut;
    if (!getFuture(promise, fut)) {
        return false;
    }
    if (fut.valid()) {
        _shareholder[name] = fut.get();
        return true;
    }
    return false;
}

