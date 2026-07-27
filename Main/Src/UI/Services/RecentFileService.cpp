#include "UI/RecentFileService.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/RecentFileRepository.h"
#include "Persistence/Models/RecentFileRecord.h"

RecentFileService::RecentFileService(PersistenceService* persistence)
    : m_persistence(persistence)
{
}

void RecentFileService::setPersistenceService(PersistenceService* persistence)
{
    m_persistence = persistence;
}

PersistenceService* RecentFileService::persistenceService() const
{
    return m_persistence;
}

void RecentFileService::addRecentFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return;

    // 数据库端写入
    if (m_persistence && m_persistence->isOpen() && m_persistence->recentFiles())
    {
        QFileInfo fileInfo(filePath);
        RecentFileRecord rec;
        rec.filePath = filePath.toStdString();
        rec.title = fileInfo.fileName().toStdString();
        rec.format = fileInfo.suffix().toUpper().toStdString();
        rec.lastOpenedTime = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        m_persistence->recentFiles()->append(rec);
    }

    // QSettings 兜底双写
    QStringList files = loadRecentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    saveRecentFiles(files);
}

QStringList RecentFileService::loadRecentFiles() const
{
    // 数据库优先
    if (m_persistence && m_persistence->isOpen() && m_persistence->recentFiles())
    {
        auto records = m_persistence->recentFiles()->loadAll();
        if (!records.empty())
        {
            QStringList result;
            result.reserve(static_cast<int>(records.size()));
            for (const auto& rec : records)
                result.append(QString::fromStdString(rec.filePath));
            return result;
        }
    }

    // QSettings 兜底
    QSettings settings;
    return settings.value(QStringLiteral("RecentFiles"), QStringList()).toStringList();
}

void RecentFileService::saveRecentFiles(const QStringList& files) const
{
    // 数据库端由 addRecentFile 逐条写入，此处不做批量覆盖
    // QSettings 兜底：保留旧版兼容性
    QSettings settings;
    settings.setValue(QStringLiteral("RecentFiles"), files);
}