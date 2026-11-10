/**
 * @file DatabaseSchemaMigrationTests.cpp
 * @brief 数据库 schema 迁移回归测试
 *
 * 覆盖 recent_files / documents 的 workbench 列补列场景：
 *  - 旧库（无 workbench 列）启动时自动补列，历史数据保留且默认值为空
 *  - 版本号已是 7 但缺列（历史缺陷留下的状态）同样能在启动时修复
 *  - 重复执行迁移幂等：列只存在一份，数据不丢
 */

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "DatabaseBootstrapper.h"
#include "Engine/Persistence/Database.h"

namespace
{
    /// 统计表中指定列在 PRAGMA 结果里出现的次数（幂等性断言用）
    int columnCount(Eg::Database& db, const std::string& table, const std::string& column)
    {
        int count = 0;
        for (const auto& row : db.query("PRAGMA table_info(" + table + ")"))
        {
            if (row.at("name") == column)
            {
                ++count;
            }
        }
        return count;
    }

    bool hasColumn(Eg::Database& db, const std::string& table, const std::string& column)
    {
        return columnCount(db, table, column) > 0;
    }

    /// 先用引导器建出完整 schema，作为「真实库」的基础
    bool createFullSchema(Eg::Database& db)
    {
        DatabaseBootstrapper bootstrapper(db);
        return bootstrapper.ensureSchema();
    }

    /// 把 recent_files 换回旧版结构（无 workbench 列）
    bool replaceWithLegacyRecentFiles(Eg::Database& db)
    {
        if (!db.execute("DROP TABLE IF EXISTS recent_files"))
        {
            return false;
        }
        return db.execute(R"(
            CREATE TABLE recent_files (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                file_path       TEXT    NOT NULL,
                title           TEXT    NOT NULL,
                format          TEXT    DEFAULT '',
                last_opened_at  TEXT    DEFAULT (datetime('now')),
                UNIQUE(file_path)
            )
        )");
    }

    /// 把 documents 换回旧版结构（无 workbench 列）
    bool replaceWithLegacyDocuments(Eg::Database& db)
    {
        if (!db.execute("DROP TABLE IF EXISTS documents"))
        {
            return false;
        }
        return db.execute(R"(
            CREATE TABLE documents (
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
        )");
    }

    /// 改写 app_meta 里的 schema 版本号
    bool setSchemaVersion(Eg::Database& db, int version)
    {
        const std::string value = std::to_string(version);
        return db.execute("DELETE FROM app_meta WHERE key = 'schema_version'") &&
               db.execute("INSERT INTO app_meta (key, value) VALUES ('schema_version', '" + value + "')");
    }
}  // namespace

/// 旧库首次启动：缺 workbench 列时自动补列，历史行保留且默认值为空
TEST(DatabaseSchemaMigrationTest, LegacyRecentFilesGetsWorkbenchColumnOnStartup)
{
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(createFullSchema(db));
    ASSERT_TRUE(replaceWithLegacyRecentFiles(db));

    // 清掉版本号，模拟「首次创建」分支遇到已存在的旧表（真实缺陷的触发路径）
    ASSERT_TRUE(db.execute("DELETE FROM app_meta WHERE key = 'schema_version'"));

    // 迁移前先插入一条历史数据，验证补列后旧行不丢、默认值为空串
    ASSERT_TRUE(db.execute("INSERT INTO recent_files (file_path, title, format) VALUES "
                           "('/tmp/legacy.apr', 'legacy.apr', 'SY')"));
    ASSERT_FALSE(hasColumn(db, "recent_files", "workbench"));

    DatabaseBootstrapper bootstrapper(db);
    ASSERT_TRUE(bootstrapper.ensureSchema()) << bootstrapper.lastError();

    EXPECT_TRUE(hasColumn(db, "recent_files", "workbench"));
    EXPECT_EQ(DatabaseBootstrapper::latestSchemaVersion(), bootstrapper.schemaVersion());

    auto rows = db.query("SELECT * FROM recent_files WHERE file_path = '/tmp/legacy.apr'");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().at("workbench"), "");

