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

bool StackNode::Process(const String& strategy, DataContext& context) {
    if (_hstack) {
        HStack(context);
    } else { [[likely]]
        Stack(context);
    }
    return true;
}

void StackNode::Stack(DataContext& context) {
    Eigen::MatrixXd mat(_orders.size(), _window);
    int i = 0;
    for (auto& name: _orders) {
        auto& data = context.get(name);
        Vector<double>& arr = std::get<Vector<double>>(data);
        if (arr.size() < _window)
            return;

        mat.row(i++) = Eigen::Map<Eigen::RowVectorXd>(arr.data() + arr.size() - _window - 1, _window);
    }
    context.set(_outname, mat);
}

void StackNode::HStack(DataContext& context) {
    auto size = _orders.size() *_window;
    Vector<double> temp(size);
    for (auto& name: _orders) {
        auto& data = context.get(name);
        Vector<double>& arr = std::get<Vector<double>>(data);
        if (arr.size() < _window)
            return;
        std::copy(arr.begin(), arr.end(), temp.begin());
    }
    Eigen::MatrixXd mat(1, size);
    context.set(_outname, mat);
}

const nlohmann::json StackNode::getParams() {
    return {"order", "window", "stack"};
}