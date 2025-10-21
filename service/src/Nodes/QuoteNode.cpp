#include "Nodes/QuoteNode.h"
#include "Util/string_algorithm.h"

QuoteInputNode::QuoteInputNode(Server* server): _server(server) {

}

feature_t QuoteInputNode::Process(const DataFeatures& org, const feature_t& input)
{
    // 
    Vector<double> result;
    for (int i = 0; i < org._features.size(); ++i) {
        if (_validDatumNames.count(org._features[i])) {
            result.push_back(org._data[i]);
        }
    }
    return result;
}

void QuoteInputNode::Connect(QNode* next, const String& from, const String& to) {
    Vector<String> froms;
    split(from, froms, "_");
    QNode::Connect(next, froms[0], to);
    if (froms.size() == 2) {
        auto id = get_feature_id(froms[1], "");
        _validDatumNames[id] = froms[1];
    }
}

bool QuoteInputNode::Init() {
    // String source = URI_RAW_QUOTE;
    // if (!Subscribe(source, _sock)) {
    //     WARN("subscribe {} fail.", source);
    //     return false;
    // }
    return true;
}
