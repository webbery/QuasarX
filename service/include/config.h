#include "json.hpp"
#include <cstdint>

class ServerConfig {
public:
  ServerConfig(const std::string& path);
  ~ServerConfig();

  void Flush();

  bool IsOK() {return _status;}

  std::string GetHost();
  std::string GetHost() const;
  uint16_t GetPort();

  bool HasDefault();

  nlohmann::json GetDefault();
  
  nlohmann::json GetExchanges();

  std::string GetDatabasePath();

  nlohmann::json& GetSchemas();
  
  const nlohmann::json& GetExchangeByAPI(const std::string& name) const;
  const nlohmann::json& GetExchangeByName(const std::string& name) const;

  const nlohmann::json& GetBrokerByName(const std::string& name) const;

  const nlohmann::json& GetAllStopLoss() const;
  uint64_t AddStopLoss(const nlohmann::json& sl);
  void DeleteStopLoss(int id);
  const nlohmann::json& GetStopLoss(int id);

  std::string GetSMTPSender();
  std::string GetSMTPPasswd();

private:
  bool _status = false;

  nlohmann::json _config;

  uint64_t _max_risk_id;

  std::string _path;
};