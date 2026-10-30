/**
 * @file RecentFileRepository.cpp
 * @brief 最近文件数据仓库实现
 */
#include "RecentFileRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

#include <sstream>

RecentFileRepository::RecentFileRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
{
}

std::vector<RecentFileRecord> RecentFileRepository::loadAll()
{
    std::vector<RecentFileRecord> result;

    std::string sql = "SELECT * FROM recent_files ORDER BY last_opened_at DESC";
    auto rows = m_database.query(sql);
    for (const auto& row : rows)
    {
        result.push_back(rowToRecord(row));
    }

    return result;
}

bool RecentFileRepository::saveAll(const std::vector<RecentFileRecord>& records)
{
    SY_DEBUGF("[RecentFileRepository] Saving %zu recent files", records.size());
    Eg::Database::Transaction txn(m_database);

    // 清空现有数据
    if (!m_database.execute("DELETE FROM recent_files"))
    {
        return fail("RecentFileRepository", "Failed to clear recent_files");
    }

    // 批量插入
    for (const auto& rec : records)
    {
        auto values = recordToRow(rec);
        if (!m_database.insert("recent_files", values))
        {
            return fail("RecentFileRepository", "Failed to insert recent file");
        }
    }

    if (!txn.commit())
    {
        return fail("RecentFileRepository", "Failed to commit transaction");
    }
    SY_DEBUGF("[RecentFileRepository] Saved %zu recent files", records.size());
    return true;
}

bool RecentFileRepository::append(const RecentFileRecord& record)
{
    auto values = recordToRow(record);

    // 使用 INSERT OR REPLACE 处理重复路径（UNIQUE(file_path) 约束保证去重）
    if (!m_database.insertOrReplace("recent_files", values))
    {
        return fail("RecentFileRepository", "Failed to append recent file");
    }

    SY_DEBUGF("[RecentFileRepository] Appended recent file: %s", record.filePath.c_str());

    // 控制列表上限：按工作台分桶裁剪，每个工作台各自保留最近 kMaxRecentFiles 条。
    // 2D/3D 互不挤占 —— 3D 下保存/打开不能再把 2D 的记录顶掉。
    constexpr int kMaxRecentFiles = 10;
    std::map<std::string, std::string> trimParams;
    trimParams["wb_outer"] = record.workbench;
    trimParams["wb_inner"] = record.workbench;
    // deleteRows 按键名字典序将 value 按位置绑定到 whereClause 中的 ?（wb_outer 先于 wb_inner），
    // 两处 ? 绑定的是同一个工作台 ID，顺序无关。
    // 内层 SELECT 包一层是为了兼容旧 SQLite 对 IN (SELECT ... LIMIT) 的限制。
    if (!m_database.deleteRows("recent_files",
            "workbench = ? AND id NOT IN (SELECT id FROM (SELECT id FROM recent_files "
            "WHERE workbench = ? ORDER BY last_opened_at DESC LIMIT " +
                std::to_string(kMaxRecentFiles) + "))",
            trimParams))
    {
        // 删除超限记录失败不影响主流程，记录警告即可
        SY_WARNF("[RecentFileRepository] Failed to trim excess records: %s", m_database.lastError().c_str());
    }

    return true;
}

bool RecentFileRepository::remove(const std::string& filePath)
{
    std::map<std::string, std::string> whereParams;
    whereParams["file_path"] = filePath;

    if (!m_database.deleteRows("recent_files", "file_path = :file_path", whereParams))
    {
        return fail("RecentFileRepository", "Failed to remove recent file");
    }

    return true;
}

RecentFileRecord RecentFileRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    RecentFileRecord rec;
    rec.id = getInt(row, "id");
    rec.filePath = getString(row, "file_path");
    rec.title = getString(row, "title");
    rec.format = getString(row, "format");
    rec.workbench = getString(row, "workbench");
    rec.lastOpenedTime = getString(row, "last_opened_at");
    return rec;
}

std::map<std::string, std::string> RecentFileRepository::recordToRow(const RecentFileRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["file_path"] = rec.filePath;
    row["title"] = rec.title;
    row["format"] = rec.format;
    row["workbench"] = rec.workbench;
    row["last_opened_at"] = rec.lastOpenedTime;
    return row;
}