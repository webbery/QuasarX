#pragma once
#include "xtp/xtp_trader_api.h"
#include "../exchange.h"
#include <map>

class XTPExchange;
class XTPTrade: public XTP::API::TraderSpi {
public:
  XTPTrade(XTPExchange* exchange);

  virtual void OnQueryPosition(XTPQueryStkPositionRsp *position, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id);

  virtual void OnQueryAsset(XTPQueryAssetRsp* asset, XTPRI* error_info, int request_id, bool is_last, uint64_t session_id);

  virtual void OnOrderEvent(XTPOrderInfo *order_info, XTPRI *error_info, uint64_t session_id);

  virtual void OnTradeEvent(XTPTradeReport *trade_info, uint64_t session_id);

  virtual void OnCancelOrderError(XTPOrderCancelInfo *cancel_info, XTPRI *error_info, uint64_t session_id);

  virtual void OnQueryOrderEx(XTPOrderInfoEx *order_info, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id);
  
  virtual void OnQueryOrder(XTPQueryOrderRsp *order_info, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id);

  AccountPosition GetPosition();

  AccountAsset GetAsset();

  void Order();

  void CancelOrder();

  OrderList GetOrders();

  struct Order GetOrder(order_id id);

private:
  XTPExchange* _exchange;

  AccountPosition _position;
  AccountAsset _asset;
  OrderList _orders;
  std::map<order_id, struct Order> _order;

};