#include "Nodes/QuoteNode.h"
#include "Util/string_algorithm.h"

QuoteInputNode::QuoteInputNode(Server* server): _server(server) {

}

bool QuoteInputNode::Process(DataContext& context, const DataFeatures& org)
{
    // 
    for (int i = 0; i < org._names.size(); ++i) {
        if (_validDatumNames.count(org._names[i])) {
            context.add(org._names[i], org._data[i]);
        }
    }
    return true;
}

void QuoteInputNode::Connect(QNode* next, const String& from, const String& to) {
    Vector<String> froms;
    split(from, froms, "_");
    QNode::Connect(next, froms[0], to);
    if (froms.size() == 2) {
        // auto id = get_feature_id(froms[1], "");
        _validDatumNames[froms[1]] = froms[1];
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
