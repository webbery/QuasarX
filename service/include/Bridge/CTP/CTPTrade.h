#pragma once
#include "std_header.h"
#include "ctp/ThostFtdcTraderApi.h"
#include <future>

class CTPTrade: public CThostFtdcTraderSpi {
public:
    CTPTrade(CThostFtdcTraderApi* api);
    ~CTPTrade();

    bool IsConnected();

    bool IsLogin();

    void GetAllContracts(List<StringView>& contracts);

protected:
	virtual void OnFrontConnected();
	virtual void OnFrontDisconnected(int nReason);
	virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast);

	virtual void OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast);
private:
  std::mutex _mx;
  std::condition_variable _cv;
  bool _conn_status;
  bool _login_status;

  Map<String, String> _contracts;
};
