#pragma once

#include "Persistence/Models/SettingRecord.h"

#include <map>
#include <vector>
#include <string>

namespace Eg
{
    class Database;
}

/**
 * @brief 设置仓储 — 封装 settings 表的 CRUD 操作
 *
 * 按分组+键名唯一索引，支持单值读写和分组批量加载。
 * WorkbenchWindow 通过 PersistenceService 访问此仓储。
 */
class SettingsRepository
{
public:
    explicit SettingsRepository(Eg::Database& database);

    /// 读取单个设置值
    std::string loadValue(const std::string& groupName, const std::string& key, const std::string& defaultValue = "");

    /// 保存或更新单个设置值
    bool saveValue(const std::string& groupName,
        const std::string& key,
        const std::string& value,
        const std::string& dataType = "string");

    /// 删除指定设置
    bool removeValue(const std::string& groupName, const std::string& key);

    /// 加载指定分组的所有设置
    std::vector<SettingRecord> loadGroup(const std::string& groupName);

    const std::string& lastError() const;

private:
    SettingRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const SettingRecord& rec) const;

    Eg::Database& m_database;
    std::string m_lastError;
};