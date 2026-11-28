/**
 * @file DatabaseBootstrapper.cpp
 * @brief 数据库引导程序实现
 *
 * 只建当前表结构，不做迁移：表结构版本不一致的库整库重建。
 */
#include "DatabaseBootstrapper.h"

#include "Engine/Persistence/Database.h"
#include "Log/AprLogger.h"
#include "VersionInfo.h"

#include <sstream>
#include <map>
#include <vector>

// 当前 Schema 版本号：改表结构时递增。库里记录的版本与它不一致时整库重建（不做迁移）
static constexpr int kSchemaVersion = 8;

// ---- 注册所有表名和列名白名单，用于 Database 层 SQL 注入防护 ----
static void registerSchemaWhiteList()
{
    Eg::Database::registerTableName("app_meta");
    Eg::Database::registerTableName("recent_files");
    Eg::Database::registerTableName("workspace_snapshots");
    Eg::Database::registerTableName("documents");
    Eg::Database::registerTableName("settings");
    Eg::Database::registerTableName("dialog_states");

    Eg::Database::registerColumnNames("app_meta", { "id", "key", "value", "updated_at" });
    Eg::Database::registerColumnNames("recent_files", { "id", "file_path", "title", "format", "workbench", "last_opened_at" });
    Eg::Database::registerColumnNames(
        "workspace_snapshots", { "id", "workbench_id", "geometry", "window_state", "updated_at" });
    Eg::Database::registerColumnNames("documents",
        { "id", "file_path", "title", "format", "workbench", "entity_count", "file_size", "last_opened_at", "last_saved_at", "created_at" });
    Eg::Database::registerColumnNames("settings", { "id", "group_name", "key", "value", "data_type", "updated_at" });
    Eg::Database::registerColumnNames(
        "dialog_states", { "id", "dialog_key", "document_id", "state_json", "created_at", "updated_at" });
}

DatabaseBootstrapper::DatabaseBootstrapper(Eg::Database& database)
    : m_database(database)
{
}

bool DatabaseBootstrapper::ensureSchema()
{
    if (!m_database.isOpen())
    {
        m_lastError = "Database is not open";
        APR_ERROR("[DatabaseBootstrapper] Database is not open");
        return false;
    }

    // 注册表名/列名白名单，用于 Database 层 SQL 注入防护
    registerSchemaWhiteList();

    // 先建元数据表，再查询版本：全新数据库上若先查后建，
    // 日志里会出现 "no such table: app_meta" 的假错误（查询本身被 catch 吞掉）。
    if (!ensureMetaTable())
    {
        return false;
    }

    // 不做迁移：别的表结构版本的库整库重建（最近文件、设置等应用状态随之清空）
    const int currentVersion = schemaVersion();
    if (currentVersion != 0 && currentVersion != kSchemaVersion)
    {
        APR_WARNF("[DatabaseBootstrapper] Schema v%d differs from v%d: rebuilding all tables",
            currentVersion,
            kSchemaVersion);
        if (!dropBusinessTables())
        {
            return false;
        }
    }

    if (!createBusinessTables() || !createMetaTable() || !createIndices())
    {
        return false;
    }

    // 记录写库的应用版本（仅供诊断）
    std::map<std::string, std::string> values;
    values["key"] = "app_version";
    values["value"] = MainApp::appVersion().c_str();
    m_database.insertOrReplace("app_meta", values);
    return true;
}

