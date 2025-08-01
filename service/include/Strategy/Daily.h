#pragma once
#include "Strategy.h"

class Server;
class DailyStrategy : public IStrategy {
public:
    DailyStrategy(Server* handle);

    int generate(const Vector<float>& prediction);

    bool is_valid();

private:
    Server* _handle;
};
