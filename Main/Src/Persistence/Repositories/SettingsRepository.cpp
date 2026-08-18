#include "SettingsRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

SettingsRepository::SettingsRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
{
}

std::string SettingsRepository::loadValue(
    const std::string& groupName, const std::string& key, const std::string& defaultValue)
{
    std::string whereClause = "group_name = :group_name AND key = :key";
    std::map<std::string, std::string> whereParams;
    whereParams["group_name"] = groupName;
    whereParams["key"] = key;

    std::string value = m_database.get("settings", "value", whereClause, whereParams, defaultValue);
    return value;
}

bool SettingsRepository::saveValue(
    const std::string& groupName, const std::string& key, const std::string& value, const std::string& dataType)
{
    std::map<std::string, std::string> values;
    values["group_name"] = groupName;
    values["key"] = key;
    values["value"] = value;
    values["data_type"] = dataType;

    if (!m_database.insertOrReplace("settings", values))
    {
        return fail("SettingsRepository", "Failed to save setting");
    }

    return true;
}

bool SettingsRepository::removeValue(const std::string& groupName, const std::string& key)
{
    std::map<std::string, std::string> whereParams;
    whereParams["group_name"] = groupName;
    whereParams["key"] = key;

    if (!m_database.deleteRows("settings", "group_name = :group_name AND key = :key", whereParams))
    {
        return fail("SettingsRepository", "Failed to remove setting");
    }

    return true;
}

std::vector<SettingRecord> SettingsRepository::loadGroup(const std::string& groupName)
{
    std::vector<SettingRecord> result;

    std::string sql = "SELECT * FROM settings WHERE group_name = ?";
    std::vector<std::string> params = { groupName };
    auto rows = m_database.query(sql, params);

    for (const auto& row : rows)
    {
        result.push_back(rowToRecord(row));
    }

    return result;
}

SettingRecord SettingsRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    SettingRecord rec;
    auto it = row.find("id");
    if (it != row.end())
    {
        rec.id = std::stoi(it->second);
    }
    it = row.find("group_name");
    if (it != row.end())
    {
        rec.groupName = it->second;
    }
    it = row.find("key");
    if (it != row.end())
    {
        rec.key = it->second;
    }
    it = row.find("value");
    if (it != row.end())
    {
        rec.value = it->second;
    }
    it = row.find("data_type");
    if (it != row.end())
    {
        rec.dataType = it->second;
    }
    it = row.find("updated_at");
    if (it != row.end())
    {
        rec.updatedAt = it->second;
    }
    return rec;
}

std::map<std::string, std::string> SettingsRepository::recordToRow(const SettingRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["group_name"] = rec.groupName;
    row["key"] = rec.key;
    row["value"] = rec.value;
    row["data_type"] = rec.dataType;
    row["updated_at"] = rec.updatedAt;
    return row;
}