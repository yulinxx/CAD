#include "UI/Service/ViewCaptureService.h"
#include "RenderWidget.h"
#include "UI3D/Render3D/RenderWidget3D.h"
#include "Engine2D/Environment/SceneEnvironment.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Ut/BBox3.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QImage>

namespace Ui
{
    ViewCaptureService::ViewCaptureService() = default;
    ViewCaptureService::~ViewCaptureService() = default;

    QString ViewCaptureService::generateFileName(bool is3D) const
    {
        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
        return QString("SanYiCAD_%1_%2.png").arg(is3D ? "3D" : "2D").arg(ts);
    }

    QString ViewCaptureService::screenshotsDir() const
    {
        // 优先用应用数据目录下的 Screenshots
        QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (base.isEmpty())
        {
            base = QDir::homePath();
        }
        QString dir = QDir(base).filePath("SanYiCAD/Screenshots");
        QDir().mkpath(dir);
        return dir;
    }

    QString ViewCaptureService::saveImage(const QImage& img, const CaptureRequest& req, bool is3D)
    {
        if (img.isNull())
        {
            return QString();
        }

        QString path;
        if (!req.filePath.isEmpty())
        {
            path = req.filePath;
        }
        else if (req.autoSave)
        {
            path = QDir(screenshotsDir()).filePath(generateFileName(is3D));
        }
        else
        {
            path = QFileDialog::getSaveFileName(nullptr,
                QCoreApplication::translate("ViewCaptureService", "Save Screenshot"),
                QDir(screenshotsDir()).filePath(generateFileName(is3D)),
                QCoreApplication::translate("ViewCaptureService", "PNG Image (*.png);;All Files (*)"));

            if (path.isEmpty())
            {
                return QString();
            }
        }

        if (!img.save(path, "PNG"))
        {
            return QString();
        }
        return path;
    }

    QImage ViewCaptureService::capture2D(void* widget, const CaptureRequest& req)
    {
        auto* w = static_cast<RenderWidget*>(widget);
        if (!w || !w->isInitialized())
        {
            return QImage();
        }

        // 读回必须发生在 EndFrame 之前，所以整个「渲染一帧 + 读像素」序列都在
        // 控件内部完成（RenderWidget::captureFrameRGBA）。此前这里持有
        // Render::RenderDevice* 自己调 renderReadPixels，是宿主唯一一处
        // 直接握着渲染设备句柄的地方，也是渲染 DLL 无法独立替换的原因之一。
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        if (!w->captureFrameRGBA(rgba, width, height) || width == 0 || height == 0)
        {
            return QImage();
        }

        // DLL 保证输出为 RGBA8、左上原点、逐行紧凑，因此这里既不翻转也不换序。
        const QImage img(rgba.data(), static_cast<int>(width), static_cast<int>(height),
            static_cast<qsizetype>(width) * 4, QImage::Format_RGBA8888);
        QImage result = img.copy();  // 脱离 rgba 的生命周期

        // 指定分辨率时按后备缓冲结果缩放。真正的「任意分辨率离屏重渲染」需要
        // 渲染到纹理的 Surface，DLL 侧尚未提供，缩放是当前唯一诚实的做法。
        const QSize targetSize = req.resolution;
        if (!targetSize.isEmpty() && targetSize != result.size())
        {
            result = result.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return result;
    }

    QImage ViewCaptureService::capture3D(void* widget, const CaptureRequest& req)
    {
        auto* w3d = static_cast<RenderWidget3D*>(widget);
        if (!w3d)
        {
            return QImage();
        }

        QSize targetSize = req.resolution;
        if (targetSize.isEmpty())
        {
            targetSize = w3d->size() * w3d->devicePixelRatioF();
        }

        // 根据 framing 选择相机
        if (req.framing == FramingKind::UseCurrent)
        {
            // 当前视图：使用 widget 当前相机
            QMatrix4x4 view = w3d->viewMatrix();
            QMatrix4x4 proj = w3d->projectionMatrix();
            return w3d->captureOffscreen(targetSize.width(), targetSize.height(), view, proj);
        }
        else if (req.framing == FramingKind::PresetCamera3D || req.framing == FramingKind::FitAll)
        {
            // 计算场景包围盒
            if (w3d->sceneManager())
            {
                Ut::BBox3f sceneBBox = w3d->sceneManager()->sceneBBox3D();
                if (sceneBBox.isValid())
                {
                    return w3d->captureIsometricOffscreen(targetSize.width(), targetSize.height(), sceneBBox);
                }
            }
            // 无场景或 bbox 无效，回退当前相机
            QMatrix4x4 view = w3d->viewMatrix();
            QMatrix4x4 proj = w3d->projectionMatrix();
            return w3d->captureOffscreen(targetSize.width(), targetSize.height(), view, proj);
        }
        else if (req.framing == FramingKind::CustomBox)
        {
            // 自定义 3D 范围：TODO
        }

        return QImage();
    }
}  // namespace Ui