#include "UI/Services/RecentFileService.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

#include "Log/SyLogger.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/RecentFileRepository.h"
#include "Persistence/Models/RecentFileRecord.h"

RecentFileService::RecentFileService(PersistenceService* persistence)
    : m_persistence(persistence)
{
}

void RecentFileService::addRecentFile(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        SY_DEBUGF("[RecentFileService] addRecentFile: empty path, ignored");
        return;
    }

    SY_DEBUGF("[RecentFileService] Adding recent file: %s", qPrintable(filePath));

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
        SY_DEBUG("[RecentFileService] File added to database");
    }
    else
    {
        SY_DEBUG("[RecentFileService] Database not available, using QSettings fallback");
    }

    // QSettings 兜底双写
    QStringList files = loadRecentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > kMaxRecentFiles)
    {
        files.removeLast();
    }
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
            SY_DEBUGF("[RecentFileService] Loaded %zu recent files from database", records.size());
            QStringList result;
            result.reserve(static_cast<int>(records.size()));
            for (const auto& rec : records)
            {
                result.append(QString::fromStdString(rec.filePath));
            }
            return result;
        }
    }

    // QSettings 兜底
    SY_DEBUG("[RecentFileService] Database empty, falling back to QSettings");
    QSettings settings;
    return settings.value(QStringLiteral("RecentFiles"), QStringList()).toStringList();
}

void RecentFileService::saveRecentFiles(const QStringList& files) const
{
    SY_DEBUGF("[RecentFileService] Saving %d recent files to QSettings", files.size());
    // 数据库端由 addRecentFile 逐条写入，此处不做批量覆盖
    // QSettings 兜底：保留旧版兼容性
    QSettings settings;
    settings.setValue(QStringLiteral("RecentFiles"), files);
}