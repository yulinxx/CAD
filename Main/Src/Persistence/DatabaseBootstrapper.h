#pragma once

#include <string>

namespace Eg
{
    class Database;
}

/**
 * @brief 数据库初始化器 — 负责创建和升级数据库表结构
 *
 * 管理 Schema 版本，确保表结构与应用代码保持同步。
 * 在应用启动时由 AppInitializer 调用 ensureSchema()。
 */
class DatabaseBootstrapper
{
public:
    explicit DatabaseBootstrapper(Eg::Database& database);

    /// 确保数据库表结构存在且为最新版本（幂等操作）
    /// @return 成功返回 true，失败时可通过 lastError() 获取错误信息
    bool ensureSchema();

    /// 当前数据库 Schema 版本号
    int schemaVersion() const;

    /// 最新应用期望的 Schema 版本号
    static int latestSchemaVersion();

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    /// 创建 app_meta 元数据表（记录 schema 版本等）
    bool createMetaTable();

    /// 创建所有业务数据表
    bool createBusinessTables();

    /// 执行从当前版本到目标版本的迁移
    bool runMigrations(int currentVersion, int targetVersion);

    Eg::Database& m_database;
    std::string m_lastError;
};