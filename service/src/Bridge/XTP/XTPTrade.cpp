#include "Bridge/XTP/XTPTrade.h"
#include "Bridge/XTP/XTPExchange.h"
#include "Bridge/exchange.h"
#include "server.h"

#define NOTIFY_RESPONSE(request_id) \
  { auto& mtx = _exchange->GetMutex(request_id);\
    std::unique_lock<std::mutex> lock(mtx);\
    auto& cv = _exchange->GetCV(request_id);\
    cv.notify_all(); }

XTPTrade::XTPTrade(XTPExchange* exchange):_exchange(exchange) {

}

void XTPTrade::OnQueryPosition(XTPQueryStkPositionRsp *position, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id) {
    // _position.total_qty = position->total_qty;
    auto& mtx = _exchange->GetMutex(REQUEST_POSITION);
    std::unique_lock<std::mutex> lock(mtx);
    auto& cv = _exchange->GetCV(REQUEST_POSITION);
    cv.notify_all();
}

void XTPTrade::OnQueryAsset(XTPQueryAssetRsp* asset, XTPRI* error_info, int request_id, bool is_last, uint64_t session_id) {
  _asset.total_asset = asset->total_asset;
  NOTIFY_RESPONSE(REQUEST_ASSET);
}

void XTPTrade::OnOrderEvent(XTPOrderInfo *order_info, XTPRI *error_info, uint64_t session_id) {
#ifdef _DEBUG
  printf("Order Added:  %ld %s %f %ld\n", order_info->order_xtp_id, order_info->ticker, order_info->price, order_info->quantity);
#endif
}

void XTPTrade::OnTradeEvent(XTPTradeReport *trade_info, uint64_t session_id) {
  auto handle = _exchange->GetHandle();
  order_id id;
  id._id = trade_info->order_xtp_id;
  // 交易完成,更新订单，更新资金, 更新收益率/夏普率...
  TradeReport report;
  _exchange->OnOrderReport(id, report);
}

void XTPTrade::OnCancelOrderError(XTPOrderCancelInfo *cancel_info, XTPRI *error_info, uint64_t session_id) {
#ifdef _DEBUG
  printf("Cancel Order: %ld\n", cancel_info->order_xtp_id);
#endif
}

void XTPTrade::OnQueryOrderEx(XTPOrderInfoEx *order_info, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id) {
#ifdef _DEBUG
  printf("Query OrderEx: %ld %s %f %ld\n", order_info->order_xtp_id, order_info->ticker, order_info->price, order_info->quantity);
#endif
}

void XTPTrade::OnQueryOrder(XTPQueryOrderRsp *order_info, XTPRI *error_info, int request_id, bool is_last, uint64_t session_id) {
#ifdef _DEBUG
  printf("Query Order: %ld %s %f %ld\n", order_info->order_xtp_id, order_info->ticker, order_info->price, order_info->quantity);
#endif
  auto ptr = std::make_shared<struct Order>();
  // ptr->_oid._id = order_info->order_xtp_id;
  // ptr->price = order_info->price;
  // ptr->number = order_info->quantity;
  // memcpy(ptr->symbol, order_info->ticker, 16);
  // _orders._order.push_back(ptr);
  NOTIFY_RESPONSE(REQUEST_ORDERS);
}

AccountPosition XTPTrade::GetPosition() {
    return _position;
}

AccountAsset XTPTrade::GetAsset() {
  return _asset;
}

void XTPTrade::CancelOrder() {

}

OrderList XTPTrade::GetOrders() {
  return _orders;
}

Order XTPTrade::GetOrder(order_id id) {
  auto itr = _order.find(id);
  Order order;
  // order._oid._id = 0;
  if (itr != _order.end()) {
    order = itr->second;
    _order.erase(itr);
  }
  return order;
}
