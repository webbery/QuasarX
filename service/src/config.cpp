#include "config.h"
#include <filesystem>
#include <fstream>
#include <utility>
#include "Util/string_algorithm.h"

using namespace std;

ServerConfig::ServerConfig(const std::string& path):_path(path) {
    if (!filesystem::exists(path)) {
        Init();
    }
    try {
        std::ifstream ifs;
        ifs.open(path);
        std::string content((std::istreambuf_iterator<char>(ifs)),
            (std::istreambuf_iterator<char>())); // 读取所有内容
        _config = nlohmann::json::parse(content);
        ifs.close();
    } catch(...) {
        FATAL("read config {} fail.", path);
        return;
    }
    _status = true;

    _max_risk_id = 0;
    for (auto& risk: _config["risk"]) {
        int id = risk["id"];
        if (id > _max_risk_id) {
            _max_risk_id = id;
        }
    }
    // 
    std::string key_file("server.key");
    if (std::filesystem::exists(key_file)) {
        std::ifstream ifs;
        ifs.open(key_file);
        if (!ifs.is_open()) {
            LOG("load server.key fail.");
            return;
        }
        std::stringstream buffer;  
        buffer << ifs.rdbuf();  
        _server_key = buffer.str();
        ifs.close();
    }
    std::string pub_file("server.crt");
    if (std::filesystem::exists(pub_file)) {
        std::ifstream ifs;
        ifs.open(pub_file);
        if (!ifs.is_open()) {
            LOG("load server.crt fail.");
            return;
        }
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        _pub_key = buffer.str();
        ifs.close();
    }
}

ServerConfig::~ServerConfig() {
    
}

void ServerConfig::Flush() {
    std::ofstream ofs(_path);
    ofs << _config;
    ofs.close();
}

std::string ServerConfig::GetSMTPSender() {
    return _config["server"]["smtp"]["mail"].dump();
}

std::string ServerConfig::GetSMTPPasswd() {
    return _config["server"]["smtp"]["auth"].dump();
}

const nlohmann::json& ServerConfig::GetAllStopLoss() const {
    return _config["risk"];
}

uint64_t ServerConfig::AddStopLoss(const nlohmann::json& sl) {
    ++_max_risk_id;
    auto new_loss = sl;
    new_loss["id"] = _max_risk_id;
    _config["risk"].emplace_back(new_loss);
    return _max_risk_id;
}

void ServerConfig::DeleteStopLoss(int id) {
    auto& sl = _config["risk"];
    for (int i = 0; i < sl.size(); ++i) {
        if (sl[i]["id"] == id) {
            sl.erase(sl.begin() + i);
            break;
        }
    }
}

const nlohmann::json& ServerConfig::GetStopLoss(int id) {
    for (auto& item: _config["risk"]) {
        if (item["id"] == id)
            return item;
    }
    return nullptr;
}

bool ServerConfig::HasDefault() {
    return _config["server"].contains("default");
}

nlohmann::json ServerConfig::GetDefault() {
    return _config["server"]["default"];
}

std::string ServerConfig::GetHost() {
    return _config["server"]["addr"];
}

std::string ServerConfig::GetHost() const
{
  return _config["server"]["addr"];
}

uint16_t ServerConfig::GetPort() {
    return _config["server"]["port"];
}

nlohmann::json ServerConfig::GetExchanges() {
    return _config["exchange"];
}

std::string ServerConfig::GetDatabasePath() {
    return _config["server"]["db_path"];
}

nlohmann::json& ServerConfig::GetSchemas() {
    return _config["server"]["schema"];
}

const nlohmann::json& ServerConfig::GetExchangeByAPI(const std::string& name) const{
    auto& exchanges = _config["exchange"];
    for (auto& setting: exchanges) {
        if (setting["api"] == name) {
            return setting;
        }
    }
    return exchanges.front();
}

const nlohmann::json& ServerConfig::GetExchangeByName(const std::string& name) const {
    if (_config.empty()) {
        printf("Empty json.\n");
    }
    auto& exchanges = _config["exchange"];
    for (auto& setting: exchanges) {
        if (setting["name"] == name) {
            return setting;
        }
    }
    return exchanges.front();
}

const nlohmann::json& ServerConfig::GetBrokerByName(const std::string& name) const {
    auto& brokers = _config["broker"];
    for (auto& broker: brokers) {
        if (broker["name"] == name)
            return broker;
    }
    return brokers.front();
}

std::string ServerConfig::GetWarningAddr() {
    std::string email = _config["server"]["notice"]["email"];
    return email;
}

