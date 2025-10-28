#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/exchange.h"
#include <cstring>
#include <filesystem>
#include "Util/system.h"

#define BROKER_ID "9999"

CTPExchange::CTPExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _pUserMdApi(nullptr)
, _pUserTradeApi(nullptr), _nRequestID(1) {
    
}

CTPExchange::~CTPExchange() {
    if (_quote) {
        delete _quote;
    }
    _pUserMdApi->Release();
}

bool CTPExchange::Init(const ExchangeInfo& handle){
    // 创建QuoteApi
    if (!std::filesystem::exists("./flow")) {
        std::filesystem::create_directory("./flow");
    }
    auto version = CThostFtdcMdApi::GetApiVersion();
    INFO("CTP version: {}", version);
    _pUserMdApi = CThostFtdcMdApi::CreateFtdcMdApi("./flow/", false, false);

    _quote = new CTPQuote(this);
    _pUserMdApi->RegisterSpi(_quote);
    std::string protocal("tcp://");
    protocal += handle._quote_addr;
    protocal += ":" + std::to_string(handle._quote_port);
    char szFrontAddr[32] = { 0 };
    sprintf(szFrontAddr, "%s", protocal.c_str());
    _pUserMdApi->RegisterFront(szFrontAddr);
    // _pUserMdApi->SubscribePublicTopic(THOST_TERT_QUICK);
    // _pUserMdApi->SubscribePrivateTopic(THOST_TERT_RESTART);
    _pUserMdApi->Init();

    _pUserTradeApi = CThostFtdcTraderApi::CreateFtdcTraderApi("./flow/");
    _trade = new CTPTrade(_pUserTradeApi);
    _pUserTradeApi->RegisterSpi(_trade);
    protocal = "tcp://";
    protocal += handle._trade_addr;
    protocal += ":" + std::to_string(handle._trade_port);
    memset(szFrontAddr, 0, 32);
    sprintf(szFrontAddr, "%s", protocal.c_str());
    _pUserTradeApi->RegisterFront(szFrontAddr);
    _pUserTradeApi->Init();

    memcpy(&_info, &handle, sizeof(ExchangeInfo));
    return  _quote->IsConnected() && _trade->IsConnected();
}

bool CTPExchange::Release()
{
  if (_pUserMdApi) {
    _pUserMdApi->Release();
    // _pUserMdApi->Join();
  }
  if (_quote) {
    delete  _quote;
    _quote = nullptr;
  }
  if (_pUserTradeApi) {
    _pUserTradeApi->Release();
    // _pUserTradeApi->Join();
  }
  if (_trade) {
    delete _trade;
  }
  return true;
}

bool CTPExchange::IsLogin() {
  return _quote->LoginStatus();
}

bool CTPExchange::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info)
{
    return true;
}

bool CTPExchange::Login(){
  if (!_quote->Init()) {
    return false;
  }
  if (!_quote->IsConnected())
    return false;
  if (_quote->IsLogin())
    return true;

  CThostFtdcReqUserLoginField reqUserLogin = { 0 };
  strcpy(reqUserLogin.BrokerID, BROKER_ID);
  strcpy(reqUserLogin.UserID, _info._username);
  strcpy(reqUserLogin.Password, _info._passwd);
  int num = _pUserMdApi->ReqUserLogin(&reqUserLogin, _nRequestID++);
  switch (num)
  {
  case -1:
    WARN("connect error");
    return false;
  case -2:
    WARN("process count is limited"); // 未处理请求超过许可数
    return false;
  case -3:// 每秒发送请求数超过许可数
    WARN("request per second is limited");
    return false;
  default:
    break;
  }
  int res = _pUserTradeApi->ReqUserLogin(&reqUserLogin, _nRequestID++);
  if (res < 0) {
    WARN("trade login fail.");
    return false;
  }
  bool ret = _quote->IsLogin() && _trade->IsLogin();
  if (ret && !_contracts.empty()) {
    UpdateCommission();
  }
  return ret;
}

bool CTPExchange::GetPosition(AccountPosition&) {
  return true;
}

AccountAsset CTPExchange::GetAsset(){
    AccountAsset aas;
    return aas;
}

order_id CTPExchange::AddOrder(const symbol_t& symbol, OrderContext* order){
    return order_id{0};
}

void CTPExchange::OnOrderReport(order_id id, const TradeReport& report) {

}

bool CTPExchange::CancelOrder(order_id id){
    return true;
}

bool CTPExchange::GetOrders(OrderList& ol)
{
    return true;
}

QuoteInfo CTPExchange::GetQuote(symbol_t symbol) {
  return _quote->GetQuote(symbol);
}

void CTPExchange::SetFilter(const QuoteFilter& filter) {

}

void CTPExchange::QueryQuotes(){
  if (!_quote->IsLogin())
    return;

  // 请求所有合约
  if (_contracts.empty()) {
    CThostFtdcQryInstrumentField req = {0};
    int res = _pUserTradeApi->ReqQryInstrument(&req, _nRequestID++);

    _trade->GetAllContracts(_contracts);
    UpdateCommission();
  }
  if (_quote->HasQuote())
    return;

  //std::cout<< "SubscribeMarketData\n";
  char** codes = new char*[_contracts.size()];
  int i = 0;
  for (auto& id: _contracts) {
    codes[i] = new char[id.size() + 1];
    memset(codes[i], 0, id.size() + 1);
    memcpy(codes[i], id.data(), id.size());
    ++i;
  }
  if (!_contracts.empty()) {
    _pUserMdApi->SubscribeMarketData(codes, _contracts.size());
  }
  for (i = (int)_contracts.size() - 1; i >= 0; --i) {
    delete[] codes[i];
  }
  delete[] codes;
}

void CTPExchange::UpdateCommission() {
  for (auto& contract: _contracts) {
    CThostFtdcQryInstrumentMarginRateField margin_rate;
    strcpy(margin_rate.BrokerID, BROKER_ID);
    strcpy(margin_rate.InvestorID, _info._username);
    margin_rate.HedgeFlag = THOST_FTDC_HF_Speculation;
    strcpy(margin_rate.InstrumentID, contract.data());
    auto ret = _pUserTradeApi->ReqQryInstrumentMarginRate(&margin_rate, _nRequestID++);

    CThostFtdcQryInstrumentCommissionRateField comm_rate;
    strcpy(comm_rate.BrokerID, BROKER_ID);
    strcpy(comm_rate.InvestorID, _info._username);
    strcpy(comm_rate.InstrumentID, contract.data());
    _pUserTradeApi->ReqQryInstrumentCommissionRate(&comm_rate, _nRequestID++);
  }
}

double CTPExchange::GetAvailableFunds()
{
    return 1000000;
}

const Commission& CTPExchange::GetCommission(const symbol_t& symbol) {
  return _commissions.at(symbol);
}