    // 补列后写入带 workbench 的记录必须成功（原缺陷就是这一步报 no column named workbench）
    std::map<std::string, std::string> values;
    values["file_path"] = "/tmp/new.apr";
    values["title"] = "new.apr";
    values["format"] = "SY";
    values["workbench"] = "2D";
    values["last_opened_at"] = "2026-11-04T10:00:00";
    EXPECT_TRUE(db.insert("recent_files", values)) << db.lastError();

    auto written = db.query("SELECT workbench FROM recent_files WHERE file_path = '/tmp/new.apr'");
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written.front().at("workbench"), "2D");
}

/// 版本号已被写成 7 但表缺列（历史缺陷留下的现场状态）：启动时仍能修复
TEST(DatabaseSchemaMigrationTest, CurrentVersionWithoutColumnIsRepaired)
{
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(createFullSchema(db));
    ASSERT_TRUE(replaceWithLegacyRecentFiles(db));
    ASSERT_TRUE(replaceWithLegacyDocuments(db));
    ASSERT_TRUE(setSchemaVersion(db, DatabaseBootstrapper::latestSchemaVersion()));

    ASSERT_FALSE(hasColumn(db, "recent_files", "workbench"));
    ASSERT_FALSE(hasColumn(db, "documents", "workbench"));

    DatabaseBootstrapper bootstrapper(db);
    ASSERT_TRUE(bootstrapper.ensureSchema()) << bootstrapper.lastError();

    EXPECT_TRUE(hasColumn(db, "recent_files", "workbench"));
    EXPECT_TRUE(hasColumn(db, "documents", "workbench"));
    EXPECT_EQ(DatabaseBootstrapper::latestSchemaVersion(), bootstrapper.schemaVersion());
}

/// 从 v5 迁移到最新版本：两个 workbench 列都要补齐
TEST(DatabaseSchemaMigrationTest, MigrationFromV5AddsBothWorkbenchColumns)
{
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(createFullSchema(db));
    ASSERT_TRUE(replaceWithLegacyRecentFiles(db));
    ASSERT_TRUE(replaceWithLegacyDocuments(db));
    ASSERT_TRUE(setSchemaVersion(db, 5));

    DatabaseBootstrapper bootstrapper(db);
    ASSERT_TRUE(bootstrapper.ensureSchema()) << bootstrapper.lastError();

    EXPECT_TRUE(hasColumn(db, "recent_files", "workbench"));
    EXPECT_TRUE(hasColumn(db, "documents", "workbench"));
    EXPECT_EQ(DatabaseBootstrapper::latestSchemaVersion(), bootstrapper.schemaVersion());
}

/// 重复执行迁移必须幂等：列不会重复 ALTER，数据不丢失
TEST(DatabaseSchemaMigrationTest, MigrationIsIdempotent)
{
    Eg::Database db;
    ASSERT_TRUE(db.open(":memory:"));
    ASSERT_TRUE(createFullSchema(db));
    ASSERT_TRUE(replaceWithLegacyRecentFiles(db));
    ASSERT_TRUE(db.execute("INSERT INTO recent_files (file_path, title, format) VALUES "
                           "('/tmp/keep.apr', 'keep.apr', 'SY')"));

    DatabaseBootstrapper first(db);
    ASSERT_TRUE(first.ensureSchema()) << first.lastError();
    ASSERT_EQ(columnCount(db, "recent_files", "workbench"), 1);
    ASSERT_EQ(db.query("SELECT * FROM recent_files").size(), 1u);

    DatabaseBootstrapper second(db);
    ASSERT_TRUE(second.ensureSchema()) << second.lastError();

    EXPECT_EQ(columnCount(db, "recent_files", "workbench"), 1);
    EXPECT_EQ(db.query("SELECT * FROM recent_files").size(), 1u);
    EXPECT_EQ(DatabaseBootstrapper::latestSchemaVersion(), second.schemaVersion());
}
