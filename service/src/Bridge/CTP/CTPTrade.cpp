#include "Bridge/CTP/CTPTrade.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"

CTPTrade::CTPTrade(CThostFtdcTraderApi* api):_conn_status(false) {
  
}

CTPTrade::~CTPTrade() {

}

bool CTPTrade::IsConnected() {
  if (_conn_status) return true;
  std::unique_lock<std::mutex> lock(_mx);
  std::chrono::time_point tick = std::chrono::system_clock::now() + std::chrono::seconds(15);
  _cv.wait_until(lock, tick);
  return _conn_status;
}

bool CTPTrade::IsLogin() {
  if (_login_status) return true;
  std::unique_lock<std::mutex> lock(_mx);
  std::chrono::time_point tick = std::chrono::system_clock::now() + std::chrono::seconds(15);
  _cv.wait_until(lock, tick);
  return _login_status;
}

void CTPTrade::OnFrontConnected() {
  INFO("CTP trade connected.");
  std::unique_lock<std::mutex> lock(_mx);
  _conn_status = true;
  _cv.notify_all();
}

void CTPTrade::OnFrontDisconnected(int nReason) {
    // 避免其他回调没有正常结束导致的一直等待
  _conn_status = false;
}

void CTPTrade::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
  std::unique_lock<std::mutex> lock(_mx);
  if (pRspInfo && pRspInfo->ErrorID != 0) {
#ifdef WIN32
    WARN("Login ERROR: {}", pRspInfo->ErrorMsg);
#else
    WARN("Login ERROR: {}", to_gbk(pRspInfo->ErrorMsg));
#endif
  } else {
    _login_status = true;
  }
  _cv.notify_all();
}

void CTPTrade::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast) {
    //INFO("Contract: {}, {}", pInstrument->InstrumentName, pInstrument->InstrumentID);
    auto len = strlen(pInstrument->InstrumentID);
    String symbol(pInstrument->InstrumentID, len);
    _contracts[symbol] = pInstrument->InstrumentName;
    // 处理合约保证金率
    // pInstrument->
    if (bIsLast) {
      _cv.notify_all();
    }
}

void CTPTrade::GetAllContracts(List<StringView>& contracts) {
  std::unique_lock<std::mutex> lock(_mx);
  std::chrono::time_point tick = std::chrono::system_clock::now() + std::chrono::seconds(15);
  _cv.wait_until(lock, tick);
  for (auto& item : _contracts) {
      contracts.push_back(item.first);
  }
}
