#include "Bridge/CTP/CTPExchange.h"
#include "Bridge/exchange.h"
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>
#include "Util/system.h"

#define BROKER_ID "9999"

CTPExchange::CTPExchange(Server* server)
:ExchangeInterface(server), _quote(nullptr), _pUserMdApi(nullptr)
, _pUserTradeApi(nullptr), _trade(nullptr), _nRequestID(1) {

}

CTPExchange::~CTPExchange() {
    // 先停止 API 回调，再释放资源
    if (_pUserMdApi) {
        _pUserMdApi->RegisterSpi(nullptr);
        _pUserMdApi->Release();
        _pUserMdApi = nullptr;
    }
    if (_pUserTradeApi) {
        _pUserTradeApi->RegisterSpi(nullptr);
        _pUserTradeApi->Release();
        _pUserTradeApi = nullptr;
    }
    // SPI 实现对象在 API 释放后删除
    if (_quote) {
        delete _quote;
        _quote = nullptr;
    }
    if (_trade) {
        delete _trade;
        _trade = nullptr;
    }
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
    protocal += handle._default_addr;
    protocal += ":" + std::to_string(handle._stock_port);
    memset(szFrontAddr, 0, 32);
    sprintf(szFrontAddr, "%s", protocal.c_str());
    _pUserTradeApi->RegisterFront(szFrontAddr);
    _pUserTradeApi->Init();

    memcpy(&_info, &handle, sizeof(ExchangeInfo));
    return  _quote->IsConnected() && _trade->IsConnected();
}

bool CTPExchange::Release()
{
  // 释放行情 API
  if (_pUserMdApi) {
    _pUserMdApi->RegisterSpi(nullptr);
    _pUserMdApi->Release();
    _pUserMdApi = nullptr;
  }
  if (_quote) {
    delete _quote;
    _quote = nullptr;
  }
  // 释放交易 API
  if (_pUserTradeApi) {
    _pUserTradeApi->RegisterSpi(nullptr);
    _pUserTradeApi->Release();
    _pUserTradeApi = nullptr;
  }
  if (_trade) {
    delete _trade;
    _trade = nullptr;
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

bool CTPExchange::Login(AccountType t){
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

void CTPExchange::Logout(AccountType t) {

}

bool CTPExchange::GetPosition(AccountPosition&) {
  return true;
}

AccountAsset CTPExchange::GetAsset(){
    AccountAsset aas;
    return aas;
}

order_id CTPExchange::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order){
    // run_id: 策略运行 ID，用于区分不同的策略实例（回测/实盘）
    return order_id();
}

void CTPExchange::OnOrderReport(order_id id, const TradeReport& report) {

}

Boolean CTPExchange::CancelOrder(order_id id, OrderContext* order){
    return true;
}

bool CTPExchange::GetOrders(SecurityType type, OrderList& ol)
{
    return true;
}

bool CTPExchange::GetOrder(const String& sysID, Order& ol)
{
    return true;
}

QuoteInfo CTPExchange::GetQuote(symbol_t symbol) {
  return _quote->GetQuote(symbol);
}

bool CTPExchange::GetCommission(symbol_t symbol, List<Commission>&) {
  return true;
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

  // 使用智能指针管理内存，确保异常安全
  std::vector<std::unique_ptr<char[]>> codes(_contracts.size());
  int i = 0;
  for (auto& id: _contracts) {
    codes[i] = std::make_unique<char[]>(id.size() + 1);
    memset(codes[i].get(), 0, id.size() + 1);
    memcpy(codes[i].get(), id.data(), id.size());
    ++i;
  }
  if (!_contracts.empty()) {
    // 构建原始指针数组供 API 使用
    std::vector<char*> instrument_ids(_contracts.size());
    for (size_t j = 0; j < _contracts.size(); ++j) {
      instrument_ids[j] = codes[j].get();
    }
    _pUserMdApi->SubscribeMarketData(instrument_ids.data(), _contracts.size());
  }
  // unique_ptr 会自动释放内存，无需手动 delete
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

double CTPExchange::GetAvailableFunds(run_id_t run_id)
{
    // run_id: 策略运行 ID，用于区分不同的策略实例（回测/实盘）
    return 1000000;
}

Boolean CTPExchange::HasPermission(symbol_t symbol)
{
    return true;
}

void CTPExchange::Reset()
{

}

int CTPExchange::GetStockLimitation(char type)
{
    return 0;
}

bool CTPExchange::SetStockLimitation(char type, int limitation)
{
    return false;
}

