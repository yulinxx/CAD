#pragma once

#include <QString>
#include <QStringList>

class PersistenceService;

/**
 * @class RecentFileService
 * @brief 最近文件管理服务 —— 封装 QSettings + 数据库双写的最近文件操作
 *
 * 将"最近文件"的读写逻辑从 WorkbenchWindow 和 ApplicationCompositionRoot 中剥离，
 * 提供统一的 add/load/save 接口。内部优先使用数据库持久化，QSettings 作为兜底。
 */
class RecentFileService
{
public:
    explicit RecentFileService(PersistenceService* persistence = nullptr);

    /// 设置持久化服务（运行时可更新）
    void setPersistenceService(PersistenceService* persistence);

    /// 获取持久化服务
    PersistenceService* persistenceService() const;

    /// 追加一个最近文件（自动去重、截断、双写）
    void addRecentFile(const QString& filePath);

    /// 加载最近文件列表（数据库优先，QSettings 兜底）
    QStringList loadRecentFiles() const;

    /// 保存最近文件列表（QSettings 兜底写入）
    void saveRecentFiles(const QStringList& files) const;

private:
    /// 最大最近文件数量
    static constexpr int kMaxRecentFiles = 10;

    PersistenceService* m_persistence{ nullptr };
};