int DatabaseBootstrapper::schemaVersion() const
{
    if (!m_database.isOpen())
    {
        return 0;
    }

    try
    {
        std::string version = m_database.get("app_meta", "value", "key = 'schema_version'");
        if (version.empty())
        {
            return 0;
        }

        return std::stoi(version);
    }
    catch (const std::exception& e)
    {
        // 首次运行时表可能还不存在，按 v0 处理
        return 0;
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

    try
    {
        std::string version = m_database.get("app_meta", "value", "key = 'app_version'");
        return version;
    }
    catch (const std::exception&)
    {
        // 首次运行时表可能还不存在
        return {};
    }
    catch (...)
    {
        return {};
    }
}

const std::string& DatabaseBootstrapper::lastError() const
{
    return m_lastError;
}

bool DatabaseBootstrapper::ensureMetaTable()
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
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

bool DatabaseBootstrapper::createMetaTable()
{
    if (!ensureMetaTable())
    {
        return false;
    }

    // 写入当前 schema 版本号。必须用 INSERT OR REPLACE：Database::set() 底层是 UPDATE，
    // 影响 0 行时仍返回成功，表刚创建时不会插入任何行。
    std::map<std::string, std::string> values;
    values["key"] = "schema_version";
    values["value"] = std::to_string(kSchemaVersion);
    if (!m_database.insertOrReplace("app_meta", values))
    {
        m_lastError = "Failed to write schema version: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
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
            workbench       TEXT    DEFAULT '',
            last_opened_at  TEXT    DEFAULT (datetime('now')),
            UNIQUE(file_path)
        )
    )";

    if (!m_database.execute(sqlRecentFiles))
    {
        m_lastError = "Failed to create recent_files table: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
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
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 文档元数据表
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
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 对话框状态表
    if (!createDialogStateTable())
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
            workbench       TEXT    DEFAULT '',
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
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

bool DatabaseBootstrapper::dropBusinessTables()
{
    // 除 app_meta 外的全部表（含已不再使用的表）一律删除，随后按当前结构重建
    const auto tables = m_database.query(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT IN ('app_meta', 'sqlite_sequence')");
    for (const auto& row : tables)
    {
        const auto it = row.find("name");
        if (it == row.end() || it->second.empty())
        {
            continue;
        }
        if (!m_database.execute("DROP TABLE IF EXISTS \"" + it->second + "\""))
        {
            m_lastError = "Failed to drop table " + it->second + ": " + m_database.lastError();
            APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
            return false;
        }
    }
    return true;
}

bool DatabaseBootstrapper::createDialogStateTable()
{
    // 对话框状态表：支持任意对话框的界面数据持久化
    // state_json 字段存储 JSON 序列化的对话框状态，灵活支持不同对话框结构
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS dialog_states (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            dialog_key      TEXT    NOT NULL,
            document_id     TEXT    DEFAULT '',
            state_json      TEXT    NOT NULL,
            created_at      TEXT    DEFAULT (datetime('now')),
            updated_at      TEXT    DEFAULT (datetime('now')),
            UNIQUE(dialog_key, document_id)
        )
    )";

    if (!m_database.execute(sql))
    {
        m_lastError = "Failed to create dialog_states table: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

bool DatabaseBootstrapper::createIndices()
{
    // 为 recent_files 表的 last_opened_at 字段创建索引，加速按时间排序查询
    if (!m_database.execute(
            "CREATE INDEX IF NOT EXISTS idx_recent_files_opened_at ON recent_files(last_opened_at DESC)"))
    {
        m_lastError = "Failed to create index idx_recent_files_opened_at: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 为 dialog_states 表的 document_id 字段创建索引，加速按文档查询
    if (!m_database.execute("CREATE INDEX IF NOT EXISTS idx_dialog_states_doc_id ON dialog_states(document_id)"))
    {
        m_lastError = "Failed to create index idx_dialog_states_doc_id: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    // 为 dialog_states 表的 dialog_key 字段创建索引，加速按对话框标识查询
    if (!m_database.execute("CREATE INDEX IF NOT EXISTS idx_dialog_states_key ON dialog_states(dialog_key)"))
    {
        m_lastError = "Failed to create index idx_dialog_states_key: " + m_database.lastError();
        APR_ERRORF("[DatabaseBootstrapper] %s", m_lastError.c_str());
        return false;
    }

    return true;
}