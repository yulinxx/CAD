#include "SqliteRepositoryBase.h"

#include "Log/SyLogger.h"

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