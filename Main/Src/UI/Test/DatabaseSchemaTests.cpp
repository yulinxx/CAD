/**
 * @file DatabaseSchemaTests.cpp
 * @brief 数据库表结构引导测试（只建当前结构，不做迁移）
 *
 *  - 全新库：建出当前版本的全部表，版本号为当前版本
 *  - 重复启动幂等：数据保留
 *  - 表结构版本不一致：整库重建（数据清空、多余的表删除），版本号写成当前版本
 */

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "DatabaseBootstrapper.h"
#include "Engine/Persistence/Database.h"

namespace DatabaseSchema
{
    bool hasColumn(Eg::Database& db, const std::string& table, const std::string& column)
    {
        for (const auto& row : db.query("PRAGMA table_info(" + table + ")"))
        {
            if (row.at("name") == column)
            {
                return true;
            }
        }
        return false;
    }

    bool tableExists(Eg::Database& db, const std::string& table)
    {
        return !db.query("SELECT name FROM sqlite_master WHERE type = 'table' AND name = '" + table + "'").empty();
    }

    size_t rowCount(Eg::Database& db, const std::string& table)
    {
        return db.query("SELECT id FROM " + table).size();
    }

    bool setSchemaVersion(Eg::Database& db, int version)
    {
        const std::string value = std::to_string(version);
        return db.execute("DELETE FROM app_meta WHERE key = 'schema_version'") &&
               db.execute("INSERT INTO app_meta (key, value) VALUES ('schema_version', '" + value + "')");
    }

    bool insertRecentFile(Eg::Database& db)
    {
        return db.execute(
            "INSERT INTO recent_files (file_path, title, format, workbench) VALUES ('/tmp/a.apr', 'a', 'apr', '2D')");
    }
}  // namespace DatabaseSchema

TEST(DatabaseSchemaTest, FreshDatabaseGetsCurrentSchema)
{
    using namespace DatabaseSchema;
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));

    DatabaseBootstrapper bootstrapper(db);
    ASSERT_TRUE(bootstrapper.ensureSchema()) << bootstrapper.lastError();
    EXPECT_EQ(bootstrapper.schemaVersion(), DatabaseBootstrapper::latestSchemaVersion());
    for (const char* table : { "recent_files", "workspace_snapshots", "documents", "settings", "dialog_states" })
    {
        EXPECT_TRUE(tableExists(db, table)) << table;
    }
    EXPECT_TRUE(hasColumn(db, "recent_files", "workbench"));
    EXPECT_TRUE(hasColumn(db, "documents", "workbench"));
}

TEST(DatabaseSchemaTest, RepeatedStartupKeepsData)
{
    using namespace DatabaseSchema;
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(DatabaseBootstrapper(db).ensureSchema());
    ASSERT_TRUE(insertRecentFile(db));

    DatabaseBootstrapper again(db);
    ASSERT_TRUE(again.ensureSchema()) << again.lastError();
    EXPECT_EQ(rowCount(db, "recent_files"), 1u);
    EXPECT_EQ(again.schemaVersion(), DatabaseBootstrapper::latestSchemaVersion());
}

TEST(DatabaseSchemaTest, OtherSchemaVersionRebuildsAllTables)
{
    using namespace DatabaseSchema;
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(DatabaseBootstrapper(db).ensureSchema());
    ASSERT_TRUE(insertRecentFile(db));
    ASSERT_TRUE(db.execute("CREATE TABLE stale_table (id INTEGER PRIMARY KEY)"));
    ASSERT_TRUE(setSchemaVersion(db, DatabaseBootstrapper::latestSchemaVersion() - 1));

    DatabaseBootstrapper bootstrapper(db);
    ASSERT_TRUE(bootstrapper.ensureSchema()) << bootstrapper.lastError();
    EXPECT_EQ(bootstrapper.schemaVersion(), DatabaseBootstrapper::latestSchemaVersion());
    EXPECT_FALSE(tableExists(db, "stale_table"));
    EXPECT_TRUE(tableExists(db, "recent_files"));
    EXPECT_EQ(rowCount(db, "recent_files"), 0u);
    EXPECT_TRUE(hasColumn(db, "recent_files", "workbench"));
}
