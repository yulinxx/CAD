#pragma once

#include <string>

namespace Eg
{
    class Database;
}

/**
 * @brief 数据库初始化器 — 负责创建当前版本的数据库表结构
 *
 * 在应用启动时由 AppInitializer 调用 ensureSchema()。不做迁移：
 * - schema_version: 数据库表结构版本；与当前版本不一致的库整库重建
 * - app_version: 写库的应用版本（仅供诊断）
 */
class DatabaseBootstrapper
{
public:
    explicit DatabaseBootstrapper(Eg::Database& database);

    /// 确保数据库表结构存在且为当前版本（幂等操作）
    /// @return 成功返回 true，失败时可通过 lastError() 获取错误信息
    bool ensureSchema();

    /// 当前数据库 Schema 版本号
    int schemaVersion() const;

    /// 最新应用期望的 Schema 版本号
    static int latestSchemaVersion();

    /// 获取数据库中记录的应用版本
    std::string databaseAppVersion() const;

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    /// 只保证 app_meta 表存在（CREATE TABLE IF NOT EXISTS，不写任何行）。
    /// 必须在任何 app_meta 查询之前调用，否则首次启动会先查询后建表。
    bool ensureMetaTable();

    /// 创建 app_meta 元数据表并写入 schema 版本号
    bool createMetaTable();

    /// 创建所有业务数据表
    bool createBusinessTables();

    /// 创建 documents 表（幂等）
    bool createDocumentsTable();

    /// 删除 app_meta 之外的全部表（表结构版本不一致时整库重建用）
    bool dropBusinessTables();

    /// 创建对话框状态表
    bool createDialogStateTable();

    /// 为常用查询字段创建索引，提升查询性能
    bool createIndices();

    Eg::Database& m_database;
    std::string m_lastError;
};