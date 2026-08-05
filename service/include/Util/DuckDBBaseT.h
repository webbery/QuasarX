#pragma once
#include "duckdb.h"
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

// CRTP 模板基类：统一 DuckDB 单例管理
// 派生类需实现 ensureTables()，通过 friend class DuckDBBaseT<Derived> 访问
template <typename Derived>
class DuckDBBaseT {
public:
    // 初始化 db 文件（自动建目录）
    bool init(const std::string& db_dir, const std::string& db_filename) {
        if (initialized_) return true;
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        if (initialized_) return true;

        std::filesystem::create_directories(db_dir);
        std::string db_path = db_dir + "/" + db_filename;

        if (duckdb_open_ext(db_path.c_str(), &db_, nullptr, nullptr) != DuckDBSuccess) {
            return false;
        }
        if (duckdb_connect(db_, &conn_) != DuckDBSuccess) {
            duckdb_close(&db_);
            db_ = nullptr;
            return false;
        }

        exec_unsafe("PRAGMA threads=2");
        static_cast<Derived*>(this)->ensureTables();

        initialized_ = true;
        return true;
    }

    // 关闭：CHECKPOINT + disconnect + close
    void shutdown() {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        if (!initialized_) return;
        initialized_ = false;
        if (conn_) {
            duckdb_result r;
            duckdb_query(conn_, "CHECKPOINT", &r);
            duckdb_destroy_result(&r);
            duckdb_disconnect(&conn_);
            conn_ = nullptr;
        }
        if (db_) {
            duckdb_close(&db_);
            db_ = nullptr;
        }
    }

    // 执行 SQL（自动加锁）
    bool exec(const std::string& sql) {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        return exec_unsafe(sql);
    }

    // 查询（回调方式处理结果集）
    bool query(const std::string& sql,
               std::function<bool(duckdb_result&)> row_handler) {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        duckdb_result result;
        if (duckdb_query(conn_, sql.c_str(), &result) != DuckDBSuccess) {
            duckdb_destroy_result(&result);
            return false;
        }
        bool ok = row_handler ? row_handler(result) : true;
        duckdb_destroy_result(&result);
        return ok;
    }

    bool isInitialized() const { return initialized_; }

    // 子类访问句柄（必须已持锁）
    duckdb_connection conn() const { return conn_; }
    duckdb_database db() const { return db_; }
    std::recursive_mutex& mtx() { return mtx_; }

    // 跨 DB 一次性连接（RAII，替代 ATTACH/DETACH）
    class ScopedConnection {
    public:
        explicit ScopedConnection(const std::string& db_path) {
            if (duckdb_open(db_path.c_str(), &db_) == DuckDBSuccess) {
                duckdb_connect(db_, &conn_);
            }
        }
        ~ScopedConnection() {
            if (conn_) duckdb_disconnect(&conn_);
            if (db_) duckdb_close(&db_);
        }
        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;
        ScopedConnection(ScopedConnection&&) = delete;
        ScopedConnection& operator=(ScopedConnection&&) = delete;

        duckdb_connection conn() const { return conn_; }
        bool valid() const { return conn_ != nullptr; }

        bool exec(const std::string& sql) {
            if (!conn_) return false;
            duckdb_result result;
            if (duckdb_query(conn_, sql.c_str(), &result) != DuckDBSuccess) {
                duckdb_destroy_result(&result);
                return false;
            }
            duckdb_destroy_result(&result);
            return true;
        }

    private:
        duckdb_database db_ = nullptr;
        duckdb_connection conn_ = nullptr;
    };

protected:
    DuckDBBaseT() = default;
    ~DuckDBBaseT() { shutdown(); }

    // 子类持锁后调用，避免重锁
    bool exec_unsafe(const std::string& sql) {
        duckdb_result result;
        if (duckdb_query(conn_, sql.c_str(), &result) != DuckDBSuccess) {
            duckdb_destroy_result(&result);
            return false;
        }
        duckdb_destroy_result(&result);
        return true;
    }

private:
    duckdb_database db_ = nullptr;
    duckdb_connection conn_ = nullptr;
    std::recursive_mutex mtx_;
    bool initialized_ = false;
};
