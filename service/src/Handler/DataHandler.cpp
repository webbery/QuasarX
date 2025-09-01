#include "Handler/DataHandler.h"
#include "server.h"

DataHandler::DataHandler(Server* server): HttpHandler(server) {

}

void DataHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& cfg = _server->GetConfig();
    
}