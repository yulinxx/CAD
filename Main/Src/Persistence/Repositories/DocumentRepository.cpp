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

    // 使用 INSERT OR REPLACE 替代先 loadByPath 再 UPDATE/INSERT 的 N+1 查询模式
    // documents 表的 file_path 已有 UNIQUE 约束，因此 INSERT OR REPLACE 可正确覆盖已有记录
    auto values = recordToRow(record);
    if (!m_database.insertOrReplace("documents", values))
    {
        return fail("DocumentRepository", "Failed to save document metadata");
    }

    SY_DEBUGF("[DocumentRepository] Saved document: %s", record.filePath.c_str());
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
        rec.id = getInt(row, "id");
        rec.filePath = getString(row, "file_path");
        rec.title = getString(row, "title");
        rec.format = getString(row, "format");
        rec.entityCount = getInt(row, "entity_count");
        rec.fileSize = getString(row, "file_size");
        rec.lastOpenedAt = getString(row, "last_opened_at");
        rec.lastSavedAt = getString(row, "last_saved_at");
        rec.createdAt = getString(row, "created_at");
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