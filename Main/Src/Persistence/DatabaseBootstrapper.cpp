#include "DatabaseBootstrapper.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"
#include "VersionInfo.h"

#include <sstream>
#include <map>
#include <vector>

// 当前 Schema 版本号（每次修改表结构时递增）
// v1: 初始表结构
// v2: 新增 documents 表
// v3: layers 表新增 fill 列（填充图层标志，色块填充）
// v4: layers 表新增 fill_color 列（填充色，色块填充）
static constexpr int kSchemaVersion = 4;

// 应用版本使用 MainApp::appVersion() 全局定义

DatabaseBootstrapper::DatabaseBootstrapper(Eg::Database& database)
    : m_database(database)
{
}

bool DatabaseBootstrapper::ensureSchema()
{
    if (!m_database.isOpen())
    {
        m_lastError = "Database is not open";
        SY_ERROR("[DatabaseBootstrapper] Database is not open");
        return false;
    }

    // 检查应用版本兼容性
    std::string dbAppVersion = databaseAppVersion();
    if (!dbAppVersion.empty() && dbAppVersion != MainApp::appVersion().c_str())
    {
        SY_WARNF("[DatabaseBootstrapper] App version mismatch: db=%s current=%s - clearing incompatible data",
            dbAppVersion.c_str(), MainApp::appVersion().c_str());
        // 版本不匹配时清除所有业务数据（保留 meta 表）
        m_database.execute("DELETE FROM recent_files");
        m_database.execute("DELETE FROM workspace_snapshots");
        m_database.execute("DELETE FROM layers");
        m_database.execute("DELETE FROM documents");
        // 重置 schema 版本，强制重建
        m_database.execute("DELETE FROM app_meta WHERE key = 'schema_version'");
    }

    // 记录当前应用版本
    {
        std::map<std::string, std::string> values;
        values["key"] = "app_version";
        values["value"] = MainApp::appVersion().c_str();
        m_database.insertOrReplace("app_meta", values);
    }

    int currentVersion = schemaVersion();

    if (currentVersion == 0)
    {
        // 首次创建：建表
        SY_DEBUG("[DatabaseBootstrapper] First run, creating schema v1");
        if (!createMetaTable())
        {
            return false;
        }
        if (!createBusinessTables())
        {
            return false;
        }
    }
    else if (currentVersion < kSchemaVersion)
    {
        // 需要迁移
        SY_DEBUGF("[DatabaseBootstrapper] Migrating schema from v%d to v%d", currentVersion, kSchemaVersion);
        if (!runMigrations(currentVersion, kSchemaVersion))
        {
            return false;
        }
    }

    SY_DEBUGF("[DatabaseBootstrapper] Schema is up to date (v%d)", kSchemaVersion);
    return true;
}

int DatabaseBootstrapper::schemaVersion() const
{
    if (!m_database.isOpen())
    {
        return 0;
    }

    std::string version = m_database.get("app_meta", "value", "key = 'schema_version'");
    if (version.empty())
    {
        return 0;
    }

    try
    {
        return std::stoi(version);
    }
    catch (...)
    {
        return 0;
    }
}

int DatabaseBootstrapper::latestSchemaVersion()
{
    return kSchemaVersion;
}

std::string DatabaseBootstrapper::databaseAppVersion() const
{
    if (!m_database.isOpen())
    {
        return {};
    }

    std::string version = m_database.get("app_meta", "value", "key = 'app_version'");
    return version;
}

const std::string& DatabaseBootstrapper::lastError() const
{
    return m_lastError;
}

bool DatabaseBootstrapper::createMetaTable()
{
    // 创建元数据表，存储 db_version 等信息
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS app_meta (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            key     TEXT    NOT NULL UNIQUE,
            value   TEXT    NOT NULL,
            updated_at TEXT DEFAULT (datetime('now'))
        )
    )";

    if (!m_database.execute(sql))
    {
        m_lastError = "Failed to create app_meta table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 写入当前 schema 版本号
    std::string versionStr = std::to_string(kSchemaVersion);
    if (!m_database.set("app_meta", "value", versionStr, "key = 'schema_version'"))
    {
        // 如果 set 失败（表刚创建，可能没有行），尝试 insert
        std::map<std::string, std::string> values;
        values["key"] = "schema_version";
        values["value"] = versionStr;
        m_database.execute("DELETE FROM app_meta WHERE key = 'schema_version'");
        if (!m_database.insert("app_meta", values))
        {
            m_lastError = "Failed to insert schema version: " + m_database.lastError();
            SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
            return false;
        }
    }

    return true;
}

