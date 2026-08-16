#include "DatabaseBootstrapper.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

#include <sstream>

// 当前 Schema 版本号（每次修改表结构时递增）
// v1: 初始表结构
// v2: 新增 documents 表
// v3: layers 表新增 fill 列（填充图层标志，色块填充）
// v4: layers 表新增 fill_color 列（填充色，色块填充）
static constexpr int kSchemaVersion = 4;

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

    int currentVersion = schemaVersion();

    if (currentVersion == 0)
    {
        // 首次创建：建表
        SY_INFO("[DatabaseBootstrapper] First run, creating schema v1");
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
        SY_INFOF("[DatabaseBootstrapper] Migrating schema from v%d to v%d", currentVersion, kSchemaVersion);
        if (!runMigrations(currentVersion, kSchemaVersion))
        {
            return false;
        }
    }

    SY_INFOF("[DatabaseBootstrapper] Schema is up to date (v%d)", kSchemaVersion);
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

    // 文档元数据表（v2 新增）
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

bool DatabaseBootstrapper::runMigrations(int currentVersion, int targetVersion)
{
    // 逐版本递增执行迁移，保证幂等性和可追溯性
    for (int v = currentVersion; v < targetVersion; ++v)
    {
        SY_INFOF("[DatabaseBootstrapper] Running migration v%d -> v%d", v, v + 1);

        if (v == 1)
        {
            // v1 -> v2: 新增 documents 表，用于存储文档元数据
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
                m_lastError = "Migration v1->v2 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_INFO("[DatabaseBootstrapper] Migration v1->v2: created documents table");
        }
        else if (v == 2)
        {
            // v2 -> v3: layers 表新增 fill 列（填充图层标志）
            // 注意：CREATE TABLE IF NOT EXISTS 不会为已存在的表补列，必须 ALTER TABLE
            std::string sqlFill = R"(
                ALTER TABLE layers ADD COLUMN fill INTEGER DEFAULT 0
            )";
            if (!m_database.execute(sqlFill))
            {
                m_lastError = "Migration v2->v3 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_INFO("[DatabaseBootstrapper] Migration v2->v3: added layers.fill column");
        }
        else if (v == 3)
        {
            // v3 -> v4: layers 表新增 fill_color 列（填充色）
            std::string sqlFillColor = R"(
                ALTER TABLE layers ADD COLUMN fill_color TEXT DEFAULT ''
            )";
            if (!m_database.execute(sqlFillColor))
            {
                m_lastError = "Migration v3->v4 failed: " + m_database.lastError();
                SY_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
                return false;
            }
            SY_INFO("[DatabaseBootstrapper] Migration v3->v4: added layers.fill_color column");
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

    SY_INFOF("[DatabaseBootstrapper] Schema migrated to v%d", targetVersion);
    return true;
}