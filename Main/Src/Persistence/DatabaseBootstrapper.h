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
 *
 * 版本控制：
 * - schema_version: 数据库表结构版本
 * - app_version: 应用版本（用于判断是否需要清除旧数据）
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

    /// 获取数据库中记录的应用版本
    std::string databaseAppVersion() const;

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    /// 创建 app_meta 元数据表（记录 schema 版本等）
    bool createMetaTable();

    /// 创建所有业务数据表
    bool createBusinessTables();

    /// 确保 layers 表包含 fill / fill_color 列（幂等，兼容旧格式已存在的表）
    bool ensureLayerColumns();

    /// 创建 documents 表（幂等；初始建表与 v1->v2 迁移共用同一 DDL）
    bool createDocumentsTable();

    /// 幂等新增 layers 列：PRAGMA 检查缺失后 ALTER TABLE ADD COLUMN
    bool ensureLayerColumn(const std::string& column, const std::string& definition);

    /// 确保 layers.fill 列存在（初始建表与 v2->v3 迁移共用）
    bool ensureLayerFillColumn();

    /// 确保 layers.fill_color 列存在（初始建表与 v3->v4 迁移共用）
    bool ensureLayerFillColorColumn();

    /// 确保 layers.layer_type 列存在（补齐历史库里缺失的图层类型列）
    bool ensureLayerTypeColumn();

    /// 执行从当前版本到目标版本的迁移
    bool runMigrations(int currentVersion, int targetVersion);

    Eg::Database& m_database;
    std::string m_lastError;
};