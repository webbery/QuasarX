#pragma once
#include "std_header.h"
#include "nng/nng.h"
#include <functional>
#include <thread>

class ITransfer {
public:
    ITransfer();
    virtual ~ITransfer();

    virtual bool work(nng_socket& from, nng_socket& to) = 0;

    void stop();
    void start(const String& name, const String& from, const String& to);

    bool is_running() { return _running; }

private:
    void run(const String& name, const String& from, const String& to);

private:
    std::thread* _worker;

    nng_socket _recv;
    nng_socket _send;

    bool _running;
};

class Transfer : public ITransfer {
    typedef std::function<bool (nng_socket& from, nng_socket& to)> transfer_function;
public:
    Transfer(transfer_function worker);

    virtual bool work(nng_socket& from, nng_socket& to);

private:
    transfer_function _func;
};
