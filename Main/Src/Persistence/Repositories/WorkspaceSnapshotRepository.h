#pragma once

#include "Persistence/Models/WorkspaceSnapshotRecord.h"

#include <map>
#include <vector>
#include <string>

namespace Eg { class Database; }

/**
 * @brief 工作台布局快照仓储 — 封装 workspace_snapshots 表的 CRUD 操作
 *
 * 每个工作台类型（2D/3D）保存一份布局快照。
 * WorkbenchWindow 在切换工作台时调用 save/load。
 */
class WorkspaceSnapshotRepository
{
public:
    explicit WorkspaceSnapshotRepository(Eg::Database& database);

    /// 加载指定工作台的布局快照
    /// @param workbenchId 工作台标识（如 "2D", "3D"）
    /// @return 找到的快照；如果不存在则返回 id==0 的空记录
    WorkspaceSnapshotRecord load(const std::string& workbenchId);

    /// 保存或更新工作台布局快照
    bool save(const WorkspaceSnapshotRecord& record);

    /// 删除指定工作台的布局快照
    bool remove(const std::string& workbenchId);

    const std::string& lastError() const;

private:
    WorkspaceSnapshotRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const WorkspaceSnapshotRecord& rec) const;

    Eg::Database& m_database;
    std::string m_lastError;
};