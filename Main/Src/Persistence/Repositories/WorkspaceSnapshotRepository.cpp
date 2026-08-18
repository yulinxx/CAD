#include "WorkspaceSnapshotRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

WorkspaceSnapshotRepository::WorkspaceSnapshotRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
{
}

WorkspaceSnapshotRecord WorkspaceSnapshotRepository::load(const std::string& workbenchId)
{
    std::string sql = "SELECT * FROM workspace_snapshots WHERE workbench_id = ?";
    std::vector<std::string> params = { workbenchId };
    auto rows = m_database.query(sql, params);

    if (rows.empty())
    {
        return WorkspaceSnapshotRecord();
    }

    const auto& row = rows[0];
    return rowToRecord(row);
}

bool WorkspaceSnapshotRepository::save(const WorkspaceSnapshotRecord& record)
{
    auto values = recordToRow(record);

    if (!m_database.insertOrReplace("workspace_snapshots", values))
    {
        return fail("WorkspaceSnapshotRepository", "Failed to save workspace snapshot");
    }

    return true;
}

bool WorkspaceSnapshotRepository::remove(const std::string& workbenchId)
{
    std::map<std::string, std::string> whereParams;
    whereParams["workbench_id"] = workbenchId;

    if (!m_database.deleteRows("workspace_snapshots", "workbench_id = :workbench_id", whereParams))
    {
        return fail("WorkspaceSnapshotRepository", "Failed to remove workspace snapshot");
    }

    return true;
}

WorkspaceSnapshotRecord WorkspaceSnapshotRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    WorkspaceSnapshotRecord rec;
    auto it = row.find("id");
    if (it != row.end())
    {
        rec.id = std::stoi(it->second);
    }
    it = row.find("workbench_id");
    if (it != row.end())
    {
        rec.workbenchId = it->second;
    }
    it = row.find("geometry");
    if (it != row.end())
    {
        rec.geometry = it->second;
    }
    it = row.find("window_state");
    if (it != row.end())
    {
        rec.windowState = it->second;
    }
    it = row.find("updated_at");
    if (it != row.end())
    {
        rec.updatedAt = it->second;
    }
    return rec;
}

std::map<std::string, std::string> WorkspaceSnapshotRepository::recordToRow(const WorkspaceSnapshotRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["workbench_id"] = rec.workbenchId;
    row["geometry"] = rec.geometry;
    row["window_state"] = rec.windowState;
    row["updated_at"] = rec.updatedAt;
    return row;
}