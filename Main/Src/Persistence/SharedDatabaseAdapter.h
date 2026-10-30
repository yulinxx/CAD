/**
 * @file SharedDatabaseAdapter.h
 * @brief 将 UI::IDatabase 接口适配到业务库的 Eg::Database 连接上
 *
 * 背景：cad_database.sqlite 之前被同一进程的两条连接并发写入 —— 业务数据
 * （RecentFile/Document/Layer 等）走 PersistenceService 的 Eg::Database，
 * 设置走 UICommon 自建的 DatabaseWrapper。WAL 下双写连接仍会互相撞
 * SQLITE_BUSY(5)/BUSY_SNAPSHOT(517)，导入回写期间设置/最近文件写入重试
 * 全部失败的 "database is locked" 即源于此。
 *
 * 此适配器让设置侧复用业务库的唯一连接，从根源上消除双写竞争。
 *
 * 所有权语义：只借用 Eg::Database&（由 PersistenceService 持有）。
 * open() 不开文件（连接已由业务侧打开），close() 是 no-op，
 * 析构不触碰共享连接。
 */
#pragma once

#include "UI/Persistence/IDatabase.h"

#include <string>

namespace Eg
{
    class Database;
}

class SharedDatabaseAdapter final : public UI::IDatabase
{
public:
    explicit SharedDatabaseAdapter(Eg::Database& db);
    ~SharedDatabaseAdapter() override;

    // IDatabase 接口实现（语义见类注释）
    bool open(const char* dbPath) override;
    void close() override;
    bool isOpen() const override;

    bool execute(const char* sql) override;

    bool insert(const char* tableName, const UI::KvPair* values, size_t count) override;
    bool insertOrReplace(const char* tableName, const UI::KvPair* values, size_t count) override;
    bool update(const char* tableName,
        const UI::KvPair* values,
        size_t valueCount,
        const char* whereClause,
        const UI::KvPair* whereParams,
        size_t whereParamCount) override;
    bool deleteRows(
        const char* tableName, const char* whereClause, const UI::KvPair* whereParams, size_t whereParamCount) override;

    void query(const char* sql,
        UI::QueryRowCallback callback,
        void* userData,
        const char* const* params = nullptr,
        size_t paramCount = 0) override;

    size_t get(char* buffer,
        size_t bufferSize,
        const char* tableName,
        const char* column,
        const char* whereClause,
        const UI::KvPair* whereParams,
        size_t whereParamCount,
        const char* defaultValue = "") override;

    bool set(const char* tableName,
        const char* column,
        const char* value,
        const char* whereClause,
        const UI::KvPair* whereParams,
        size_t whereParamCount) override;

    bool beginTransaction() override;
    bool commit() override;
    bool rollback() override;

    int64_t lastInsertRowId() const override;
    int changes() const override;

    const char* lastError() const override;
    void clearError() override;

private:
    /// Eg::Database 有表名/列名白名单（防注入），设置侧的表（settings_common、
    /// 各 domain 表）是运行期动态名，业务侧的 bootstrapper 不会注册它们。
    /// 这里在 CRUD 前把本次用到的表名/列名注册进白名单（幂等）。
    void registerWhitelist(const char* tableName, const UI::KvPair* values, size_t count);

    Eg::Database& m_db;
    // Eg::Database::lastError() 按值返回 std::string；接口契约是 const char*，
    // 缓存到成员后暴露，指针有效期至下一次操作
    mutable std::string m_lastErrorCache;
};
