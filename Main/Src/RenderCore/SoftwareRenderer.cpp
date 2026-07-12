#include "SoftwareRenderer.h"

#include "RenderTypes.h"

#include <QCoreApplication>
#include <QFont>

namespace
{
    QString trSoftwareRenderer(const char* text)
    {
        return QCoreApplication::translate("SoftwareRenderer", text);
    }
}

// ============================================================================
// 主渲染入口
// ============================================================================

void SoftwareRenderer::render(QPainter& painter, const RenderFrame& frame,
    const ViewCamera3D& camera, const Size2D& viewportSize)
{
    // 清屏
    painter.fillRect(0, 0, viewportSize.width, viewportSize.height, QColor(30, 30, 30));

    if (!frame.valid || frame.batches.empty())
    {
        painter.setPen(QColor(120, 120, 120));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
        painter.drawText(QRect(0, 0, viewportSize.width, viewportSize.height),
            Qt::AlignCenter,
            trSoftwareRenderer("RenderCore pipeline ready\nWaiting for scene data..."));
        return;
    }

    drawBatches3D(painter, frame, camera);
    drawAxesIndicator(painter, camera, viewportSize.width, viewportSize.height);
    drawStatisticsOverlay(painter, frame, viewportSize.width, viewportSize.height);
}

// ============================================================================
// 3D 批次绘制（委托 Camera3D 投影）
// ============================================================================

void SoftwareRenderer::drawBatches3D(QPainter& painter, const RenderFrame& frame,
    const ViewCamera3D& camera)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const auto& batch : frame.batches)
    {
        if (batch.vertices.size() < 2)
            continue;

        QColor color;
        if (batch.selected)
        {
            color = QColor(51, 204, 255);
        }
        else
        {
            const auto& v0 = batch.vertices[0];
            color = QColor::fromRgbF(v0.r, v0.g, v0.b, v0.a);
        }

        painter.setPen(QPen(color, batch.lineWidth));

        switch (batch.primitiveType)
        {
            case PrimitiveType::Lines:
                for (int i = 0; i < batch.vertices.size() - 1; i += 2)
                {
                    int sx1, sy1, sx2, sy2;
                    const auto& v1 = batch.vertices[i];
                    const auto& v2 = batch.vertices[i + 1];
                    if (camera.project(v1.x, v1.y, v1.z, sx1, sy1) &&
                        camera.project(v2.x, v2.y, v2.z, sx2, sy2))
                    {
                        painter.drawLine(sx1, sy1, sx2, sy2);
                    }
                }
                break;

            case PrimitiveType::LineStrip:
                for (int i = 0; i < batch.vertices.size() - 1; ++i)
                {
                    int sx1, sy1, sx2, sy2;
                    const auto& v1 = batch.vertices[i];
                    const auto& v2 = batch.vertices[i + 1];
                    if (camera.project(v1.x, v1.y, v1.z, sx1, sy1) &&
                        camera.project(v2.x, v2.y, v2.z, sx2, sy2))
                    {
                        painter.drawLine(sx1, sy1, sx2, sy2);
                    }
                }
                break;

            case PrimitiveType::Points:
            {
                QPen ptPen(color, batch.pointSize > 0 ? batch.pointSize : 3);
                painter.setPen(ptPen);
                for (const auto& v : batch.vertices)
                {
                    int sx, sy;
                    if (camera.project(v.x, v.y, v.z, sx, sy))
                        painter.drawPoint(sx, sy);
                }
            }
            break;

            default:
                break;
        }
    }
}

// ============================================================================
// 坐标轴指示器（静态工具方法）
// ============================================================================

void SoftwareRenderer::drawAxesIndicator(QPainter& painter, const ViewCamera3D& camera,
    int viewW, int viewH)
{
    const int cx = viewW - 80;
    const int cy = viewH - 80;
    const int len = 50;

    const double yawRad = camera.yaw() * M_PI / 180.0;
    const double pitchRad = camera.pitch() * M_PI / 180.0;

    auto projAxis = [&](double ax, double ay, double az) -> QPoint {
        double rx = ax * qCos(yawRad) + az * qSin(yawRad);
        double ry = ay;
        double rz = -ax * qSin(yawRad) + az * qCos(yawRad);
        double ry2 = ry * qCos(pitchRad) - rz * qSin(pitchRad);
        double rz2 = ry * qSin(pitchRad) + rz * qCos(pitchRad);
        double camZ = rz2 + 10.0;
        double scale = 50.0 / camZ;
        return QPoint(cx + static_cast<int>(rx * scale),
            cy - static_cast<int>(ry2 * scale));
        };

    QPoint origin = projAxis(0, 0, 0);

    painter.setFont(QFont(QStringLiteral("Consolas"), 8));

    // X 红
    painter.setPen(QPen(QColor(255, 80, 80), 2));
    QPoint xEnd = projAxis(1, 0, 0);
    painter.drawLine(origin, xEnd);
    painter.drawText(xEnd + QPoint(5, 0), QStringLiteral("X"));

    // Y 绿
    painter.setPen(QPen(QColor(80, 255, 80), 2));
    QPoint yEnd = projAxis(0, 1, 0);
    painter.drawLine(origin, yEnd);
    painter.drawText(yEnd + QPoint(5, 0), QStringLiteral("Y"));

    // Z 蓝
    painter.setPen(QPen(QColor(80, 80, 255), 2));
    QPoint zEnd = projAxis(0, 0, 1);
    painter.drawLine(origin, zEnd);
    painter.drawText(zEnd + QPoint(5, 0), QStringLiteral("Z"));
}

// ============================================================================
// 帧统计覆盖层（静态工具方法）
// ============================================================================

void SoftwareRenderer::drawStatisticsOverlay(QPainter& painter,
    const RenderFrame& frame,
    int viewW, int viewH)
{
    painter.setPen(QColor(100, 100, 100));
    painter.setFont(QFont(QStringLiteral("Consolas"), 9));
    painter.drawText(10, viewH - 30,
        QStringLiteral("Frame %1 | %2 batches | %3 verts | %4 ents | %5 ms")
        .arg(frame.frameId)
        .arg(frame.statistics.batchCount)
        .arg(frame.statistics.totalVertexCount)
        .arg(frame.statistics.entityCount)
        .arg(frame.statistics.compileTimeMs, 0, 'f', 2));
}