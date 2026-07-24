#pragma once

#include "Persistence/Models/RecentFileRecord.h"

#include <map>
#include <vector>
#include <string>

namespace Eg
{
    class Database;
}

/**
 * @brief 最近文件仓储 — 封装 recent_files 表的 CRUD 操作
 *
 * 不直接暴露 SQL，所有操作通过 Database 的通用接口完成。
 * WorkbenchWindow 通过 PersistenceService 访问此仓储。
 */
class RecentFileRepository
{
public:
    explicit RecentFileRepository(Eg::Database& database);

    /// 加载所有最近文件（按最后打开时间降序排列）
    std::vector<RecentFileRecord> loadAll();

    /// 保存全部最近文件（替换整个列表）
    bool saveAll(const std::vector<RecentFileRecord>& records);

    /// 追加一个最近文件记录（如果已存在则更新 last_opened_at）
    bool append(const RecentFileRecord& record);

    /// 移除指定路径的最近文件记录
    bool remove(const std::string& filePath);

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    RecentFileRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const RecentFileRecord& rec) const;

    Eg::Database& m_database;
    std::string m_lastError;
};