std::pair<char, char> ServerConfig::GetDailyTime() {
    std::string daily = _config["server"]["default"]["daily"];
    std::vector<char> info;
    split(daily, info, ":");
    if (info.size() != 2) {
        return std::make_pair(20, 0);
    }
    return std::make_pair(info[0], info[1]);
}

bool ServerConfig::AuthenticateUser(const std::string& user, const std::string& pwd) {
    std::string name = _config["server"]["user"];
    if (name.empty()) {
        WARN("login user is not config.");
        return false;
    }
    if (name != user)
        return false;

    std::string password = _config["server"]["passwd"];
    if (password.empty()) {
        WARN("user password has not set.");
        return false;
    }
    return password == pwd;
}

std::string ServerConfig::GetJWTKey() {
    return _config["server"]["jwt"];
}

std::string ServerConfig::GetPrivateKey() {
    return _server_key;
}

std::string ServerConfig::GetPublicKey()
{
    return _pub_key;
}

std::string ServerConfig::GetIssuer() {
    return "QuasarX";
}

float ServerConfig::GetFreeRate() {
    if (_config["server"]["default"].contains("freerate")) {
        return _config["server"]["default"]["freerate"];
    }
    return 0.015;
}

void ServerConfig::SetFreeRate(float rate) {
    _config["server"]["default"]["freerate"] = rate;
}

short ServerConfig::GetTradeDays() {
    if (_config["server"]["default"].contains("trade_days")) {
        return _config["server"]["default"]["trade_days"];
    }
    return 252;
}

void ServerConfig::SetTradeDays(short days) {
    _config["server"]["default"]["trade_days"] = days;
}

std::list<std::pair<std::string, std::string>> ServerConfig::GetStockAccounts() {
    auto& accounts = _config["server"]["accounts"];
    Vector<std::pair<std::string, std::string>> container(accounts.size());
    for (auto& item: accounts) {
        String type = item["t"];
        if (type != "stock")
            continue;

        String name = item["n"];
        String pwd = item["p"];
        int index = item["i"];
        container[index] = std::move(make_pair(name, pwd));
    }
    return {container.begin(), container.end()};
}

void ServerConfig::DeleteStockAccount(const std::string& name) {
    auto& accounts = _config["server"]["accounts"];
    for (auto itr = accounts.begin(); itr != accounts.end(); ++itr) {
        if ((*itr)["n"] == name) {
            accounts.erase(itr);
            break;
        }
    }
}

void ServerConfig::AddStockAccount(const std::string& name, const std::string& pwd) {
    auto& accounts = _config["server"]["accounts"];
    nlohmann::json acc;
    acc["n"] = name;
    acc["p"] = pwd;
    acc["t"] = "stock";
    acc["i"] = accounts.size();
    accounts.emplace_back(std::move(acc));
}

nlohmann::json& ServerConfig::GetStockLimits()
{
    return _config["limit"]["stock"];
}

void ServerConfig::Init()
{
    nlohmann::json config;
    nlohmann::json broker;
    broker["commission"] = { {"fee", 0.0001345}, {"min", 5.0}, {"stamp", 0.001} };
    broker["db"] = DATA_PATH "/broker";
    broker["name"] = "astock";
    broker["type"] = "stock";
    config["broker"].emplace_back(std::move(broker));
    nlohmann::json sim_exchange;
    sim_exchange["api"] = "sim";
    sim_exchange["name"] = "stock-sim";
    sim_exchange["pool"] = std::vector<String>();
    sim_exchange["quote"] = DATA_PATH;
    sim_exchange["trade"] = "";
    sim_exchange["type"] = "stock";
    sim_exchange["desc"] = "";
    config["exchange"].emplace_back(std::move(sim_exchange));
    config["risk"] = std::vector<String>();
    nlohmann::json server;
    server["addr"] = "localhost";
    server["db_path"] = DATA_PATH;
    server["default"] = {
        {"broker", "astock"},
        {"daily", "20:00"},
        {"exchange", {"stock-sim"}},
        {"freerate", 0.0175},
        {"record", {"*"}},
        {"strategy", {}},
    };
    server["jwt"] = "2025_09_jwt_update_key";
    server["notice"] = { {"email", ""} };
    server["passwd"] = "admin";
    server["user"] = "admin";
    server["port"] = 19107;
    server["smtp"] = { {"addr", ""}, {"auth", ""}, {"mail", ""} };
    server["ssl"] = "";
    config["server"] = std::move(server);
    std::ofstream ofs(_path);
    ofs << config;
    ofs.close();
}

