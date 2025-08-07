#include "Bridge/XTP/XTPExchange.h"
#include <cstddef>
#include <cstdio>
#include <string.h>
#include "server.h"
#include "std_header.h"
#include "xtp/xoms_api_struct.h"
#include "Util/system.h"
#include "xtp/xtp_api_data_type.h"

using namespace std;

#define WAIT_FOR_REQUEST(request_id) \
  auto& mtx = GetMutex(request_id);\
  std::unique_lock<std::mutex> lock(mtx);\
  auto& cv = _cvs[request_id];\
  if (cv.wait_for(lock, std::chrono::seconds(3)) != std::cv_status::timeout)

#define FINISH_WAIT() \
  else {\
    XTPRI* error_info = m_pTradeApi->GetApiLastError();\
    printf("request time out, %d : %s\n", error_info->error_id, error_info->error_msg);\
  }

XTPExchange::XTPExchange(Server* server)
:ExchangeInterface(server), m_pQuoteApi(NULL), m_pTradeApi(NULL), m_session(0), m_pTrade(NULL), m_pQuote(NULL),
_requested(false), _login_status(false){
}

XTPExchange::~XTPExchange() {
  Release();
}

bool XTPExchange::Init(const ExchangeInfo& handle) {
    uint8_t client_id = 1;//一个进程一个client id，可在[1, 99]区间内任选，并固定下来
	  std::string save_file_path = "./";//保存xtp api日志的路径，需要有可读写权限
	  XTP_LOG_LEVEL log_level = XTP_LOG_LEVEL_DEBUG;//xtp api日志的输出级别，建议调试时使用debug级别，正常运行时使用info级别
	  ///创建QuoteApi
    if (m_pQuoteApi == nullptr) {
      m_pQuoteApi = XTP::API::QuoteApi::CreateQuoteApi(client_id, save_file_path.c_str(), log_level);
      if (m_pQuoteApi == NULL) {
          printf("NULL\n");
          return false;
      }
      m_pQuote = new XTPQuote(m_pQuoteApi, this);
      m_pQuoteApi->RegisterSpi(m_pQuote);
      m_pQuoteApi->SetHeartBeatInterval(15);
    }
	  
    if (m_pTradeApi == nullptr) {
      m_pTradeApi = XTP::API::TraderApi::CreateTraderApi(client_id, "./", XTP_LOG_LEVEL_DEBUG);
      if (!m_pTradeApi) {
        return false;
      }
      m_pTrade = new XTPTrade(this);
      m_pTradeApi->SetHeartBeatInterval(15);
      // 设置公有流（订单响应、成交回报）重传方式
      m_pTradeApi->SetSoftwareKey("b8aa7173bba3470e390d787219b2112e");
      m_pTradeApi->SetSoftwareVersion("0.0.0.1");
      m_pTradeApi->SubscribePublicTopic(XTP_TERT_QUICK);
      m_pTradeApi->RegisterSpi(m_pTrade);
    }
    memcpy(&_handle, &handle, sizeof(ExchangeInfo));

    return true;
}

bool XTPExchange::Release()
{
  if (m_session) {
    m_pQuoteApi->UnSubscribeAllMarketData();
    m_pQuoteApi->Logout();
    m_pTradeApi->Logout(m_session);
    m_session = 0;
  }
  if (m_pTradeApi) {
    m_pTradeApi->Release();
    m_pTradeApi = nullptr;
  }
  if (m_pTrade) {
    delete m_pTrade;
    m_pTrade = nullptr;
  }

  if (m_pQuoteApi) {
    m_pQuoteApi->Release();
    m_pQuoteApi = nullptr;
  }
  if (m_pQuote) {
    delete m_pQuote;
    m_pQuote = nullptr;
  }
  return true;
}

