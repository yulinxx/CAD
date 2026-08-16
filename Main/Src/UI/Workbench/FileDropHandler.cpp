#include "FileDropHandler.h"

#include "Import/ImportService.h"
#include "Import/ImportOptions.h"
#include "Import/ImportResult.h"
#include "Log/SyLogger.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "FileIO/ImageUtils.h"

#include <QApplication>
#include <QCursor>
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

#include <vector>

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

    // 将图片文件解码为 RGBA8888 QImage。
    // 优先 QImage（覆盖 png/bmp/jpg/gif/ico/svg 等）；失败时回退到
    // Fio::loadImageToRgba（libwebp 解 webp、libtiff 解 tiff、stb_image 解 tga 等），
    // 确保拖拽支持全部声明格式。
    bool loadRgbaImage(const QString& filePath, QImage& outRgba)
    {
        QImage image(filePath);
        if (!image.isNull())
        {
            outRgba = image.convertToFormat(QImage::Format_RGBA8888);
            return true;
        }

        std::vector<unsigned char> bytes;
        int w = 0, h = 0;
        if (!Fio::loadImageToRgba(filePath.toUtf8().constData(), bytes, w, h) || w <= 0 || h <= 0)
        {
            return false;
        }
        outRgba = QImage(bytes.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
        return !outRgba.isNull();
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

void FileDropHandler::setScreenToWorldConverter(std::function<std::optional<QPointF>(const QPoint&)> converter)
{
    m_screenToWorld = std::move(converter);
}

void FileDropHandler::installAppEventFilter()
{
    if (m_appFilterInstalled)
    {
        return;
    }
    if (QCoreApplication* app = QApplication::instance())
    {
        app->installEventFilter(this);
        m_appFilterInstalled = true;
        SY_INFO("[FileDropHandler] App-level event filter installed for drag-drop");
    }
}

bool FileDropHandler::eventFilter(QObject* watched, QEvent* event)
{
    // 应用级过滤器兜底：在 QOpenGLWidget 原生子窗口（macOS/Windows）上，
    // 拖放事件不冒泡到 WorkbenchWindow，这里统一拦截处理。
    if (!m_importService)
    {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type())
    {
    case QEvent::DragEnter:
    {
        if (handleDragEnter(static_cast<QDragEnterEvent*>(event)))
        {
            return true;  // 已接受，不再下发给目标控件
        }
        return false;
    }
    case QEvent::DragMove:
    {
        if (handleDragMove(static_cast<QDragMoveEvent*>(event)))
        {
            return true;
        }
        return false;
    }
    case QEvent::DragLeave:
    {
        handleDragLeave(static_cast<QDragLeaveEvent*>(event));
        return false;  // DragLeave 不消费，交回目标控件
    }
    case QEvent::Drop:
    {
        if (handleDrop(static_cast<QDropEvent*>(event)))
        {
            return true;  // 已导入，消费事件避免二次处理
        }
        return false;
    }
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
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

    // 图片导入锚点：鼠标松开处（全局屏幕坐标 → 世界坐标）。转换失败时回退到原点。
    QPointF anchorWorld(0, 0);
    if (m_screenToWorld)
    {
        if (auto w = m_screenToWorld(QCursor::pos()))
        {
            anchorWorld = *w;
        }
    }

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

            const bool ok = importImage(filePath, anchorWorld);
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

bool FileDropHandler::importImage(const QString& filePath, const QPointF& anchorWorld)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    QImage rgba;
    if (!loadRgbaImage(filePath, rgba))
    {
        return false;
    }

    const Fio::ImageInfo info = Fio::readImageInfo(filePath.toUtf8().constData());
    const float worldW = Fio::pixelsToUnit(rgba.width(), info.dpiX, Fio::UnitType::Millimeter);
    const float worldH = Fio::pixelsToUnit(rgba.height(), info.dpiY, Fio::UnitType::Millimeter);

    auto* imgEntity = new Eg::SyImage();
    imgEntity->nWidth = rgba.width();
    imgEntity->nHeight = rgba.height();
    imgEntity->ePixelFormat = Eg::SyPixelFormat::RGBA32;
    imgEntity->setPixelData(rgba.constBits(), static_cast<size_t>(rgba.sizeInBytes()));

    // 图片以中心点为世界锚点，锚点即鼠标松开处的世界坐标
    const double halfW = worldW / 2.0;
    const double halfH = worldH / 2.0;
    const double cx = anchorWorld.x();
    const double cy = anchorWorld.y();
    imgEntity->basePoint = Ut::Vec2d(cx, cy);
    imgEntity->topLeft = Ut::Vec2d(cx - halfW, cy + halfH);
    imgEntity->topRight = Ut::Vec2d(cx + halfW, cy + halfH);
    imgEntity->bottomLeft = Ut::Vec2d(cx - halfW, cy - halfH);
    imgEntity->bottomRight = Ut::Vec2d(cx + halfW, cy - halfH);

    m_sceneManager->addEntity(imgEntity);
    return true;
}
