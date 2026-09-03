#include "DocumentRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

DocumentRepository::DocumentRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
{
}

std::vector<DocumentRecord> DocumentRepository::loadAll()
{
    std::vector<DocumentRecord> result;

    try
    {
        std::string sql = "SELECT * FROM documents ORDER BY last_opened_at DESC";
        auto rows = m_database.query(sql);
        result.reserve(rows.size());
        for (const auto& row : rows)
        {
            try
            {
                result.push_back(rowToRecord(row));
            }
            catch (const std::exception& e)
            {
                SY_WARNF("[DocumentRepository] Skipping malformed row in loadAll: %s", e.what());
            }
        }
    }
    catch (const std::exception& e)
    {
        m_lastError = std::string("loadAll query failed: ") + e.what();
        SY_ERRORF("[DocumentRepository] %s", m_lastError.c_str());
    }

    return result;
}

DocumentRecord DocumentRepository::loadByPath(const std::string& filePath)
{
    if (filePath.empty())
    {
        m_lastError = "loadByPath: empty filePath";
        SY_WARNF("[DocumentRepository] %s", m_lastError.c_str());
        return DocumentRecord();
    }

    try
    {
        std::string sql = "SELECT * FROM documents WHERE file_path = ?";
        std::vector<std::string> params = { filePath };
        auto rows = m_database.query(sql, params);

        if (rows.empty())
        {
            return DocumentRecord();
        }

        return rowToRecord(rows[0]);
    }
    catch (const std::exception& e)
    {
        m_lastError = std::string("loadByPath failed: ") + e.what();
        SY_ERRORF("[DocumentRepository] %s", m_lastError.c_str());
        return DocumentRecord();
    }
}

bool DocumentRepository::save(const DocumentRecord& record)
{
    if (record.filePath.empty())
    {
        return setError("DocumentRepository", "save: empty filePath");
    }

    // 先检查是否已存在同路径的记录，若存在则更新而非插入
    DocumentRecord existing;
    try
    {
        existing = loadByPath(record.filePath);
    }
    catch (const std::exception& e)
    {
        m_lastError = std::string("save: loadByPath threw: ") + e.what();
        SY_ERRORF("[DocumentRepository] %s", m_lastError.c_str());
        return false;
    }

    if (existing.id > 0)
    {
        // 已有记录：通过 id 更新 fields
        std::map<std::string, std::string> values;
        values["title"] = record.title;
        values["format"] = record.format;
        values["entity_count"] = std::to_string(record.entityCount);
        values["file_size"] = record.fileSize;
        // 只在调用方明确提供了时间字段时才更新
        if (!record.lastOpenedAt.empty())
        {
            values["last_opened_at"] = record.lastOpenedAt;
        }
        if (!record.lastSavedAt.empty())
        {
            values["last_saved_at"] = record.lastSavedAt;
        }
        if (!record.createdAt.empty())
        {
            values["created_at"] = record.createdAt;
        }

        std::string whereClause = "id = :id";
        std::map<std::string, std::string> whereParams;
        whereParams["id"] = std::to_string(existing.id);

        if (!m_database.update("documents", values, whereClause, whereParams))
        {
            return fail("DocumentRepository", "Failed to update document metadata");
        }

        SY_DEBUGF("[DocumentRepository] Updated document: %s", record.filePath.c_str());
        return true;
    }

    // New record: insert
    auto values = recordToRow(record);
    if (!m_database.insertOrReplace("documents", values))
    {
        return fail("DocumentRepository", "Failed to save document metadata");
    }

    return true;
}

bool DocumentRepository::remove(const std::string& filePath)
{
    if (filePath.empty())
    {
        m_lastError = "remove: empty filePath";
        SY_WARNF("[DocumentRepository] %s", m_lastError.c_str());
        return false;
    }

    std::map<std::string, std::string> whereParams;
    whereParams["file_path"] = filePath;

    if (!m_database.deleteRows("documents", "file_path = :file_path", whereParams))
    {
        return fail("DocumentRepository", "Failed to remove document");
    }

    int deleted = m_database.changes();
    if (deleted == 0)
    {
        SY_WARNF("[DocumentRepository] remove: no document found for path: %s", filePath.c_str());
    }
    else
    {
        SY_DEBUGF("[DocumentRepository] Removed document: %s (%d row(s))", filePath.c_str(), deleted);
    }

    return true;
}

DocumentRecord DocumentRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    DocumentRecord rec;
    try
    {
        auto it = row.find("id");
        if (it != row.end() && !it->second.empty())
        {
            rec.id = std::stoi(it->second);
        }
        it = row.find("file_path");
        if (it != row.end())
        {
            rec.filePath = it->second;
        }
        it = row.find("title");
        if (it != row.end())
        {
            rec.title = it->second;
        }
        it = row.find("format");
        if (it != row.end())
        {
            rec.format = it->second;
        }
        it = row.find("entity_count");
        if (it != row.end() && !it->second.empty())
        {
            rec.entityCount = std::stoi(it->second);
        }
        it = row.find("file_size");
        if (it != row.end())
        {
            rec.fileSize = it->second;
        }
        it = row.find("last_opened_at");
        if (it != row.end())
        {
            rec.lastOpenedAt = it->second;
        }
        it = row.find("last_saved_at");
        if (it != row.end())
        {
            rec.lastSavedAt = it->second;
        }
        it = row.find("created_at");
        if (it != row.end())
        {
            rec.createdAt = it->second;
        }
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[DocumentRepository] rowToRecord failed: %s", e.what());
        throw;  // 向上传播让调用方决定是否忽略本条记录
    }
    return rec;
}

std::map<std::string, std::string> DocumentRepository::recordToRow(const DocumentRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["file_path"] = rec.filePath;
    row["title"] = rec.title;
    row["format"] = rec.format;
    row["entity_count"] = std::to_string(rec.entityCount);
    row["file_size"] = rec.fileSize;
    row["last_opened_at"] = rec.lastOpenedAt;
    row["last_saved_at"] = rec.lastSavedAt;
    row["created_at"] = rec.createdAt;
    return row;
}