bool XTPExchange::Login() {
  if (IsLogin())
    return true;
  std::string quote_server_ip = _handle._quote_addr;//行情服务器ip地址
	int quote_server_port = _handle._quote_port;//行情服务器端口port
	std::string quote_username = _handle._username;//行情服务器的登陆账户名
	std::string quote_password = _handle._passwd;//行情服务器的登陆密码
	XTP_PROTOCOL_TYPE protocol_type = XTP_PROTOCOL_TCP;//Level1服务器通常使用TCP，具体以运营通知为准，Level2服务器请用UDP，公网测试环境均为TCP，以实际服务器支持的类型为准
	std::string local_ip = GetIP();//本地网卡对应的ip
  if (strcmp(_handle._local_addr, "localhost") != 0 && strcmp(_handle._local_addr, "127.0.0.1") != 0) {
    local_ip = _handle._local_addr;
  }
  if (local_ip.empty()) {
    WARN("Please config local IP addr");
    return false;
  }

	uint64_t ret = m_pQuoteApi->Login(quote_server_ip.c_str(), quote_server_port, quote_username.c_str(), quote_password.c_str(), protocol_type, local_ip.c_str());
	if (0 != ret)
	{
		// 登录失败，获取错误信息
		XTPRI* error_info = m_pQuoteApi->GetApiLastError();
    WARN("login to quote server error, {} : {}", error_info->error_id, error_info->error_msg);
		return 0;
	}
  if (m_session == 0 && !m_pQuote->Init()) {
    return 0;
  }

  // 用户请根据实际情况修改登录参数
  std::string trade_server_ip = _handle._trade_addr;
  int trade_server_port = _handle._trade_port;
  std::string account_name = _handle._username;
  std::string account_pw = _handle._passwd;

  ret = m_pTradeApi->Login(trade_server_ip.c_str(), trade_server_port, account_name.c_str(), account_pw.c_str(), XTP_PROTOCOL_TCP);
  if (ret != 0) // 登录成功
  {
    m_session = ret;
    _login_status = true;
    INFO("XTP Login OK");
    return true;
  } else // 登录失败
  {
    XTPRI* error_info = m_pTradeApi->GetApiLastError();
    WARN("login to trade server error, {} : {}", error_info->error_id, error_info->error_msg);
    return false;
  }
}

void XTPExchange::SetFilter(const QuoteFilter& filter) {
  _filter = filter;
}

void XTPExchange::QueryQuotes() {
    if (_filter._symbols.empty()) {
        if (m_pQuote->IsAllTickCapture()) {
          auto symbols = m_pQuote->GetAllSymbols();
          for (auto& symb: symbols) {
            _filter._symbols.insert(get_symbol(symb));
          }
          // TODO: 补充ETF
          // _server->
          QueryQuotes();
        } else {
          // 该逻辑调用一次返回一次
          m_pQuoteApi->QueryAllTickersFullInfo(XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SZ);
          m_pQuoteApi->QueryAllTickersFullInfo(XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SH);
        }
    }
    else {
        if (_requested)
            return;

        _requested = true;
        Map<XTP_EXCHANGE_TYPE, int> markets;
        Array<Vector<String>, 4> symbols;
        Array<XTP_EXCHANGE_TYPE, 2> market_type{ XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SH, XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SZ };

        for (auto& name : _filter._symbols) {
            // auto name = get_symbol(symbol);
            auto market = _server->GetExchange(name);
            auto mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_UNKNOWN;
            switch (market) {
            case ExchangeName::MT_Shanghai: mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SH;
                break;
            case ExchangeName::MT_Shenzhen: mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SZ;
                break;
            case ExchangeName::MT_Beijing:
            default:
                break;
            }
            markets[mt] += 1;;
            symbols[mt - 1].push_back(name);
        }
        for (auto& item : markets) {
            int index = item.first;
            int count = item.second;
            char** values = new char* [count];
            for (int i = 0; i < count; ++i) {
                auto& s = symbols[item.first - 1][i];
                values[i] = new char[s.size() + 1] {0};
                memcpy(values[i], s.c_str(), s.size());
            }
            m_pQuoteApi->SubscribeMarketData(values, count, item.first);
            for (int i = 0; i < count; ++i) {
                delete[] values[i];
            }
            delete[] values;
        }
    }
}

