/**
 * @file SharedDatabaseAdapter.cpp
 */
#include "SharedDatabaseAdapter.h"

#include "Engine/Persistence/Database.h"

#include "Log/SyLogger.h"

#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace
{
    std::map<std::string, std::string> toMap(const UI::KvPair* values, size_t count)
    {
        std::map<std::string, std::string> result;
        if (values != nullptr)
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (values[i].key != nullptr && values[i].value != nullptr)
                {
                    result.emplace(values[i].key, values[i].value);
                }
            }
        }
        return result;
    }

    void copyToBuffer(char* buffer, size_t bufferSize, const std::string& value)
    {
        const size_t copyLen = (value.size() < bufferSize - 1) ? value.size() : (bufferSize - 1);
        std::memcpy(buffer, value.c_str(), copyLen);
        buffer[copyLen] = '\0';
    }
}  // namespace

SharedDatabaseAdapter::SharedDatabaseAdapter(Eg::Database& db)
    : m_db(db)
{
}

SharedDatabaseAdapter::~SharedDatabaseAdapter() = default;

bool SharedDatabaseAdapter::open(const char* dbPath)
{
    // 采纳语义：共享连接由 PersistenceService 打开，这里只报告状态
    if (!m_db.isOpen())
    {
        m_lastErrorCache = "Shared database connection is not open";
        SY_ERRORF("[SharedDatabaseAdapter] open(%s) rejected: shared connection not open", dbPath);
        return false;
    }
    m_lastErrorCache.clear();
    return true;
}

void SharedDatabaseAdapter::close()
{
    // no-op：连接生命周期归 PersistenceService 管
    SY_DEBUG("[SharedDatabaseAdapter] close() ignored on shared connection");
}

bool SharedDatabaseAdapter::isOpen() const
{
    return m_db.isOpen();
}

bool SharedDatabaseAdapter::execute(const char* sql)
{
    const bool ok = m_db.execute(sql != nullptr ? sql : "");
    m_lastErrorCache = m_db.lastError();
    return ok;
}

void SharedDatabaseAdapter::registerWhitelist(const char* tableName, const UI::KvPair* values, size_t count)
{
    if (tableName == nullptr)
    {
        return;
    }
    Eg::Database::registerTableName(tableName);
    std::vector<std::string> columns;
    columns.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (values != nullptr && values[i].key != nullptr)
        {
            columns.emplace_back(values[i].key);
        }
    }
    Eg::Database::registerColumnNames(tableName, columns);
}

bool SharedDatabaseAdapter::insert(const char* tableName, const UI::KvPair* values, size_t count)
{
    registerWhitelist(tableName, values, count);
    const bool ok = m_db.insert(tableName != nullptr ? tableName : "", toMap(values, count));
    m_lastErrorCache = m_db.lastError();
    return ok;
}

bool SharedDatabaseAdapter::insertOrReplace(const char* tableName, const UI::KvPair* values, size_t count)
{
    registerWhitelist(tableName, values, count);
    const bool ok = m_db.insertOrReplace(tableName != nullptr ? tableName : "", toMap(values, count));
    m_lastErrorCache = m_db.lastError();
    return ok;
}

bool SharedDatabaseAdapter::update(const char* tableName,
    const UI::KvPair* values,
    size_t valueCount,
    const char* whereClause,
    const UI::KvPair* whereParams,
    size_t whereParamCount)
{
    registerWhitelist(tableName, values, valueCount);
    const bool ok = m_db.update(tableName != nullptr ? tableName : "",
        toMap(values, valueCount),
        whereClause != nullptr ? whereClause : "",
        toMap(whereParams, whereParamCount));
    m_lastErrorCache = m_db.lastError();
    return ok;
}

bool SharedDatabaseAdapter::deleteRows(
    const char* tableName, const char* whereClause, const UI::KvPair* whereParams, size_t whereParamCount)
{
    if (tableName != nullptr)
    {
        Eg::Database::registerTableName(tableName);
    }
    const bool ok = m_db.deleteRows(tableName != nullptr ? tableName : "",
        whereClause != nullptr ? whereClause : "",
        toMap(whereParams, whereParamCount));
    m_lastErrorCache = m_db.lastError();
    return ok;
}

void SharedDatabaseAdapter::query(
    const char* sql, UI::QueryRowCallback callback, void* userData, const char* const* params, size_t paramCount)
{
    if (callback == nullptr)
    {
        return;
    }

    std::vector<std::string> bindParams;
    bindParams.reserve(paramCount);
    for (size_t i = 0; i < paramCount; ++i)
    {
        bindParams.emplace_back(params[i] != nullptr ? params[i] : "");
    }

    const auto rows = m_db.query(sql != nullptr ? sql : "", bindParams);
    m_lastErrorCache = m_db.lastError();

    for (const auto& row : rows)
    {
        // 列名/值在回调期间必须有效：先落地字符串，再拼 KvPair 视图
        std::vector<std::pair<std::string, std::string>> storage;
        storage.reserve(row.size());
        std::vector<UI::KvPair> kvPairs;
        kvPairs.reserve(row.size());
        for (const auto& kv : row)
        {
            storage.emplace_back(kv.first, kv.second);
            kvPairs.push_back({ storage.back().first.c_str(), storage.back().second.c_str() });
        }
        callback(kvPairs.data(), kvPairs.size(), userData);
    }
}

size_t SharedDatabaseAdapter::get(char* buffer,
    size_t bufferSize,
    const char* tableName,
    const char* column,
    const char* whereClause,
    const UI::KvPair* whereParams,
    size_t whereParamCount,
    const char* defaultValue)
{
    if (buffer == nullptr || bufferSize == 0)
    {
        return 0;
    }
    if (tableName != nullptr)
    {
        Eg::Database::registerTableName(tableName);
        if (column != nullptr)
        {
            Eg::Database::registerColumnNames(tableName, { column });
        }
    }

    const std::string value = m_db.get(tableName != nullptr ? tableName : "",
        column != nullptr ? column : "",
        whereClause != nullptr ? whereClause : "",
        toMap(whereParams, whereParamCount),
        defaultValue != nullptr ? defaultValue : "");
    m_lastErrorCache = m_db.lastError();

    copyToBuffer(buffer, bufferSize, value);
    const size_t len = std::strlen(buffer);
    return len;
}

bool SharedDatabaseAdapter::set(const char* tableName,
    const char* column,
    const char* value,
    const char* whereClause,
    const UI::KvPair* whereParams,
    size_t whereParamCount)
{
    const UI::KvPair values[1] = { { column, value } };
    return update(tableName, values, 1, whereClause, whereParams, whereParamCount);
}

bool SharedDatabaseAdapter::beginTransaction()
{
    const bool ok = m_db.beginTransaction();
    m_lastErrorCache = m_db.lastError();
    return ok;
}

bool SharedDatabaseAdapter::commit()
{
    const bool ok = m_db.commit();
    m_lastErrorCache = m_db.lastError();
    return ok;
}

bool SharedDatabaseAdapter::rollback()
{
    const bool ok = m_db.rollback();
    m_lastErrorCache = m_db.lastError();
    return ok;
}

int64_t SharedDatabaseAdapter::lastInsertRowId() const
{
    return m_db.lastInsertRowId();
}

int SharedDatabaseAdapter::changes() const
{
    return m_db.changes();
}

const char* SharedDatabaseAdapter::lastError() const
{
    return m_lastErrorCache.c_str();
}

void SharedDatabaseAdapter::clearError()
{
    m_lastErrorCache.clear();
    m_db.clearError();
}
