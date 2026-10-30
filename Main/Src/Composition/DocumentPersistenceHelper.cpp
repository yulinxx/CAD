/**
 * @file DocumentPersistenceHelper.cpp
 * @brief 文档持久化辅助实现
 */
#include "DocumentPersistenceHelper.h"

#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/DocumentRepository.h"
#include "Persistence/Models/DocumentRecord.h"
#include "Log/SyLogger.h"

#include <QDateTime>
#include <QFileInfo>

namespace
{
    // 工作台 ID 归一化：统一大写，空值兜底 "2D"（与 RecentFileService 保持一致）
    QString normalizeWorkbench(const QString& workbenchId)
    {
        const QString trimmed = workbenchId.trimmed().toUpper();
        return trimmed.isEmpty() ? QStringLiteral("2D") : trimmed;
    }
}  // namespace

bool DocumentPersistenceHelper::recordImport(
    PersistenceService* persistence, const QString& filePath, int entityCount, const QString& workbenchId)
{
    if (!persistence || !persistence->documents())
    {
        return true;
    }

    auto existing = persistence->documents()->loadByPath(filePath.toStdString());
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    DocumentRecord dr;
    dr.filePath = filePath.toStdString();
    dr.title = QFileInfo(filePath).fileName().toStdString();
    dr.format = QFileInfo(filePath).suffix().toUpper().toStdString();
    dr.workbench = normalizeWorkbench(workbenchId).toStdString();
    dr.entityCount = entityCount;
    dr.lastOpenedAt = now.toStdString();
    dr.lastSavedAt = existing.id > 0 ? existing.lastSavedAt : now.toStdString();
    dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();

    if (!persistence->documents()->save(dr))
    {
        SY_WARNF("[DocumentPersistenceHelper] Failed to save import metadata for: %s", filePath.toUtf8().constData());
        return false;
    }
    return true;
}

bool DocumentPersistenceHelper::recordExport(
    PersistenceService* persistence, const std::string& filePath, int entityCount, const QString& workbenchId)
{
    if (!persistence || !persistence->documents())
    {
        return true;
    }

    auto existing = persistence->documents()->loadByPath(filePath);
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    DocumentRecord dr;
    dr.filePath = filePath;
    dr.title = QFileInfo(QString::fromStdString(filePath)).fileName().toStdString();
    dr.format = QFileInfo(QString::fromStdString(filePath)).suffix().toUpper().toStdString();
    dr.workbench = normalizeWorkbench(workbenchId).toStdString();
    dr.entityCount = entityCount;
    dr.lastSavedAt = now.toStdString();
    dr.lastOpenedAt = existing.id > 0 ? existing.lastOpenedAt : now.toStdString();
    dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();

    if (!persistence->documents()->save(dr))
    {
        SY_WARNF("[DocumentPersistenceHelper] Failed to save export metadata for: %s", filePath.c_str());
        return false;
    }
    return true;
}