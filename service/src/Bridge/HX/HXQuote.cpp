#include "Bridge/HX/HXQuote.h"
#include "Bridge/HX/HXExchange.h"
#include "Util/system.h"
#include "Bridge/exchange.h"
#include "Util/string_algorithm.h"
#include "Bridge/ETFOptionSymbol.h"
#include <shared_mutex>

using namespace TORALEV1API;

HXQuateSpi::HXQuateSpi(CTORATstpXMdApi* api, HXExchange* exchange)
:_exchange(exchange), _isInited(false) {
    
}

HXQuateSpi::~HXQuateSpi() {
    if (_sock.id != 0) {
        nng_close(_sock);
    }
}

bool HXQuateSpi::Init() {
    if (!_isInited) {
        _isInited = true;
        return Publish(URI_RAW_QUOTE, _sock);
    }
    return _isInited;
}

QuoteInfo HXQuateSpi::GetQuote(symbol_t symbol) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (_tickers.count(symbol)) {
        return _tickers[symbol];
    }
    return QuoteInfo();
}

void HXQuateSpi::OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField *pMarketDataField) {
    // if (pMarketDataField->MDSecurityStat == 0)
    //     return;

    auto name = pMarketDataField->SecurityID;
    // if (strcmp(name, "000001") == 0) {
    //     INFO("recv: {} {}", pMarketDataField->ExchangeID, pMarketDataField->LastPrice);
    // }
    auto symb = to_symbol(name);
    QuoteInfo& info = _tickers[symb];
    auto cur = Now();
    auto strDate = ToString(cur);
    List<String> infos;
    split(strDate, infos, " ");
    InitQuoteInfo(info, symb, infos.front(), pMarketDataField);
    
    yas::shared_buffer buf = yas::save<flags>(info);
    if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message fail.\n");
        return;
    }
}

void HXQuateSpi::OnFrontConnected()
{
    INFO("HX connected");
}

void HXQuateSpi::OnFrontDisconnected(int nReason)
{
    INFO("HX quote disconnect:{}", nReason);
    //_exchange->InitQuote();
    //_exchange->_login_status = false;
    //_exchange->_quote_inited = false;
    //_exchange->_quote_login = false;
    // _exchange->_trader_login = false;
    // _exchange->Login(AccountType::MAIN);
}

void HXQuateSpi::OnRspSubSimplifyMarketData(CTORATstpSpecificSecurityField* pSpecificSecurityField, CTORATstpRspInfoField* pRspInfoField)
{
}

void HXQuateSpi::OnRspSubSPMarketData(TORALEV1API::CTORATstpSpecificSecurityField* pSpecificSecurityField, TORALEV1API::CTORATstpRspInfoField* pRspInfoField)
{
    INFO("subscribe option");
}

void HXQuateSpi::OnRtnSPMarketData(TORALEV1API::CTORATstpMarketDataField* pMarketDataField)
{
    //option data
    auto strCode = pMarketDataField->SecurityID;
    String strName(to_utf8(pMarketDataField->SecurityName));
    auto strExchange = pMarketDataField->ExchangeID;

    contract_type t = contract_type::call;
    // 
    symbol_t symb = ETFOptionSymbol(strCode, strName);

    if (_names.count(symb) == 0) {
        _names[symb] = strName;
    }

    QuoteInfo& info = _tickers[symb];
    auto cur = Now();
    auto strDate = ToString(cur);
    List<String> infos;
    split(strDate, infos, " ");
    InitQuoteInfo(info, symb, infos.front(), pMarketDataField);

    yas::shared_buffer buf = yas::save<flags>(info);
    if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message fail.\n");
        return;
    }
}

List<QuoteInfo>& HXQuateSpi::GetOptionHistory(symbol_t symbol) {
    std::shared_lock<std::shared_mutex> lock(_optMtx);
    return _optionHistory[symbol];
}

void HXQuateSpi::InitQuoteInfo(QuoteInfo& info, symbol_t symb, const String& date, TORALEV1API::CTORATstpMarketDataField* pMarketDataField) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    String strTime(pMarketDataField->UpdateTime);
    strTime = date + " " + strTime;
    info._time = FromStr(strTime, "%Y-%m-%d %H:%M:%S");
    info._symbol = symb;
    info._open = pMarketDataField->OpenPrice;
    info._close = pMarketDataField->PreClosePrice;
    info._volume = pMarketDataField->Volume;
    info._value = pMarketDataField->Turnover;
    info._high = pMarketDataField->HighestPrice;
    info._low = pMarketDataField->LowestPrice;
    info._upper = pMarketDataField->UpperLimitPrice;
    info._lower = pMarketDataField->LowerLimitPrice;

    info._bidPrice[0] = pMarketDataField->BidPrice1;
    info._bidPrice[1] = pMarketDataField->BidPrice2;
    info._bidPrice[2] = pMarketDataField->BidPrice3;
    info._bidPrice[3] = pMarketDataField->BidPrice4;
    info._bidPrice[4] = pMarketDataField->BidPrice5;
    info._askPrice[0] = pMarketDataField->AskPrice1;
    info._askPrice[1] = pMarketDataField->AskPrice2;
    info._askPrice[2] = pMarketDataField->AskPrice3;
    info._askPrice[3] = pMarketDataField->AskPrice4;
    info._askPrice[4] = pMarketDataField->AskPrice5;
    info._bidVolume[0] = pMarketDataField->BidVolume1;
    info._bidVolume[1] = pMarketDataField->BidVolume2;
    info._bidVolume[2] = pMarketDataField->BidVolume3;
    info._bidVolume[3] = pMarketDataField->BidVolume4;
    info._bidVolume[4] = pMarketDataField->BidVolume5;
    info._askVolume[0] = pMarketDataField->AskVolume1;
    info._askVolume[1] = pMarketDataField->AskVolume2;
    info._askVolume[2] = pMarketDataField->AskVolume3;
    info._askVolume[3] = pMarketDataField->AskVolume4;
    info._askVolume[4] = pMarketDataField->AskVolume5;
}
