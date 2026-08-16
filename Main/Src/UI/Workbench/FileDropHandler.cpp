#include "FileDropHandler.h"

#include "Import/ImportService.h"
#include "Import/ImportOptions.h"
#include "Import/ImportResult.h"
#include "Log/SyLogger.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QWidget>

FileDropHandler::FileDropHandler(QObject* parent)
    : QObject(parent)
{
}

void FileDropHandler::setImportService(ImportService* service)
{
    m_importService = service;
}

// void FileDropHandler::installAppEventFilter()
// {
//     if (m_appFilterInstalled)
//     {
//         return;
//     }
//     if (QCoreApplication* app = QApplication::instance())
//     {
//         app->installEventFilter(this);
//         m_appFilterInstalled = true;
//         SY_INFO("[FileDropHandler] App-level event filter installed for drag-drop");
//     }
// }

// bool FileDropHandler::eventFilter(QObject* watched, QEvent* event)
// {
//     if (!m_importService)
//     {
//         return QObject::eventFilter(watched, event);
//     }

//     switch (event->type())
//     {
//     case QEvent::DragEnter:
//     {
//         handleDragEnter(static_cast<QDragEnterEvent*>(event));
//         return event->isAccepted();
//     }
//     case QEvent::DragMove:
//     {
//         handleDragMove(static_cast<QDragMoveEvent*>(event));
//         return event->isAccepted();
//     }
//     case QEvent::DragLeave:
//     {
//         handleDragLeave(static_cast<QDragLeaveEvent*>(event));
//         return true;
//     }
//     case QEvent::Drop:
//     {
//         handleDrop(static_cast<QDropEvent*>(event));
//         return true;
//     }
//     default:
//         break;
//     }

//     return QObject::eventFilter(watched, event);
// }

QStringList FileDropHandler::supportedExtensions() const
{
    if (!m_importService)
    {
        return {};
    }
    return m_importService->supportedExtensions();
}

bool FileDropHandler::handleDragEnter(QDragEnterEvent* event)
{
    if (!m_importService || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        event->ignore();
        return false;
    }

    const QStringList exts = supportedExtensions();
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
        {
            continue;
        }
        const QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
        if (exts.contains(ext))
        {
            event->acceptProposedAction();
            return true;
        }
    }

    event->ignore();
    return false;
}

bool FileDropHandler::handleDragMove(QDragMoveEvent* event)
{
    if (!m_importService || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        event->ignore();
        return false;
    }

    event->acceptProposedAction();
    return true;
}

void FileDropHandler::handleDragLeave(QDragLeaveEvent* event)
{
    event->accept();
}

bool FileDropHandler::handleDrop(QDropEvent* event)
{
    if (!m_importService || !event->mimeData() || !event->mimeData()->hasUrls())
    {
        event->ignore();
        return false;
    }

    const QStringList exts = supportedExtensions();
    const QList<QUrl> urls = event->mimeData()->urls();

    int successCount = 0;
    int failedCount = 0;

    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        const QString filePath = url.toLocalFile();
        const QString ext = QFileInfo(filePath).suffix().toLower();
        if (!exts.contains(ext))
        {
            continue;
        }

        SY_INFOF("[FileDropHandler] Importing dropped file: %s", filePath.toUtf8().constData());
        emit sigFileImported(filePath, false);

        ImportOptions opts;
        opts.importAsNewDocument = false;
        opts.autoFit = true;

        const ImportResult result = m_importService->importFile(filePath, opts);
        if (result.success)
        {
            ++successCount;
            SY_INFOF("[FileDropHandler] Imported: %s", filePath.toUtf8().constData());
        }
        else
        {
            ++failedCount;
            SY_WARNF("[FileDropHandler] Import failed: %s", result.message.toUtf8().constData());
        }

        emit sigFileImported(filePath, result.success);
    }

    event->acceptProposedAction();
    emit sigDropFinished(successCount, failedCount);
    return successCount > 0;
}
