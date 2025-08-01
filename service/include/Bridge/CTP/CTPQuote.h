#pragma once
#include "Util/system.h"
#include "ctp/ThostFtdcMdApi.h"
#include <condition_variable>
#include <mutex>
#include <nng/nng.h>
#include <future>
#include "Bridge/exchange.h"

class CTPExchange;
class CTPQuote :public CThostFtdcMdSpi {
public:
    CTPQuote(CTPExchange* exchange);
    ~CTPQuote();

    bool Init();

    bool IsConnected();

    bool IsLogin();
    bool LoginStatus() {return _login_status; }

    bool HasQuote() { return _hasQuote;}

    QuoteInfo GetQuote(symbol_t symbol);

protected:
    void OnFrontConnected();

    void OnHeartBeatWarning(int nTimeLapse);

    // 当客户端与交易托管系统通信连接断开时，该方法被调用
    void OnFrontDisconnected(int nReason);
    // 当客户端发出登录请求之后，该方法会被调用，通知客户端登录是否成功
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
        CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);

    ///登出请求响应
    void OnRspUserLogout(CThostFtdcUserLogoutField* pUserLogout, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);

    void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);

    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);
    ///深度行情通知
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData);

    void OnRspQryMulticastInstrument(CThostFtdcMulticastInstrumentField* pMulticastInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);
    ///询价通知
    void OnRtnForQuoteRsp(CThostFtdcForQuoteRspField* pForQuoteRsp);

    void OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);

    void OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);

private:
    CTPExchange* _exchange;
    nng_socket _sock;

    std::mutex _mx;
    std::condition_variable _cv;
    bool _conn_status: 1;
    bool _login_status: 1;
    bool _reconnected: 1 = false;
    bool _hasQuote: 1 = false;

    std::map<symbol_t, QuoteInfo> _tickers;
};
