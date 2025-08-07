#include "Features/Scaler.h"

ScalerNode::ScalerNode(const nlohmann::json& params) {

}
ScalerNode::~ScalerNode() {

}

size_t ScalerNode::id() {
    String name = desc();
    return std::hash<String>()(name);
}

bool ScalerNode::plug(Server* handle, const String& account) {
    return true;
}

feature_t ScalerNode::deal(const QuoteInfo& quote, double extra) {
    feature_t f;
    return f;
}

const char* ScalerNode::desc() {

}
