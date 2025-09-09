#include "Handler/FeatureHandler.h"
#include "server.h"

FeatureHandler::FeatureHandler(Server* server)
:HttpHandler(server) {

}

void FeatureHandler::get(const httplib::Request& req, httplib::Response& res) {

}