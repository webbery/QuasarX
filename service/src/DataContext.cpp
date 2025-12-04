#include "DataContext.h"
#include "std_header.h"
#include <type_traits>
#include <variant>

feature_t& DataContext::get(const String& name) {
    return _outputs[name];
}

const feature_t& DataContext::get(const String& name) const {
    return _outputs.at(name);
}

void DataContext::add(const String& name, feature_t value) {
    std::visit([this, &name, &value](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, uint64_t>) {
            auto itr = _outputs.find(name);
            if (itr == _outputs.end()) {
                _outputs[name] = Vector<T>{std::get<T>(value)};
            } else { [[likely]]
                std::get<Vector<T>>(itr->second).push_back(std::get<T>(value));
            }
        }
        else {
            set(name, value);
        }
    }, value);
}

bool DataContext::exist(const String& name) {
    return _outputs.contains(name);
}

void DataContext::SetTime(time_t t) {
    _times.push_back(t);    
}

const List<time_t>& DataContext::GetTime() const {
    return _times;
}

time_t DataContext::Current() {
    if (_times.empty())
        return 0;
    return _times.back();
}

