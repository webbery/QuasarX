#pragma once
#include "DataGroup.h"
#include "HttpHandler.h"
#include "json.hpp"
#include <cstddef>
#include "Util/string_algorithm.h"

class Server;
class StockHandler :public HttpHandler {
public:
  StockHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

  void checkHelp();

private:
  void display(const std::vector<std::string>& cols, const std::list<std::tuple<std::string, double, double>>& data);

private:
};

class StockHistoryHandler: public HttpHandler {
public:
  StockHistoryHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

private:
  template<FixedString... Strs>
  struct QuoteExtractor {
      // // 编译时遍历展开
      // template<typename T>
      // static constexpr void extract(nlohmann::json& data, std::shared_ptr<DataGroup> group, const String& symbol, size_t i) {
      //     ((data[Strs.value] = group->Get<T>(symbol, Strs.value, i)), ...);
      // }
      template<typename T>
      static constexpr void extract(nlohmann::json& data, DataFrame& df, size_t i) {
          ((data[Strs.value] = df.get_column<T>(Strs.value)[i]),...);
      }
  };

};

class StockDetailHandler : public HttpHandler {
public:
  StockDetailHandler(Server* server);

  virtual void get(const httplib::Request& req, httplib::Response& res);

private:
};

class StockPrivilege : public HttpHandler {
public:
    StockPrivilege(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
};

class StockParams : public HttpHandler {
public:
    StockParams(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void put(const httplib::Request& req, httplib::Response& res);
};