void XTPExchange::StopQuery() {
  if (_requested) {
    Map<XTP_EXCHANGE_TYPE, int> markets;
    Array<Vector<String>, 4> symbols;
    Array<XTP_EXCHANGE_TYPE, 2> market_type{ XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SH, XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SZ };

    for (auto& name : _filter._symbols) {
        // auto name = get_symbol(symbol);
        auto market = _server->GetExchange(name);
        auto mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_UNKNOWN;
        switch (market) {
        case ExchangeName::MT_Shanghai: mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SH;
            break;
        case ExchangeName::MT_Shenzhen: mt = XTP_EXCHANGE_TYPE::XTP_EXCHANGE_SZ;
            break;
        case ExchangeName::MT_Beijing:
        default:
            break;
        }
        markets[mt] += 1;;
        symbols[mt - 1].push_back(name);
    }
    for (auto& item : markets) {
        int index = item.first;
        int count = item.second;
        char** values = new char* [count];
        for (int i = 0; i < count; ++i) {
            auto& s = symbols[item.first - 1][i];
            values[i] = new char[s.size()];
            memcpy(values[i], s.c_str(), s.size());
        }
        m_pQuoteApi->UnSubscribeMarketData(values, count, item.first);
        for (int i = 0; i < count; ++i) {
            delete[] values[i];
        }
        delete[] values;
    }
    _requested = false;
  }
}

AccountPosition XTPExchange::GetPosition() {
    m_pTradeApi->QueryPosition(nullptr, m_session, REQUEST_POSITION);
    AccountPosition pos{};
    WAIT_FOR_REQUEST(REQUEST_POSITION) {
      pos = m_pTrade->GetPosition();
    } FINISH_WAIT()
    return pos;
}

AccountAsset XTPExchange::GetAsset() {
  m_pTradeApi->QueryAsset(m_session, REQUEST_ASSET);
  AccountAsset asset{};
  WAIT_FOR_REQUEST(REQUEST_ASSET) {
    asset = m_pTrade->GetAsset();
  } FINISH_WAIT()
  return asset;
}

bool XTPExchange::AddOrder(const String& symbol, Order& o) {
  // 获取代码对应市场
  auto type = Server::GetExchange(symbol);
  if (type == MT_Unknow) {
    return false;
  }

  XTPOrderInsertInfo* order = new XTPOrderInsertInfo;
  strcpy(order->ticker, symbol.c_str());
  switch (type) {
  case MT_Shanghai: order->market = XTP_MKT_SH_A; break;
  case MT_Shenzhen: order->market = XTP_MKT_SZ_A; break;
  case MT_Beijing: order->market = XTP_MKT_BJ_A; break;
  default:
    return false;
  }
  order->price = o._order[0]._price;
  order->quantity = o._number;

  auto order_id = m_pTradeApi->InsertOrder(order, m_session);
  if (order_id == 0) {
    XTPRI* error_info = m_pTradeApi->GetApiLastError();
    printf("ERROR: add order fail, code %d: %s\n", error_info->error_id, error_info->error_msg);
    return false;
  }
  // o._oid._id = order_id;
  return true;
}

bool XTPExchange::UpdateOrder(order_id id) {
  auto itr = _orders.find(id._id);
  if (itr == _orders.end()) {
    printf("ERROR: order %ld not exist", id._id);
    return false;
  }

  _orders.erase(itr);
  return true;
}

bool XTPExchange::CancelOrder(order_id id) {
  uint64_t order_id = id._id;
  auto cancel_id = m_pTradeApi->CancelOrder(order_id, m_session);
  if (cancel_id == 0)
    return false;

  return true;
}

OrderList XTPExchange::GetOrders() {
  XTPQueryOrderReq query_param;
  memset(&query_param, 0, sizeof(XTPQueryOrderReq));
  m_pTradeApi->QueryOrders(&query_param, m_session, REQUEST_ORDERS);
  OrderList orders;
  WAIT_FOR_REQUEST(REQUEST_ORDERS) {
    orders = m_pTrade->GetOrders();
  } FINISH_WAIT()
  return orders;
}

Order XTPExchange::GetOrder(const order_id& id) {
  m_pTradeApi->QueryOrderByXTPIDEx(id._id, m_session, REQUEST_ORDER);
  Order order;
  WAIT_FOR_REQUEST(REQUEST_ORDER) {
    order = m_pTrade->GetOrder(id);
  } FINISH_WAIT()
  return order;
}

QuoteInfo XTPExchange::GetQuote(symbol_t symbol)
{
  return m_pQuote->GetQuoteInfo(symbol);
}

bool XTPExchange::IsLogin() {
  return _login_status;
}
