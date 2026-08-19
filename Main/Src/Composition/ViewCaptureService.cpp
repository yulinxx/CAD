#include "UI/Service/ViewCaptureService.h"
#include "RenderWidget.h"
#include "UI3D/Render3D/RenderWidget3D.h"
#include "Engine2D/Environment/SceneEnvironment.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Ut/BBox3.h"
#include "render/render.h"

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
            path = QFileDialog::getSaveFileName(
                nullptr,
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

    QImage ViewCaptureService::capture2D(RenderWidget* widget, const CaptureRequest& req)
    {
        if (!widget || !widget->isInitialized())
        {
            return QImage();
        }

        render::RenderDevice* dev = widget->renderDevice();
        if (!dev)
        {
            return QImage();
        }

        // 计算目标分辨率
        QSize targetSize = req.resolution;
        if (targetSize.isEmpty())
        {
            targetSize = widget->size() * widget->devicePixelRatioF();
        }

        // 根据 framing 准备 view matrix
        Render::Mat3f savedView = widget->viewMatrix();
        Render::Mat3f targetView = savedView;

        if (req.framing == FramingKind::FitAll)
        {
            // TODO: 计算全场景包围盒并生成 fit 矩阵
            // 需要场景管理器提供所有图元的 bbox
        }
        else if (req.framing == FramingKind::CustomBox)
        {
            // TODO: 用户自定义范围计算映射矩阵
        }

        // 应用目标视图矩阵（内部会自动同步 cameraCenter）
        if (req.framing != FramingKind::UseCurrent)
        {
            renderSetView2D(dev, targetView.data, targetSize.width(), targetSize.height());
        }

        // 执行离屏捕获
        std::vector<uint8_t> buf(targetSize.width() * targetSize.height() * 4);
        uint32_t rowPitch = 0;
        bool ok = renderCaptureFrame(dev, targetSize.width(), targetSize.height(), buf.data(), &rowPitch);

        // 恢复原视图矩阵（内部会自动同步 cameraCenter）
        if (req.framing != FramingKind::UseCurrent)
        {
            renderSetView2D(dev, savedView.data, widget->width(), widget->height());
        }

        if (!ok)
        {
            return QImage();
        }

        // 转 QImage（GL 左下角原点 -> QImage 左上角原点，需翻转）
        QImage img(buf.data(), targetSize.width(), targetSize.height(), rowPitch, QImage::Format_RGBA8888);
        QImage flipped = img.mirrored(false, true);  // 垂直翻转
        return flipped.copy();  // 确保数据拥有权
    }

    QImage ViewCaptureService::capture3D(RenderWidget3D* widget, const CaptureRequest& req)
    {
        if (!widget)
        {
            return QImage();
        }

        QSize targetSize = req.resolution;
        if (targetSize.isEmpty())
        {
            targetSize = widget->size() * widget->devicePixelRatioF();
        }

        // 根据 framing 选择相机
        if (req.framing == FramingKind::UseCurrent)
        {
            // 当前视图：使用 widget 当前相机
            QMatrix4x4 view = widget->viewMatrix();
            QMatrix4x4 proj = widget->projectionMatrix();
            return widget->captureOffscreen(targetSize.width(), targetSize.height(), view, proj);
        }
        else if (req.framing == FramingKind::PresetCamera3D || req.framing == FramingKind::FitAll)
        {
            // 计算场景包围盒
            if (widget->sceneManager())
            {
                Ut::BBox3f sceneBBox = widget->sceneManager()->sceneBBox3D();
                if (sceneBBox.isValid())
                {
                    return widget->captureIsometricOffscreen(targetSize.width(), targetSize.height(), sceneBBox);
                }
            }
            // 无场景或 bbox 无效，回退当前相机
            QMatrix4x4 view = widget->viewMatrix();
            QMatrix4x4 proj = widget->projectionMatrix();
            return widget->captureOffscreen(targetSize.width(), targetSize.height(), view, proj);
        }
        else if (req.framing == FramingKind::CustomBox)
        {
            // 自定义 3D 范围：TODO
        }

        return QImage();
    }
}