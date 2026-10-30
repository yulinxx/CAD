#pragma once

#include "UI/Services/IRecentFileService.h"

class PersistenceService;

/**
 * @class RecentFileService
 * @brief IRecentFileService 的唯一实现 —— 数据库 + QSettings 双写，按工作台（2D/3D）分桶
 *
 * 全仓最近文件读写只走这一条路。曾经并存的三套（WorkbenchWindow 自带一份、
 * UI2D ConfigManager 写 "Files/RecentFiles"、UI2D FileManager 纯内存）已删除。
 * 内部优先使用数据库持久化（recent_files.workbench 列区分工作台），
 * QSettings 按工作台分 key 兜底（RecentFiles/2D、RecentFiles/3D）。
 */
class RecentFileService : public IRecentFileService
{
public:
    explicit RecentFileService(PersistenceService* persistence = nullptr);

    /// 追加一个最近文件（自动去重、按工作台截断、双写）
    void addRecentFile(const QString& filePath, const QString& workbenchId) override;

    /// 加载指定工作台的最近文件列表（数据库优先，QSettings 兜底）
    QStringList loadRecentFiles(const QString& workbenchId) const override;

private:
    /// 最大每工作台最近文件数量
    static constexpr int kMaxRecentFiles = 10;

    /// 工作台 ID 归一化：统一大写，空值兜底 "2D"（与旧行为兼容）
    static QString normalizeWorkbench(const QString& workbenchId);

    /// 旧记录没有工作台归属，按扩展名归类：3D 格式归 "3D"，其余归 "2D"
    static QString classifyWorkbenchByExtension(const QString& filePath);

    /// QSettings 兜底写入（仅指定工作台的 key）
    void saveWorkbenchFiles(const QString& workbenchId, const QStringList& files) const;

    PersistenceService* m_persistence{ nullptr };
};
