#include "Bridge/CTP/CTPQuote.h"
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include "Bridge/CTP/CTPSymbol.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "nng/nng.h"
#include "nng/protocol/pubsub0/pub.h"
#include "nng/protocol/pubsub0/sub.h"
#include "yas/detail/type_traits/flags.hpp"
#include "Bridge/CTP/CTPSymbol.h"
#include "Bridge/CTP/CTPExchange.h"

#define URI_CTP_WAIT  "inproc://URI_CTP_WAIT"

#define GET_DOUBLE_VALUE(x) (x == std::numeric_limits<double>::max()?0:x);

CTPQuote::CTPQuote(CTPExchange* exchange)
:_conn_status(false), _login_status(false), _reconnected(false), _exchange(exchange){
}

CTPQuote::~CTPQuote() {

}

bool CTPQuote::Init() {
    if (_reconnected)
        return true;
    return Publish(URI_RAW_QUOTE, _sock);
}

bool CTPQuote::IsConnected()
{
  if (_conn_status) return true;
  std::unique_lock<std::mutex> lock(_mx);
  std::chrono::time_point tick = std::chrono::system_clock::now() + std::chrono::seconds(15);
  _cv.wait_until(lock, tick);
  return _conn_status;
}

bool CTPQuote::IsLogin() {
  if (_login_status) return true;
  std::unique_lock<std::mutex> lock(_mx);
  std::chrono::time_point tick = std::chrono::system_clock::now() + std::chrono::seconds(15);
  _cv.wait_until(lock, tick);
  return _login_status;
}

QuoteInfo CTPQuote::GetQuote(symbol_t symbol)
{
  return QuoteInfo();
}

void CTPQuote::OnFrontConnected() {
  INFO("CTP connected.");
  SetCurrentThreadName("CTPQuote");
  {
      std::unique_lock<std::mutex> lock(_mx);
      _conn_status = true;
      _cv.notify_all();
  }
  if (_reconnected) {
      _exchange->Login();
  }
}

void CTPQuote::OnHeartBeatWarning(int nTimeLapse) {

}

// 当客户端与交易托管系统通信连接断开时，该方法被调用
void CTPQuote::OnFrontDisconnected(int nReason) {
  WARN("disconnected {}", nReason);
  _conn_status = false;
  _login_status = false;
  // 
  _reconnected = true;
  _hasQuote = false;
}
// 当客户端发出登录请求之后，该方法会被调用，通知客户端登录是否成功
void CTPQuote::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin,
  CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  INFO("Login OK");
  std::unique_lock<std::mutex> lock(_mx);
  _login_status = true;
  _cv.notify_all();
}

///登出请求响应
void CTPQuote::OnRspUserLogout(CThostFtdcUserLogoutField* pUserLogout, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {

}

void CTPQuote::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}

void CTPQuote::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}
///深度行情通知
void CTPQuote::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData) {
  // auto symbol = String(pDepthMarketData->InstrumentID, strlen(pDepthMarketData->InstrumentID));
  _hasQuote = true;
  CTPSymbol symbol(String(pDepthMarketData->InstrumentID, strlen(pDepthMarketData->InstrumentID)));
  if (strcmp(pDepthMarketData->InstrumentID, "ta510") == 0 || strcmp(pDepthMarketData->InstrumentID, "TA510") == 0) {
    time_t n;
    time(&n);
    INFO("{}: ---update: {}[{}]", ToString(n), pDepthMarketData->UpdateTime, pDepthMarketData->LastPrice);
  }
  
  auto code = symbol_t(symbol);
  QuoteInfo& info = _tickers[code];
  info._symbol = code;
  info._open = pDepthMarketData->PreSettlementPrice;
  info._close = pDepthMarketData->LastPrice;
  info._high = pDepthMarketData->HighestPrice;
  info._low = pDepthMarketData->LowestPrice;
  String date(pDepthMarketData->ActionDay, strlen(pDepthMarketData->ActionDay));
  String t(pDepthMarketData->UpdateTime, strlen(pDepthMarketData->UpdateTime));
  String fulltime = date + " " + t;
  auto update_tick = FromStr(fulltime, "%Y%m%d %H:%M:%S");
  // time_t cur_tick;
  // time(&cur_tick);
  info._time = update_tick;
  info._volume = pDepthMarketData->Volume;
  info._turnover = pDepthMarketData->Turnover;

  info._bidPrice[0] = GET_DOUBLE_VALUE(pDepthMarketData->BidPrice1);
  info._bidPrice[1] = GET_DOUBLE_VALUE(pDepthMarketData->BidPrice2);
  info._bidPrice[2] = GET_DOUBLE_VALUE(pDepthMarketData->BidPrice3);
  info._bidPrice[3] = GET_DOUBLE_VALUE(pDepthMarketData->BidPrice4);
  info._bidPrice[4] = GET_DOUBLE_VALUE(pDepthMarketData->BidPrice5);

  info._askPrice[0] = GET_DOUBLE_VALUE(pDepthMarketData->AskPrice1);
  info._askPrice[1] = GET_DOUBLE_VALUE(pDepthMarketData->AskPrice2);
  info._askPrice[2] = GET_DOUBLE_VALUE(pDepthMarketData->AskPrice3);
  info._askPrice[3] = GET_DOUBLE_VALUE(pDepthMarketData->AskPrice4);
  info._askPrice[4] = GET_DOUBLE_VALUE(pDepthMarketData->AskPrice5);

  info._bidVolume[0] = pDepthMarketData->BidVolume1;
  info._bidVolume[1] = pDepthMarketData->BidVolume2;
  info._bidVolume[2] = pDepthMarketData->BidVolume3;
  info._bidVolume[3] = pDepthMarketData->BidVolume4;
  info._bidVolume[4] = pDepthMarketData->BidVolume5;

  info._askVolume[0] = pDepthMarketData->AskVolume1;
  info._askVolume[1] = pDepthMarketData->AskVolume2;
  info._askVolume[2] = pDepthMarketData->AskVolume3;
  info._askVolume[3] = pDepthMarketData->AskVolume4;
  info._askVolume[4] = pDepthMarketData->AskVolume5;

  constexpr std::size_t flags = yas::mem|yas::binary;
  yas::shared_buffer buf = yas::save<flags>(info);
  if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
    WARN("send quote message fail.");
    return;
  }
  // INFO("Send quote {} success.", info._symbol);
}

void CTPQuote::OnRspQryMulticastInstrument(CThostFtdcMulticastInstrumentField *pMulticastInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}
///询价通知
void CTPQuote::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *pForQuoteRsp) {

}

void CTPQuote::OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}

void CTPQuote::OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {

}
