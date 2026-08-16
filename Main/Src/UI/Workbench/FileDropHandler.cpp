#include "FileDropHandler.h"

#include "Import/ImportService.h"
#include "Import/ImportOptions.h"
#include "Import/ImportResult.h"
#include "Log/SyLogger.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "FileIO/ImageUtils.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QUrl>
#include <QWidget>

namespace
{
    // 位图/图片扩展名（与 FileOperationRegistry::doImportImage 一致），
    // 这些格式走 QImage 导入路径，不经过 ImportService 的矢量读取器。
    const QStringList& imageExtensions()
    {
        static const QStringList exts = { QStringLiteral("jpg"),    QStringLiteral("jpeg"),
                                          QStringLiteral("png"),    QStringLiteral("bmp"),
                                          QStringLiteral("tga"),    QStringLiteral("tiff"),
                                          QStringLiteral("tif"),    QStringLiteral("gif"),
                                          QStringLiteral("webp") };
        return exts;
    }
}  // namespace

FileDropHandler::FileDropHandler(QObject* parent)
    : QObject(parent)
{
}

void FileDropHandler::setImportService(ImportService* service)
{
    m_importService = service;
}

void FileDropHandler::setSceneManager(Eg::SceneManager* sceneManager)
{
    m_sceneManager = sceneManager;
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
    QStringList exts = imageExtensions();
    if (m_importService)
    {
        exts.append(m_importService->supportedExtensions());
    }
    return exts;
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

        // 位图/图片文件走 QImage 导入路径（与 FileOperationRegistry::doImportImage 一致）
        if (imageExtensions().contains(ext))
        {
            if (!m_sceneManager)
            {
                ++failedCount;
                SY_ERROR("[FileDropHandler] SceneManager not available for image import");
                emit sigFileImported(filePath, false);
                continue;
            }

            const bool ok = importImage(filePath);
            if (ok)
            {
                ++successCount;
                SY_INFOF("[FileDropHandler] Image imported: %s", filePath.toUtf8().constData());
            }
            else
            {
                ++failedCount;
                SY_WARNF("[FileDropHandler] Image import failed: %s", filePath.toUtf8().constData());
            }

            emit sigFileImported(filePath, ok);
            continue;
        }

        if (!m_importService)
        {
            ++failedCount;
            emit sigFileImported(filePath, false);
            continue;
        }

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

bool FileDropHandler::importImage(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    QImage image(filePath);
    if (image.isNull())
    {
        return false;
    }

    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

    const Fio::ImageInfo info = Fio::readImageInfo(filePath.toUtf8().constData());
    const float worldW = Fio::pixelsToUnit(rgba.width(), info.dpiX, Fio::UnitType::Millimeter);
    const float worldH = Fio::pixelsToUnit(rgba.height(), info.dpiY, Fio::UnitType::Millimeter);

    auto* imgEntity = new Eg::SyImage();
    imgEntity->nWidth = rgba.width();
    imgEntity->nHeight = rgba.height();
    imgEntity->ePixelFormat = Eg::SyPixelFormat::RGBA32;
    imgEntity->setPixelData(rgba.constBits(), static_cast<size_t>(rgba.sizeInBytes()));

    const double halfW = worldW / 2.0;
    const double halfH = worldH / 2.0;
    imgEntity->basePoint = Ut::Vec2d(0, 0);
    imgEntity->topLeft = Ut::Vec2d(-halfW, halfH);
    imgEntity->topRight = Ut::Vec2d(halfW, halfH);
    imgEntity->bottomLeft = Ut::Vec2d(-halfW, -halfH);
    imgEntity->bottomRight = Ut::Vec2d(halfW, -halfH);

    m_sceneManager->addEntity(imgEntity);
    return true;
}
