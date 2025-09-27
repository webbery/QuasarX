#include "Transfer.h"
#include "Util/system.h"
#include <exception>

ITransfer::ITransfer():_worker(nullptr), _running(true) {

}

ITransfer::~ITransfer() {
    if (_worker) {
        _worker->join();
        delete _worker;
    }
}

void ITransfer::stop() {
    _running = false;
}

void ITransfer::start(const String& name, const String& from, const String& to) {
    if (_worker) {
        _worker->join();
        delete _worker;
    }
    if (!to.empty()) {
        _worker = new std::thread(static_cast<void (ITransfer::*)(const String&, const String&, const String&)>(&Transfer::run), this, name, from, to);
    } else {
        _worker = new std::thread(static_cast<void (ITransfer::*)(const String&, const String&)>(&Transfer::run), this, name, from);
    }
}

void ITransfer::run(const String& name, const String& from, const String& to) {
    if (!Subscribe(from, _recv)) {
        WARN("subscribe {} fail.", from);
        return;
    }
    if (!Publish(to, _send)) {
        WARN("publish {} fail.", to);
        return;
    }
    _running = true;
    SetCurrentThreadName(name.c_str());
    while (_running) {
        if (!work(_recv, _send)) {
            break;
        }
    }
    nng_close(_send);
    nng_close(_recv);
}

void ITransfer::run(const String& name, const String& from) {
    if (!Subscribe(from, _recv)) {
        WARN("subscribe {} fail.", from);
        return;
    }
    _running = true;
    SetCurrentThreadName(name.c_str());
    while (_running) {
        if (!work(_recv, _send)) {
            break;
        }
    }
    nng_close(_send);
}

Transfer::Transfer(transfer_function func):_func(func) {

}

bool Transfer::work(nng_socket& from, nng_socket& to) {
    try {
        return _func(from, to);
    } catch (const std::exception& e) {
        WARN("Transfer exception: {}", e.what());
        return true;
    }
}
