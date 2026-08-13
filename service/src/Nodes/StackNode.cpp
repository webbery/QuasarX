#include "Nodes/StackNode.h"
#include "Eigen/src/Core/Matrix.h"
#include "Util/string_algorithm.h"
#include "std_header.h"
#include <algorithm>

bool StackNode::Init(const nlohmann::json& config) {
    // 堆叠要素的顺序
    String order_names = config["params"]["order"]["value"];
    split(order_names, _orders, ",");

    if (config["params"].contains("window")) {
        _window = config["params"]["window"]["value"];
    }

    _hstack = (int)config["params"]["stack"]["value"];
    _outname = (String)config["label"];
    return true;
}

NodeProcessResult StackNode::Process(const String& strategy, DataContext& context) {
    if (_hstack) {
        HStack(context);
    } else { [[likely]]
        Stack(context);
    }
    return NodeProcessResult::Success;
}

void StackNode::Stack(DataContext& context) {
    int rows = static_cast<int>(_orders.size());
    Vector<double> flat;
    flat.reserve(rows * _window);
    for (auto& name: _orders) {
        auto& data = context.get(name);
        Vector<double>& arr = std::get<Vector<double>>(data);
        if (arr.size() < _window)
            return;
        flat.insert(flat.end(), arr.end() - _window, arr.end());
    }
    context.set(_outname, flat);
}

void StackNode::HStack(DataContext& context) {
    auto size = _orders.size() * _window;
    Vector<double> temp(size);
    size_t offset = 0;
    for (auto& name: _orders) {
        auto& data = context.get(name);
        Vector<double>& arr = std::get<Vector<double>>(data);
        if (arr.size() < _window)
            return;
        std::copy(arr.end() - _window, arr.end(), temp.begin() + offset);
        offset += _window;
    }
    context.set(_outname, temp);
}

const nlohmann::json StackNode::getParams() {
    return {"order", "window", "stack"};
}