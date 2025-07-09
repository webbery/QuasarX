#include "Transfer.h"
#include "Util/log.h"
#include "Util/system.h"

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
    _worker = new std::thread(&Transfer::run, this, name, from, to);
}

void ITransfer::run(const String& name, const String& from, const String& to) {
    if (!Subscribe(from, _send)) {
        WARN("subscribe {} fail.", from);
        return;
    }
    if (!Publish(to, _recv)) {
        WARN("publish {} fail.", to);
        return;
    }
    SetCurrentThreadName(name.c_str());
    while (_running) {
        if (!work(_recv, _send)) {
            break;
        }
    }
    nng_close(_send);
    nng_close(_recv);
}

Transfer::Transfer(transfer_function func):_func(func) {

}

bool Transfer::work(nng_socket& from, nng_socket& to) {
    return _func(from, to);
}
