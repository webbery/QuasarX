#include "Decision.h"
#include "DataContext.h"

DecisionAction to_decision_action(TradeAction action, unsigned char flag) {
    if (action == TradeAction::BUY)  return flag ? DecisionAction::CloseShort : DecisionAction::OpenLong;
    if (action == TradeAction::SELL) return flag ? DecisionAction::CloseLong  : DecisionAction::OpenShort;
    return DecisionAction::OpenLong;
}

TradeAction decision_to_action(DecisionAction da) {
    return (da == DecisionAction::OpenLong || da == DecisionAction::CloseShort)
        ? TradeAction::BUY : TradeAction::SELL;
}

unsigned char decision_to_flag(DecisionAction da) {
    return (da == DecisionAction::CloseLong || da == DecisionAction::CloseShort) ? 1 : 0;
}
