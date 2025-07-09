#pragma once
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "Util/system.h"
#include <mutex>

struct StopLossInfo {
  double _price;
  union {
    double _percent;
  };
};

class IStopLoss {
public:
  virtual ~IStopLoss() {}

  /**
   * @param quotes 行情信息
   * @param sells 满足止损要求的代码
   */
  virtual void check(const Map<symbol_t, QuoteInfo>& quotes, List<symbol_t>&& sells) = 0;

  virtual void add(const symbol_t& symbol, const StopLossInfo& info) = 0;
  virtual void del(const List<symbol_t>& symbol) = 0;
  virtual void update(const symbol_t& symbol, const StopLossInfo& info) = 0;
};

/**
 * 百分比策略
 */
class SLPercentage : public IStopLoss {
public:
  typedef std::pair<double, double> PercentType;
  SLPercentage(){}
  SLPercentage(const Map<symbol_t, PercentType>& symbols):_org_price_map(symbols) {}

  virtual void check(const Map<symbol_t, QuoteInfo>&, List<symbol_t>&& sells);

  virtual void add(const symbol_t& symbol, const StopLossInfo& info);
  virtual void del(const List<symbol_t>& symbol);
  virtual void update(const symbol_t& symbol, const StopLossInfo& info);
private:
  Map<symbol_t, PercentType> _org_price_map;
  std::mutex _mutex;
};

class StepPercentage {
public:
  struct PercentType {
    double _org_price;
    double _percent;
    double _lower;
  };

  StepPercentage(){}

  virtual void check(const Map<symbol_t, QuoteInfo>&, List<symbol_t>&& sells);

  virtual void add(const symbol_t& symbol, const StopLossInfo& info);
  virtual void del(const List<symbol_t>& symbol);
  virtual void update(const symbol_t& symbol, const StopLossInfo& info);
private:
  Map<symbol_t, PercentType> _price_map;
  std::mutex _mutex;
};

class ATR: public IStopLoss {
public:
  ATR() {}

  virtual void check(const Map<symbol_t, QuoteInfo>&, List<symbol_t>&& sells);

  virtual void add(const symbol_t& symbol, const StopLossInfo& info);
  virtual void del(const List<symbol_t>& symbol);
  virtual void update(const symbol_t& symbol, const StopLossInfo& info);

private:
  
};