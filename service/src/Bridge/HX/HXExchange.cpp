#include "Bridge/HX/HXExchange.h"
#include "Util/system.h"
#include "hx/TORATstpXMdApi.h"
#include "hx/TORATstpTraderApi.h"
#include "Bridge/HX/HXQuote.h"
#include "Bridge/HX/HXTrade.h"
#include <cstring>
#include <future>
#include "server.h"
#include "Util/string_algorithm.h"

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
:ExchangeInterface(server), _quote(nullptr), _quoteAPI(nullptr), _tradeAPI(nullptr), _trade(nullptr)
, _login_status(false), _quote_inited(false), _requested(false), _reqID(0)
, _trader_login(false), _quote_login(false) {
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

bool HXExchange::Init(const ExchangeInfo& handle){
    _user = handle._username;
    _pwd = handle._passwd;
    _account = handle._account;
    _accpwd = handle._accpwd;
    _port = handle._localPort;

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
    _tradeAPI->SubscribePrivateTopic(TORASTOCKAPI::TORA_TERT_QUICK);
    _tradeAPI->SubscribePublicTopic(TORASTOCKAPI::TORA_TERT_RESTART);
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

    if (!_quote_login) {
        CTORATstpReqUserLoginField req_user_login_field;
        memset(&req_user_login_field, 0, sizeof(req_user_login_field));
        strncpy(req_user_login_field.LogInAccount, _user.c_str(), _user.size());
        strncpy(req_user_login_field.Password, _pwd.c_str(), _pwd.size());
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
        strncpy(tradeUser.LogInAccount, _account.c_str(), _account.size());
        strncpy(tradeUser.Password, _accpwd.c_str(), _accpwd.size());
        tradeUser.LogInAccountType = TORA_TSTP_LACT_AccountID;  // 
        tradeUser.AuthMode = TORA_TSTP_AM_Password;
        memcpy(tradeUser.UserProductInfo, USER_PRODUCT_INFO, strlen(USER_PRODUCT_INFO));

        // 
        String termInfo("PC;");
        termInfo += GetIP() + ";IPORT=" + std::to_string(_port);
        strcpy(tradeUser.TerminalInfo,
            (termInfo + ";LIP=192.168.118.107;MAC=54EE750B1713FCF8AE5CBD58;HD=TF655AY91GHRVL").c_str());

        auto reqID = ++_reqID;
        auto promise = std::make_shared<std::promise<TORASTOCKAPI::CTORATstpRspUserLoginField>>();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _promises.emplace(reqID, promise);
        }
        _tradeAPI->ReqUserLogin(&tradeUser, reqID);

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

bool HXExchange::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info)
{
    order_id id{ ++_reqID };

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
    order_id oid{++_reqID};
    auto promise = initPromise<bool>(oid._id);
    auto& positions = _trade->GetPositions();
    positions.clear();

    TORASTOCKAPI::CTORATstpQryPositionField field;
    memset(&field, 0, sizeof(field));
    strcpy(field.InvestorID, _account.c_str());
    int ret = _tradeAPI->ReqQryPosition(&field, oid._id);

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
    order_id oid{++_reqID};
    using namespace TORASTOCKAPI;
    CTORATstpInputOrderField* order = new CTORATstpInputOrderField;
    memset(order, 0, sizeof(CTORATstpInputOrderField));
    
    auto& o = ctx->_order;
    order->OrderRef = oid._id;
    order->Direction = (o._side == 0? TORA_TSTP_D_Buy: TORA_TSTP_D_Sell);
    order->UserRequestID = oid._id;

    String& shareholder = _shareholder[symbol._exchange];
    if (shareholder.empty()) {
        WARN("shareholder is empty");
        return {0};
    }
    strcpy(order->ShareholderID, shareholder.c_str());
    auto strCode = format_symbol(std::to_string(symbol._symbol));
    strncpy(order->SecurityID, strCode.c_str(), strCode.size());
    order->ExchangeID = toExchangeID((ExchangeName)symbol._exchange);
    order->VolumeTotalOriginal = ctx->_order._volume;
    order->LimitPrice = o._order[0]._price;
    // order->CombOffsetFlag[0] = TORA_TSTP_OF_Open;
    // order->CombOffsetFlag[1] = '\0';
    // order->CombHedgeFlag[0] = TORA_TSTP_HF_Speculation;
    // order->CombHedgeFlag[1] = '\0';
    // order->MinVolume = 0;
    order->ForceCloseReason = TORA_TSTP_FCC_NotForceClose;
    // order->UserForceClose = 0;
    // order->IsSwapOrder = 0;

    convertOrderType(*ctx, *order);

    _tradeAPI->ReqOrderInsert(order, oid._id);

    ctx->_order._symbol = symbol;
    ctx->_order._id = oid._id;
    auto pr = std::make_pair(order, ctx);
    _orders.emplace(oid._id, std::move(pr));
    strcpy(oid._sysID, ctx->_order._sysID.c_str());
    return oid;
}

void HXExchange::OnOrderReport(order_id id, const TradeReport& report){
    assert(_orders.count(id._id) != 0);

    _orders.visit(id._id, [&report](concurrent_order_map::value_type& value) {
        auto ctx = value.second.second;
        ctx->_trades._reports.emplace_back(report);
        ctx->Update(report);
        //delete ctx;
      });
    if (report._status == OrderStatus::CancelSuccess) {
        _orders.erase(id._id);
    }
}

bool HXExchange::CancelOrder(order_id id, OrderContext* order){
    auto reqID = ++_reqID;
    auto promise = initPromise<TORASTOCKAPI::CTORATstpInputOrderActionField>(reqID);
    _orders.visit(id._id, [reqID, this](concurrent_order_map::value_type& value) {
        auto ctx = value.second.second;

        TORASTOCKAPI::CTORATstpInputOrderActionField pInputOrderActionField;
        memset(&pInputOrderActionField, 0, sizeof(TORASTOCKAPI::CTORATstpInputOrderActionField));
        pInputOrderActionField.ExchangeID = toExchangeID((ExchangeName)ctx->_order._symbol._exchange);
		pInputOrderActionField.ActionFlag = TORASTOCKAPI::TORA_TSTP_AF_Delete;
        strcpy(pInputOrderActionField.OrderSysID, ctx->_order._sysID.c_str());
        _tradeAPI->ReqOrderAction(&pInputOrderActionField, reqID);
    });

    std::future<TORASTOCKAPI::CTORATstpInputOrderActionField> fut;
    if (!getFuture(promise, fut)) {
        return false;
    }
    // auto info = fut.get();
    // TradeReport report;
    // report._status = OrderStatus::CancelSuccess;
    // order->Update(report);
    // 成功则移除_orders中的订单
    // _orders.erase(id._id);
    return true;
}
// 获取当前尚未完成的所有订单
bool HXExchange::GetOrders(OrderList& ol){
    auto reqID = ++_reqID;
    auto promise = initPromise<TORASTOCKAPI::CTORATstpOrderField>(reqID);
    auto& orders = _trade->GetOrders();
    orders.clear();

    TORASTOCKAPI::CTORATstpQryOrderField qry_orders;
    memset(&qry_orders, 0, sizeof(qry_orders));
    qry_orders.IInfo = INT_NULL_VAL;
    qry_orders.IsCancel = INT_NULL_VAL;
    strcpy(qry_orders.InvestorID, _account.c_str());
    int ret = _tradeAPI->ReqQryOrder(&qry_orders, reqID);
    if (ret != 0) {
        return false;
    }

    std::future<TORASTOCKAPI::CTORATstpOrderField> fut;
    if (!getFuture(promise, fut)) {
        return false;
    }

    ol = std::move(orders);
    return true;
}

bool HXExchange::GetOrder(const String& sysID, Order& ol){
    auto reqID = ++_reqID;
    auto promise = initPromise<TORASTOCKAPI::CTORATstpOrderField>(reqID);
    auto& orders = _trade->GetOrders();
    orders.clear();

    TORASTOCKAPI::CTORATstpQryOrderField qry_orders;
    memset(&qry_orders, 0, sizeof(qry_orders));
    qry_orders.IInfo = INT_NULL_VAL;
    qry_orders.IsCancel = INT_NULL_VAL;
    strcpy(qry_orders.InvestorID, _account.c_str());
    strcpy(qry_orders.OrderSysID, sysID.c_str());
    int ret = _tradeAPI->ReqQryOrder(&qry_orders, reqID);
    if (ret != 0) {
        return false;
    }

    std::future<TORASTOCKAPI::CTORATstpOrderField> fut;
    if (!getFuture(promise, fut) || orders.size() != 1) {
        return false;
    }

    ol = std::move(orders.front());
    return true;
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
        _tradeAPI->ReqQryTradingFee(&fee, reqID);

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
    strcpy(qry_trading_account_field.AccountID, _account.c_str());

    int ret = _tradeAPI->ReqQryTradingAccount(&qry_trading_account_field, reqID);
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
    order_id id{ ++_reqID };
    auto promise = initPromise<String>(id._id);

    TORASTOCKAPI::CTORATstpQryShareholderAccountField qry_shr_account;
    memset(&qry_shr_account, 0, sizeof(qry_shr_account));
    strcpy(qry_shr_account.InvestorID, _account.c_str());
    switch (name) {
    case ExchangeName::MT_Shanghai:
        qry_shr_account.ExchangeID = TORA_TSTP_EXD_SSE;
    break;
    case ExchangeName::MT_Shenzhen:
        qry_shr_account.ExchangeID = TORA_TSTP_EXD_SZSE;
    default:
    break;
    }
    
    _tradeAPI->ReqQryShareholderAccount(&qry_shr_account, id._id);

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

