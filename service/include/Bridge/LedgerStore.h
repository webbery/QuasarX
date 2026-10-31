#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "json.hpp"
#include <mutex>

// 统一账本：账户状态（资金 / 持仓 / 两融敞口）的唯一权威副本。
// 写入口只有 ApplyFill()（增量）与 SyncBrokerMirror()（券商账本镜像，不覆盖）。
// 其余子系统通过本类接口读取同一份数据。

// 信用交易（融资融券）成交类型
enum class CreditAction {
    None,        // 非信用成交
    MarginBuy,   // 融资买入
    ShortSell,   // 融券卖出
    ShortCover,  // 买入平空
};

// 信用交易（融资融券）敞口
struct StrategyCreditState {
    double borrowedCash = 0.0;              // 融资余额（借入现金合计）
    Map<String, int64_t> borrowedShares;    // 融券在借数量（按标的）
    Map<String, double> shortAvgPrice;      // 开空均价（按标的）
    Map<String, double> financedAmount;     // 融资本金（按标的）
};

// FIFO 分批（持仓成本明细）
struct LedgerLot {
    int64_t _quantity = 0;
    double  _price = 0.0;
    time_t  _date = 0;
};
using LedgerLots = Map<symbol_t, List<LedgerLot>>;

// 一笔成交
struct LedgerFill {
    symbol_t _symbol{};
    int64_t  _quantity = 0;          // 正 = 买入，负 = 卖出
    double   _price = 0.0;
    double   _commissionRate = 0.0;  // 佣金费率
    double   _slippageRate = 0.0;    // 滑点费率
    double   _tradeFees = 0.0;       // 单笔固定费用
    bool     _isMargin = false;      // 是否融资交易
    double   _marginRatio = 1.0;     // 初始保证金比例（<1 才是融资买入）
    bool     _isShortSell = false;   // 融券卖出（开空）
    bool     _isShortCover = false;  // 买入平空
    time_t   _time = 0;

    double Gross() const { return static_cast<double>(_quantity) * _price; }
    double FeeRate() const { return _commissionRate + _slippageRate; }
};

// 券商账本镜像：启动时拉取，与本地账本比对，不覆盖本地账本
struct BrokerMirror {
    bool     _valid = false;
    String   _provider;
    time_t   _syncedAt = 0;
    double   _cash = 0.0;
    double   _borrowedCash = 0.0;
    Map<String, int64_t> _borrowedShares;
    Map<symbol_t, Position> _positions;

    bool     _matched = false;       // 与本地账本无差异
    double   _cashDiff = 0.0;        // 本地现金 − 券商现金
    List<String> _positionDiff;      // 形如 "sz.300450: local=11400 broker=0"
};

// 账本
class LedgerStore {
public:
    // 单个策略的账户
    struct Account {
        double   _allocated = 0.0;    // 分配额度
        double   _cash = 0.0;         // 自有现金
        Map<symbol_t, Position> _positions;  // 聚合持仓
        LedgerLots _lots;             // FIFO 分批
        StrategyCreditState _credit;  // 两融敞口（borrowedCash 即融资余额）
        double   _profit = 0.0;       // 累计已实现盈亏
        bool     _active = true;
    };

    LedgerStore() = default;
    ~LedgerStore() = default;

    // ── 资金分配 ───────────────────────────────────────────────
    bool Allocate(const String& strategy, double amount);   // 额度不足返回 false
    double Reclaim(const String& strategy);                 // 回收额度，只置非活跃
    void Deactivate(const String& strategy);
    void ReclaimAll();
    bool HasStrategy(const String& strategy) const;
    int GetActiveStrategyCount() const;

    double GetInitialCapital() const { return _initialCapital; }
    void SetInitialCapital(double v) { _initialCapital = v; }
    double GetTotalAllocated() const;
    double GetTotalAvailable() const;

    // ── 现金 ───────────────────────────────────────────────────
    double GetAvailable(const String& strategy) const;   // 自有现金
    double GetUsed(const String& strategy) const;        // 分配额度 − 可用现金
    double GetAllocated(const String& strategy) const;
    void SetAvailable(const String& strategy, double available);
    double UpdateAvailable(const String& strategy, double delta);

    // ── 账户 ───────────────────────────────────────────────────
    // 全部策略的账户快照（用于对外展示）
    Map<String, Account> GetAccounts() const;

    // ── 持仓 ───────────────────────────────────────────────────
    Map<symbol_t, Position> GetPositions(const String& strategy) const;
    int64_t GetQuantity(const String& strategy, symbol_t symbol) const;
    const LedgerLots& GetLots(const String& strategy) const;
    double GetPositionCost(const String& strategy, symbol_t symbol) const;
    double GetPositionValue(const String& strategy) const;
    void UpdatePrice(const String& strategy, symbol_t symbol, double price);
    bool HasPosition(const String& strategy, symbol_t symbol) const;

    // ── 两融 ───────────────────────────────────────────────────
    StrategyCreditState GetCreditState(const String& strategy) const;
    double GetBorrowedCash(const String& strategy) const;
    double GetEquity(const String& strategy) const;      // 现金 + 持仓市值 − 融资负债
    double GetMarginEquity(const String& strategy) const;

    // ── 盈亏 ───────────────────────────────────────────────────
    void AddProfit(const String& strategy, double profit);
    double GetProfit(const String& strategy) const;

    // ── 写入口 1：成交回写 ─────────────────────────────────────
    // 返回本次现金变化（负 = 支出）。
    // 买入：自有占用 = gross × marginRatio，借入 = gross × (1 − marginRatio)，
    //       现金只扣自有占用与费用，差额计入融资余额。
    // 卖出：卖出所得先按比例偿还该标的的融资本金，偿还部分从现金中扣除。
    // marginRatio ≥ 1 时按普通现金账户处理：全额扣现金，不产生负债。
    double ApplyFill(const String& strategy, const LedgerFill& fill);

    // ── 写入口 2：券商账本同步 ─────────────────────────────────
    // 记录券商账本镜像并与本地账本比对，返回 false 表示券商不可用。
    bool SyncBrokerMirror(const String& strategy, const String& provider,
                          double cash, double borrowedCash,
                          const Map<String, int64_t>& borrowedShares,
                          const Map<symbol_t, Position>& positions);
    const BrokerMirror* GetBrokerMirror(const String& strategy) const;

    // ── 版本号 ─────────────────────────────────────────────────
    uint64_t GetVersion(const String& strategy) const;

    // ── 持久化 ─────────────────────────────────────────────────
    bool Save(const String& path) const;
    bool Load(const String& path);

    // ── 从成交流水重建 ─────────────────────────────────────────
    struct RestoreFill {
        symbol_t _symbol{};
        int64_t  _quantity = 0;
        double   _price = 0.0;
        bool     _isOpen = false;
        time_t   _time = 0;
    };
    // 用成交流水重建该策略的持仓与成本，返回重建出的持仓标的数
    int RestoreFromFills(const String& strategy, const List<RestoreFill>& fills);

private:
    Account& _account(const String& strategy);
    const Account* _find(const String& strategy) const;
    void _touch(const String& strategy);
    static int64_t QuantityOf(const Account& acc, symbol_t symbol);

    mutable std::mutex _mutex;
    Map<String, Account> _accounts;
    Map<String, BrokerMirror> _mirrors;
    Map<String, uint64_t> _versions;
    double _initialCapital = 0.0;
};
