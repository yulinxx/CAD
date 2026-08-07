#include "DocumentPersistenceHelper.h"

#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/DocumentRepository.h"
#include "Persistence/Models/DocumentRecord.h"

#include <QDateTime>
#include <QFileInfo>

void DocumentPersistenceHelper::recordImport(PersistenceService* persistence,
    const QString& filePath, int entityCount)
{
    if (!persistence || !persistence->documents())
        return;

    auto existing = persistence->documents()->loadByPath(filePath.toStdString());
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    DocumentRecord dr;
    dr.filePath = filePath.toStdString();
    dr.title = QFileInfo(filePath).fileName().toStdString();
    dr.format = QFileInfo(filePath).suffix().toUpper().toStdString();
    dr.entityCount = entityCount;
    dr.lastOpenedAt = now.toStdString();
    dr.lastSavedAt = existing.id > 0 ? existing.lastSavedAt : now.toStdString();
    dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();

    persistence->documents()->save(dr);
}

void DocumentPersistenceHelper::recordExport(PersistenceService* persistence,
    const std::string& filePath, int entityCount)
{
    if (!persistence || !persistence->documents())
        return;

    auto existing = persistence->documents()->loadByPath(filePath);
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    DocumentRecord dr;
    dr.filePath = filePath;
    dr.title = QFileInfo(QString::fromStdString(filePath)).fileName().toStdString();
    dr.format = QFileInfo(QString::fromStdString(filePath)).suffix().toUpper().toStdString();
    dr.entityCount = entityCount;
    dr.lastSavedAt = now.toStdString();
    dr.lastOpenedAt = existing.id > 0 ? existing.lastOpenedAt : now.toStdString();
    dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();

    persistence->documents()->save(dr);
}
