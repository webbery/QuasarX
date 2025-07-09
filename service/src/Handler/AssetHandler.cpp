#include "Handler/AssetHandler.h"
#include "Bridge/exchange.h"
#include "tabulate/table.hpp"
#include "Util/string_algorithm.h"
#include <iostream>

AssetHandler::AssetHandler(Server* server): HttpHandler(server) {

}

void AssetHandler::doWork(const std::vector<std::string>& params) {
  if (params.empty()) {
    // for (auto exchange : _exchanges) {
    //   auto asset = exchange->GetAsset();

    //   tabulate::Table asset_table;
    //   asset_table.add_row({ std::string("Total: ") + to_dot_string(asset.total_asset)});
    //   std::cout << asset_table << std::endl;
    // }
  }
}