bool DatabaseBootstrapper::createBusinessTables()
{
    // 最近文件表
    std::string sqlRecentFiles = R"(
        CREATE TABLE IF NOT EXISTS recent_files (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path       TEXT    NOT NULL,
            title           TEXT    NOT NULL,
            format          TEXT    DEFAULT '',
            last_opened_at  TEXT    DEFAULT (datetime('now')),
            UNIQUE(file_path)
        )
    )";

    if (!m_database.execute(sqlRecentFiles))
    {
        m_lastError = "Failed to create recent_files table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 工作台布局快照表
    std::string sqlWorkspaceSnapshots = R"(
        CREATE TABLE IF NOT EXISTS workspace_snapshots (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            workbench_id    TEXT    NOT NULL UNIQUE,
            geometry        TEXT    NOT NULL,
            window_state    TEXT    NOT NULL,
            updated_at      TEXT    DEFAULT (datetime('now'))
        )
    )";

    if (!m_database.execute(sqlWorkspaceSnapshots))
    {
        m_lastError = "Failed to create workspace_snapshots table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 图层表
    std::string sqlLayers = R"(
        CREATE TABLE IF NOT EXISTS layers (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            document_id     TEXT    NOT NULL,
            layer_id        INTEGER NOT NULL,
            name            TEXT    NOT NULL DEFAULT '',
            color           TEXT    DEFAULT '#000000',
            visible         INTEGER DEFAULT 1,
            locked          INTEGER DEFAULT 0,
            fill            INTEGER DEFAULT 0,
            fill_color      TEXT    DEFAULT '',
            layer_type      INTEGER DEFAULT 0,
            order_index     INTEGER DEFAULT 0,
            updated_at      TEXT    DEFAULT (datetime('now'))
        )
    )";

    if (!m_database.execute(sqlLayers))
    {
        m_lastError = "Failed to create layers table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 兼容已存在的旧格式 layers 表：CREATE TABLE IF NOT EXISTS 不会为其补列，
    // 确保 fill / fill_color 列存在，否则持久化时会出现 "no such column" 错误。
    if (!ensureLayerColumns())
    {
        return false;
    }

    // 文档元数据表（v2 新增）
    if (!createDocumentsTable())
    {
        return false;
    }

    // 设置表
    std::string sqlSettings = R"(
        CREATE TABLE IF NOT EXISTS settings (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            group_name      TEXT    NOT NULL,
            key             TEXT    NOT NULL,
            value           TEXT    NOT NULL DEFAULT '',
            data_type       TEXT    DEFAULT 'string',
            updated_at      TEXT    DEFAULT (datetime('now')),
            UNIQUE(group_name, key)
        )
    )";

    if (!m_database.execute(sqlSettings))
    {
        m_lastError = "Failed to create settings table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

bool DatabaseBootstrapper::ensureLayerColumns()
{
    // 表不存在时由调用方负责建表，这里无需补列
    if (m_database.query("PRAGMA table_info(layers)").empty())
    {
        return true;
    }

    if (!ensureLayerFillColumn() || !ensureLayerFillColorColumn() || !ensureLayerTypeColumn())
    {
        return false;
    }

    return true;
}

bool DatabaseBootstrapper::createDocumentsTable()
{
    std::string sqlDocuments = R"(
        CREATE TABLE IF NOT EXISTS documents (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path       TEXT    NOT NULL,
            title           TEXT    NOT NULL DEFAULT '',
            format          TEXT    DEFAULT '',
            entity_count    INTEGER DEFAULT 0,
            file_size       TEXT    DEFAULT '',
            last_opened_at  TEXT    DEFAULT (datetime('now')),
            last_saved_at   TEXT    DEFAULT (datetime('now')),
            created_at      TEXT    DEFAULT (datetime('now')),
            UNIQUE(file_path)
        )
    )";

    if (!m_database.execute(sqlDocuments))
    {
        m_lastError = "Failed to create documents table: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }
    return true;
}

bool DatabaseBootstrapper::ensureLayerColumn(const std::string& column, const std::string& definition)
{
    bool exists = false;
    for (const auto& row : m_database.query("PRAGMA table_info(layers)"))
    {
        if (row.at("name") == column)
        {
            exists = true;
            break;
        }
    }

    if (exists)
    {
        return true;
    }

    std::string sql = "ALTER TABLE layers ADD COLUMN " + column + " " + definition;
    if (!m_database.execute(sql))
    {
        m_lastError = "Failed to add layers." + column + " column: " + m_database.lastError();
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }
    SY_DEBUGF("[DatabaseBootstrapper] Added missing layers.%s column", column.c_str());
    return true;
}

bool DatabaseBootstrapper::ensureLayerFillColumn()
{
    return ensureLayerColumn("fill", "INTEGER DEFAULT 0");
}

bool DatabaseBootstrapper::ensureLayerFillColorColumn()
{
    return ensureLayerColumn("fill_color", "TEXT DEFAULT ''");
}

bool DatabaseBootstrapper::ensureLayerTypeColumn()
{
    return ensureLayerColumn("layer_type", "INTEGER DEFAULT 0");
}

bool DatabaseBootstrapper::runMigrations(int currentVersion, int targetVersion)
{
    // 逐版本递增执行迁移，保证幂等性和可追溯性
    for (int v = currentVersion; v < targetVersion; ++v)
    {
        SY_DEBUGF("[DatabaseBootstrapper] Running migration v%d -> v%d", v, v + 1);

        if (v == 1)
        {
            // v1 -> v2: 新增 documents 表，用于存储文档元数据
            if (!createDocumentsTable())
            {
                m_lastError = "Migration v1->v2 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_DEBUG("[DatabaseBootstrapper] Migration v1->v2: created documents table");
        }
        else if (v == 2)
        {
            // v2 -> v3: layers 表新增 fill 列（填充图层标志）
            if (!ensureLayerFillColumn())
            {
                m_lastError = "Migration v2->v3 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_DEBUG("[DatabaseBootstrapper] Migration v2->v3: added layers.fill column");
        }
        else if (v == 3)
        {
            // v3 -> v4: layers 表新增 fill_color 列（填充色）
            if (!ensureLayerFillColorColumn())
            {
                m_lastError = "Migration v3->v4 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_DEBUG("[DatabaseBootstrapper] Migration v3->v4: added layers.fill_color column");
        }
        // 后续版本迁移在此追加 else if 分支
    }

    // 更新 schema 版本号
    std::string versionStr = std::to_string(targetVersion);
    m_database.execute("DELETE FROM app_meta WHERE key = 'schema_version'");
    std::map<std::string, std::string> values;
    values["key"] = "schema_version";
    values["value"] = versionStr;
    if (!m_database.insert("app_meta", values))
    {
        m_lastError = "Failed to update schema version after migration";
        SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    SY_DEBUGF("[DatabaseBootstrapper] Schema migrated to v%d", targetVersion);
    return true;
}