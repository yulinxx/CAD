#include "SqliteRepositoryBase.h"

#include "Log/SyLogger.h"

#include <cstdlib>

SqliteRepositoryBase::SqliteRepositoryBase(Eg::Database& database)
    : m_database(database)
{
}

const std::string& SqliteRepositoryBase::lastError() const
{
    return m_lastError;
}

bool SqliteRepositoryBase::fail(const char* tag, const std::string& message)
{
    m_lastError = message + ": " + m_database.lastError();
    SY_ERRORF("[%s] %s", tag, m_lastError.c_str());
    return false;
}

bool SqliteRepositoryBase::setError(const char* tag, const std::string& message)
{
    m_lastError = message;
    SY_ERRORF("[%s] %s", tag, m_lastError.c_str());
    return false;
}

std::string SqliteRepositoryBase::getString(
    const std::map<std::string, std::string>& row, const std::string& column, const std::string& defaultValue)
{
    auto it = row.find(column);
    return (it != row.end()) ? it->second : defaultValue;
}

int SqliteRepositoryBase::getInt(
    const std::map<std::string, std::string>& row, const std::string& column, int defaultValue)
{
    auto it = row.find(column);
    if (it == row.end() || it->second.empty())
    {
        return defaultValue;
    }
    try
    {
        return std::stoi(it->second);
    }
    catch (...)
    {
        return defaultValue;
    }
}

int64_t SqliteRepositoryBase::getInt64(
    const std::map<std::string, std::string>& row, const std::string& column, int64_t defaultValue)
{
    auto it = row.find(column);
    if (it == row.end() || it->second.empty())
    {
        return defaultValue;
    }
    try
    {
        return std::stoll(it->second);
    }
    catch (...)
    {
        return defaultValue;
    }
}

bool SqliteRepositoryBase::getBool(
    const std::map<std::string, std::string>& row, const std::string& column, bool defaultValue)
{
    auto it = row.find(column);
    if (it == row.end())
    {
        return defaultValue;
    }
    return it->second == "1";
}

double SqliteRepositoryBase::getDouble(
    const std::map<std::string, std::string>& row, const std::string& column, double defaultValue)
{
    auto it = row.find(column);
    if (it == row.end() || it->second.empty())
    {
        return defaultValue;
    }
    try
    {
        return std::stod(it->second);
    }
    catch (...)
    {
        return defaultValue;
    }
}