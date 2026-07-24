#include "RecentFileRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

#include <sstream>

RecentFileRepository::RecentFileRepository(Eg::Database& database)
    : m_database(database)
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
    Eg::Database::Transaction txn(m_database);

    // 清空现有数据
    if (!m_database.execute("DELETE FROM recent_files"))
    {
        m_lastError = "Failed to clear recent_files: " + m_database.lastError();
        SY_ERRORF("[RecentFileRepository] %s", m_lastError.c_str());
        return false;
    }

    // 批量插入
    for (const auto& rec : records)
    {
        auto values = recordToRow(rec);
        if (!m_database.insert("recent_files", values))
        {
            m_lastError = "Failed to insert recent file: " + m_database.lastError();
            SY_ERRORF("[RecentFileRepository] %s", m_lastError.c_str());
            return false;
        }
    }

    return txn.commit();
}

bool RecentFileRepository::append(const RecentFileRecord& record)
{
    auto values = recordToRow(record);

    // 使用 INSERT OR REPLACE 处理重复路径（UNIQUE(file_path) 约束保证去重）
    if (!m_database.insertOrReplace("recent_files", values))
    {
        m_lastError = "Failed to append recent file: " + m_database.lastError();
        SY_ERRORF("[RecentFileRepository] %s", m_lastError.c_str());
        return false;
    }

    // 控制列表上限，删除超出上限的最旧记录
    constexpr int kMaxRecentFiles = 10;
    std::string deleteSql = "DELETE FROM recent_files WHERE id NOT IN "
        "(SELECT id FROM recent_files ORDER BY last_opened_at DESC LIMIT "
        + std::to_string(kMaxRecentFiles) + ")";
    if (!m_database.execute(deleteSql))
    {
        // 删除超限记录失败不影响主流程，记录警告即可
        SY_WARNF("[RecentFileRepository] Failed to trim excess records: %s",
            m_database.lastError().c_str());
    }

    SY_DEBUGF("[RecentFileRepository] Appended recent file: %s", record.filePath.c_str());
    return true;
}

bool RecentFileRepository::remove(const std::string& filePath)
{
    std::map<std::string, std::string> whereParams;
    whereParams["file_path"] = filePath;

    if (!m_database.deleteRows("recent_files", "file_path = :file_path", whereParams))
    {
        m_lastError = "Failed to remove recent file: " + m_database.lastError();
        SY_ERRORF("[RecentFileRepository] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

const std::string& RecentFileRepository::lastError() const
{
    return m_lastError;
}

RecentFileRecord RecentFileRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    RecentFileRecord rec;
    auto it = row.find("id");
    if (it != row.end()) rec.id = std::stoi(it->second);
    it = row.find("file_path");
    if (it != row.end()) rec.filePath = it->second;
    it = row.find("title");
    if (it != row.end()) rec.title = it->second;
    it = row.find("format");
    if (it != row.end()) rec.format = it->second;
    it = row.find("last_opened_at");
    if (it != row.end()) rec.lastOpenedTime = it->second;
    return rec;
}

std::map<std::string, std::string> RecentFileRepository::recordToRow(const RecentFileRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0) row["id"] = std::to_string(rec.id);
    row["file_path"] = rec.filePath;
    row["title"] = rec.title;
    row["format"] = rec.format;
    row["last_opened_at"] = rec.lastOpenedTime;
    return row;
}