#pragma once

#include "UI/Services/IRecentFileService.h"

class PersistenceService;

/**
 * @class RecentFileService
 * @brief IRecentFileService 的唯一实现 —— QSettings + 数据库双写
 *
 * 全仓最近文件读写只走这一条路。曾经并存的三套（WorkbenchWindow 自带一份、
 * UI2D ConfigManager 写 "Files/RecentFiles"、UI2D FileManager 纯内存）已删除。
 * 内部优先使用数据库持久化，QSettings 作为兜底。
 */
class RecentFileService : public IRecentFileService
{
public:
    explicit RecentFileService(PersistenceService* persistence = nullptr);

    /// 追加一个最近文件（自动去重、截断、双写）
    void addRecentFile(const QString& filePath) override;

    /// 加载最近文件列表（数据库优先，QSettings 兜底）
    QStringList loadRecentFiles() const override;

    /// 保存最近文件列表（QSettings 兜底写入）
    void saveRecentFiles(const QStringList& files) const override;

private:
    /// 最大最近文件数量
    static constexpr int kMaxRecentFiles = 10;

    PersistenceService* m_persistence{ nullptr };
};
