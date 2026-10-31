#pragma once
#include "std_header.h"
#include "Util/system.h"

// 券商账本提供者：启动时拉取券商侧账户状态，与本地账本比对。
// 券商账本只做校验，不覆盖本地账本：本地账本是下单依据，券商账本是真实账户的事实，
// 两者不一致说明交易员在券商端手工动过仓，需要显式告警而不是静默覆盖。
//
// TODO(券商接入)：当前没有实现类。接入真实券商时新增实现（例如 HXBrokerLedger），
// 复用券商 SDK 的持仓与资金查询回报填出 AccountSnapshot，
// 再由 Server::InitDefault 按 config.json 的 broker.api 选择实现并注册到
// BrokerSubSystem::SetBrokerLedger()。
// 券商侧的融资融券余额（融资余额、在借股数、可用保证金）需要单独查询，
// 现有 ExchangeInterface 的 GetPosition/GetAsset/GetAvailableFunds 不覆盖这部分。

// 券商账本快照
struct BrokerAccount {
    double _cash = 0.0;              // 可用资金
    double _borrowedCash = 0.0;      // 融资余额
    Map<String, int64_t> _borrowedShares;  // 融券在借数量（按标的）
    Map<symbol_t, Position> _positions;    // 持仓
};

class IBrokerLedger {
public:
    virtual ~IBrokerLedger() = default;

    virtual String Name() const = 0;
    virtual bool IsConnected() const = 0;
    virtual bool QueryAccount(BrokerAccount& out) = 0;
};

// 未接入券商时的占位实现
class NullBrokerLedger : public IBrokerLedger {
public:
    String Name() const override { return "null"; }
    bool IsConnected() const override { return false; }
    bool QueryAccount(BrokerAccount& out) override {
        (void)out;
        return false;
    }
};
