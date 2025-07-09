#include "Handler/AccountHandler.h"

AccountHandler::AccountHandler(Server* server):HttpHandler(server) {

}

void AccountHandler::doWork(const std::vector<std::string>& params) {
  printf("Account do work\n");
}