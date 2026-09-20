#include "DialogStateRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

#include <sstream>

DialogStateRepository::DialogStateRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
{
}

bool DialogStateRepository::save(const DialogStateRecord& record)
{
    if (record.dialogKey.empty())
    {
        return setError("DialogStateRepository", "save: empty dialogKey");
    }

    auto values = recordToRow(record);
    if (!m_database.insertOrReplace("dialog_states", values))
    {
        return fail("DialogStateRepository", "Failed to save dialog state");
    }

    SY_DEBUGF("[DialogStateRepository] Saved dialog state: key=%s, doc=%s",
        record.dialogKey.c_str(), record.documentId.c_str());
    return true;
}

DialogStateRecord DialogStateRepository::load(const std::string& dialogKey,
    const std::string& documentId)
{
    std::string sql = "SELECT * FROM dialog_states WHERE dialog_key = ?";
    std::vector<std::string> params = { dialogKey };

    if (!documentId.empty())
    {
        sql += " AND document_id = ?";
        params.push_back(documentId);
    }
    else
    {
        // 加载全局状态时，document_id 应为空
        sql += " AND (document_id IS NULL OR document_id = '')";
    }

    sql += " LIMIT 1";

    auto rows = m_database.query(sql, params);
    if (rows.empty())
    {
        return DialogStateRecord();
    }

    return rowToRecord(rows[0]);
}

std::vector<DialogStateRecord> DialogStateRepository::loadByDocument(
    const std::string& documentId)
{
    std::vector<DialogStateRecord> result;

    std::string sql = "SELECT * FROM dialog_states WHERE document_id = ? ORDER BY updated_at DESC";
    std::vector<std::string> params = { documentId };
    auto rows = m_database.query(sql, params);

    for (const auto& row : rows)
    {
        result.push_back(rowToRecord(row));
    }

    return result;
}

std::vector<DialogStateRecord> DialogStateRepository::loadAll()
{
    std::vector<DialogStateRecord> result;

    std::string sql = "SELECT * FROM dialog_states ORDER BY updated_at DESC";
    auto rows = m_database.query(sql);

    for (const auto& row : rows)
    {
        result.push_back(rowToRecord(row));
    }

    return result;
}

bool DialogStateRepository::remove(const std::string& dialogKey,
    const std::string& documentId)
{
    std::string whereClause = "dialog_key = :dialog_key";
    std::map<std::string, std::string> whereParams;
    whereParams["dialog_key"] = dialogKey;

    if (!documentId.empty())
    {
        whereClause += " AND document_id = :document_id";
        whereParams["document_id"] = documentId;
    }
    else
    {
        whereClause += " AND (document_id IS NULL OR document_id = '')";
    }

    if (!m_database.deleteRows("dialog_states", whereClause, whereParams))
    {
        return fail("DialogStateRepository", "Failed to remove dialog state");
    }

    SY_DEBUGF("[DialogStateRepository] Removed dialog state: key=%s", dialogKey.c_str());
    return true;
}

int DialogStateRepository::removeByDocument(const std::string& documentId)
{
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;

    if (!m_database.deleteRows("dialog_states", "document_id = :document_id", whereParams))
    {
        SY_ERRORF("[DialogStateRepository] Failed to remove dialog states for document: %s",
            documentId.c_str());
        return 0;
    }

    int count = m_database.changes();
    SY_DEBUGF("[DialogStateRepository] Removed %d dialog states for document: %s",
        count, documentId.c_str());
    return count;
}

DialogStateRecord DialogStateRepository::rowToRecord(
    const std::map<std::string, std::string>& row) const
{
    DialogStateRecord rec;
    rec.id = getInt(row, "id");
    rec.dialogKey = getString(row, "dialog_key");
    rec.documentId = getString(row, "document_id");
    rec.stateJson = getString(row, "state_json");
    rec.createdAt = getString(row, "created_at");
    rec.updatedAt = getString(row, "updated_at");
    return rec;
}

std::map<std::string, std::string> DialogStateRepository::recordToRow(
    const DialogStateRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["dialog_key"] = rec.dialogKey;
    row["document_id"] = rec.documentId;
    row["state_json"] = rec.stateJson;
    row["created_at"] = rec.createdAt;
    row["updated_at"] = rec.updatedAt;
    return row;
}
