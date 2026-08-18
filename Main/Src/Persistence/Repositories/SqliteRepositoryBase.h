#pragma once

#include "Engine/Persistence/Database.h"

#include <string>

/**
 * @brief SQLite 仓储基类 — 统一数据库引用、错误状态与通用辅助方法
 *
 * 派生类仅需实现各自的记录模型转换(rowToRecord/recordToRow)与 CRUD 语义，
 * 数据库引用、错误信息维护以及统一的错误记录方式由基类提供，
 * 从而消除各 Repository 中重复的样板代码。
 */
class SqliteRepositoryBase
{
public:
    explicit SqliteRepositoryBase(Eg::Database& database);
    virtual ~SqliteRepositoryBase() = default;

    // 持有数据库引用，禁止拷贝
    SqliteRepositoryBase(const SqliteRepositoryBase&) = delete;
    SqliteRepositoryBase& operator=(const SqliteRepositoryBase&) = delete;

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

protected:
    /// 记录操作失败信息（拼接数据库底层错误并写错误日志），返回 false 供 bool 方法直接返回
    bool fail(const char* tag, const std::string& message);

    /// 记录错误信息并写错误日志（不拼接数据库底层错误），返回 false 供 bool 方法直接返回
    bool setError(const char* tag, const std::string& message);

    Eg::Database& m_database;
    std::string m_lastError;
};