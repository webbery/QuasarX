#include "json.hpp"
#include <cstdint>
#include <utility>
#include <list>
#define DATA_PATH   "data"

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

  std::string GetWarningAddr();

  std::pair<char, char> GetDailyTime();

  bool AuthenticateUser(const std::string& user, const std::string& pwd);

  std::string GetJWTKey();

  std::string GetPrivateKey();
  std::string GetPublicKey();
  std::string GetIssuer();
  
  float GetFreeRate();
  void SetFreeRate(float rate);
  short GetTradeDays();
  void SetTradeDays(short days);

  std::list<std::pair<std::string, std::string>> GetStockAccounts();
  void DeleteStockAccount(const std::string& name);
  void AddStockAccount(const std::string& name, const std::string& pwd);

  nlohmann::json& GetStockLimits();

private:
    void Init();

private:
  bool _status = false;

  nlohmann::json _config;

  uint64_t _max_risk_id;

  std::string _path;

  std::string _server_key;
  std::string _pub_key;
};