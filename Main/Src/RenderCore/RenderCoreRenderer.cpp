#include "RenderCoreRenderer.h"

#include "DefaultSceneCompiler.h"
#include "IRenderBackend.h"
#include "RenderBackendFactory.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "RenderTypes.h"

#include "../UI/UiEntities.h"

#include <QPainter>
#include <QtMath>
#include <chrono>

// ============================================================================
// 构造/析构
// ============================================================================

RenderCoreRenderer::RenderCoreRenderer()
{
    // 使用默认场景编译器
    m_compiler = std::make_unique<DefaultSceneCompiler>();

    // 使用占位后端（软件路径不需要真实 GPU 后端）
    m_backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);

    m_context.backendName = QStringLiteral("RenderCore");
    m_context.sceneType = QStringLiteral("3D");
    m_context.renderMode = RenderMode::Wireframe;
}

RenderCoreRenderer::~RenderCoreRenderer()
{
    shutdown();
}

// ============================================================================
// 生命周期
// ============================================================================

bool RenderCoreRenderer::initialize(void* windowHandle)
{
    if (m_ready)
        return true;

    if (m_useOpenGL && m_backend)
    {
        if (!m_backend->initialize(windowHandle))
            return false;
    }

    m_ready = true;
    emitStatus(QStringLiteral("RenderCore 渲染器就绪"));
    return true;
}

void RenderCoreRenderer::shutdown()
{
    if (m_backend)
        m_backend->shutdown();
    m_ready = false;
}

bool RenderCoreRenderer::isReady() const
{
    return m_ready;
}

void RenderCoreRenderer::setRenderLoopEnabled(bool enabled)
{
    m_renderLoopEnabled = enabled;
}

bool RenderCoreRenderer::isRenderLoopRunning() const
{
    return m_renderLoopEnabled;
}

bool RenderCoreRenderer::isOpenGL() const
{
    return m_useOpenGL;
}

// ============================================================================
// 场景与相机
// ============================================================================

void RenderCoreRenderer::setScene(SceneDocument3D* document)
{
    m_document = document;
    m_context.markDirty(DirtyRegionType::Geometry);
    m_compiler->invalidateCache();
}

void RenderCoreRenderer::setCamera(CameraController3D* controller)
{
    m_cameraController = controller;
    m_context.markDirty(DirtyRegionType::View);
}

// ============================================================================
// 场景编译
// ============================================================================

RenderFrame RenderCoreRenderer::compileScene()
{
    if (!m_document)
        return {};

    m_context.advanceFrame();

    const auto compileStart = std::chrono::steady_clock::now();

    RenderFrame frame;
    if (m_context.isDirty && m_compiler->hasCachedFrame())
    {
        frame = m_compiler->compileIncremental(m_document, m_context,
                                                RenderFrame{}); // 上一帧缓存
    }
    else
    {
        frame = m_compiler->compile(m_document, m_context);
    }

    const auto compileEnd = std::chrono::steady_clock::now();
    frame.statistics.compileTimeMs =
        std::chrono::duration<float, std::milli>(compileEnd - compileStart).count();

    m_context.clearDirty();
    return frame;
}

// ============================================================================
// 渲染
// ============================================================================

void RenderCoreRenderer::render(QPainter& painter, int width, int height)
{
    if (!m_ready)
        return;

    m_viewWidth = width;
    m_viewHeight = height;
    m_context.viewportSize = QSize(width, height);

    // 编译场景
    RenderFrame frame = compileScene();

    if (m_useOpenGL && m_backend && m_backend->isReady())
    {
        renderOpenGL(painter);
    }
    else
    {
        renderSoftware(painter, frame);
    }
}

void RenderCoreRenderer::renderSoftware(QPainter& painter, const RenderFrame& frame)
{
    // 清屏
    painter.fillRect(0, 0, m_viewWidth, m_viewHeight, QColor(30, 30, 30));

    if (!frame.valid || frame.batches.isEmpty())
    {
        // 绘制提示文本
        painter.setPen(QColor(120, 120, 120));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
        painter.drawText(QRect(0, 0, m_viewWidth, m_viewHeight),
                         Qt::AlignCenter,
                         QStringLiteral("RenderCore 渲染管线就绪\n等待场景数据..."));
        return;
    }

    // 绘制场景批次
    renderBatches3D(painter, frame);

    // 绘制帧统计覆盖层
    painter.setPen(QColor(100, 100, 100));
    painter.setFont(QFont(QStringLiteral("Consolas"), 9));
    painter.drawText(10, m_viewHeight - 30,
                     QStringLiteral("Frame %1 | %2 batches | %3 verts | %4 ents | compile %5 ms")
                         .arg(frame.frameId)
                         .arg(frame.statistics.batchCount)
                         .arg(frame.statistics.totalVertexCount)
                         .arg(frame.statistics.entityCount)
                         .arg(frame.statistics.compileTimeMs, 0, 'f', 2));
}

void RenderCoreRenderer::renderOpenGL(QPainter& painter)
{
    // OpenGL 路径：使用后端渲染到 FBO，捕获后绘制
    if (!m_backend || !m_backend->isReady())
        return;

    m_backend->bindContext(m_context);
    m_backend->beginFrame();
    m_backend->render();
    m_backend->endFrame();

    QImage captured = m_backend->captureFrame();
    if (!captured.isNull())
    {
        painter.drawImage(0, 0, captured);
    }
}

// ============================================================================
// 3D 批次渲染（软件投影）
// ============================================================================

bool RenderCoreRenderer::project3D(float x, float y, float z, int& sx, int& sy) const
{
    // 简单的透视投影：绕 Y 轴旋转 yaw，绕 X 轴旋转 pitch
    const double yawRad = qDegreesToRadians(m_yaw);
    const double pitchRad = qDegreesToRadians(m_pitch);

    // 旋转
    double rx = x * qCos(yawRad) + z * qSin(yawRad);
    double ry = y;
    double rz = -x * qSin(yawRad) + z * qCos(yawRad);

    // 绕 X 轴旋转（pitch）
    double rx2 = rx;
    double ry2 = ry * qCos(pitchRad) - rz * qSin(pitchRad);
    double rz2 = ry * qSin(pitchRad) + rz * qCos(pitchRad);

    // 透视投影
    const double fov = 60.0;
    const double scale = m_viewHeight / (2.0 * qTan(qDegreesToRadians(fov / 2.0)));
    const double camZ = rz2 + m_distance;

    if (camZ < 0.1)
        return false;

    const double projX = rx2 * scale / camZ;
    const double projY = ry2 * scale / camZ;

    sx = static_cast<int>(m_viewWidth / 2.0 + projX + m_panX);
    sy = static_cast<int>(m_viewHeight / 2.0 - projY + m_panY);

    return true;
}

void RenderCoreRenderer::renderBatches3D(QPainter& painter, const RenderFrame& frame)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const auto& batch : frame.batches)
    {
        if (batch.vertices.size() < 2)
            continue;

        // 设置颜色
        QColor color;
        if (batch.selected)
        {
            color = QColor(51, 204, 255);  // 选中高亮蓝
        }
        else
        {
            // 从第一个顶点取颜色
            const auto& v0 = batch.vertices[0];
            color = QColor::fromRgbF(v0.r, v0.g, v0.b, v0.a);
        }

        QPen pen(color, batch.lineWidth);
        painter.setPen(pen);

        // 根据图元类型绘制
        switch (batch.primitiveType)
        {
        case PrimitiveType::Lines:
        {
            for (int i = 0; i < batch.vertices.size() - 1; i += 2)
            {
                int sx1, sy1, sx2, sy2;
                const auto& v1 = batch.vertices[i];
                const auto& v2 = batch.vertices[i + 1];
                if (project3D(v1.x, v1.y, v1.z, sx1, sy1) &&
                    project3D(v2.x, v2.y, v2.z, sx2, sy2))
                {
                    painter.drawLine(sx1, sy1, sx2, sy2);
                }
            }
            break;
        }
        case PrimitiveType::LineStrip:
        {
            for (int i = 0; i < batch.vertices.size() - 1; ++i)
            {
                int sx1, sy1, sx2, sy2;
                const auto& v1 = batch.vertices[i];
                const auto& v2 = batch.vertices[i + 1];
                if (project3D(v1.x, v1.y, v1.z, sx1, sy1) &&
                    project3D(v2.x, v2.y, v2.z, sx2, sy2))
                {
                    painter.drawLine(sx1, sy1, sx2, sy2);
                }
            }
            break;
        }
        case PrimitiveType::Points:
        {
            QPen pointPen(color, batch.pointSize > 0 ? batch.pointSize : 3);
            painter.setPen(pointPen);
            for (const auto& v : batch.vertices)
            {
                int sx, sy;
                if (project3D(v.x, v.y, v.z, sx, sy))
                {
                    painter.drawPoint(sx, sy);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    // 绘制坐标轴指示器
    drawAxesIndicator(painter);
}

void RenderCoreRenderer::drawAxesIndicator(QPainter& painter)
{
    // 右下角坐标轴指示器
    const int cx = m_viewWidth - 80;
    const int cy = m_viewHeight - 80;
    const int len = 50;

    // 使用当前 yaw/pitch 投影轴方向
    const double yawRad = qDegreesToRadians(m_yaw);
    const double pitchRad = qDegreesToRadians(m_pitch);

    auto projAxis = [&](double ax, double ay, double az) -> QPoint {
        double rx = ax * qCos(yawRad) + az * qSin(yawRad);
        double ry = ay;
        double rz = -ax * qSin(yawRad) + az * qCos(yawRad);
        double ry2 = ry * qCos(pitchRad) - rz * qSin(pitchRad);
        double rz2 = ry * qSin(pitchRad) + rz * qCos(pitchRad);
        double camZ = rz2 + 10.0;
        double scale = 50.0 / camZ;
        return QPoint(cx + static_cast<int>(rx * scale), cy - static_cast<int>(ry2 * scale));
    };

    QPoint origin = projAxis(0, 0, 0);
    QPoint xEnd = projAxis(1, 0, 0);
    QPoint yEnd = projAxis(0, 1, 0);
    QPoint zEnd = projAxis(0, 0, 1);

    QFont font(QStringLiteral("Consolas"), 8);
    painter.setFont(font);

    // X 轴（红）
    painter.setPen(QPen(QColor(255, 80, 80), 2));
    painter.drawLine(origin, xEnd);
    painter.drawText(xEnd + QPoint(5, 0), QStringLiteral("X"));

    // Y 轴（绿）
    painter.setPen(QPen(QColor(80, 255, 80), 2));
    painter.drawLine(origin, yEnd);
    painter.drawText(yEnd + QPoint(5, 0), QStringLiteral("Y"));

    // Z 轴（蓝）
    painter.setPen(QPen(QColor(80, 80, 255), 2));
    painter.drawLine(origin, zEnd);
    painter.drawText(zEnd + QPoint(5, 0), QStringLiteral("Z"));
}

// ============================================================================
// 视口控制
// ============================================================================

void RenderCoreRenderer::resize(int width, int height)
{
    m_viewWidth = width;
    m_viewHeight = height;
    m_context.viewportSize = QSize(width, height);
    m_context.markDirty(DirtyRegionType::View);
}

void RenderCoreRenderer::resetView()
{
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = 10.0;
    m_panX = 0.0;
    m_panY = 0.0;
    m_context.markDirty(DirtyRegionType::View);
}

// ============================================================================
// 模式切换
// ============================================================================

void RenderCoreRenderer::setOrbitMode(bool enabled)
{
    m_orbitMode = enabled;
}

void RenderCoreRenderer::setMeasureMode(bool enabled)
{
    m_measureMode = enabled;
}

bool RenderCoreRenderer::isOrbitMode() const
{
    return m_orbitMode;
}

// ============================================================================
// 输入事件
// ============================================================================

void RenderCoreRenderer::onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH)
{
    Q_UNUSED(modifiers);
    m_viewWidth = viewW;
    m_viewHeight = viewH;
    m_lastMousePos = QPoint(x, y);

    if (button == 2) // 中键
    {
        m_panning = true;
        emitStatus(QStringLiteral("平移"));
    }
    else if (button == 1 && m_orbitMode) // 左键
    {
        m_rotating = true;
        emitStatus(QStringLiteral("旋转"));
    }
}

void RenderCoreRenderer::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    Q_UNUSED(buttons);
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    int dx = x - m_lastMousePos.x();
    int dy = y - m_lastMousePos.y();
    m_lastMousePos = QPoint(x, y);

    if (m_rotating)
    {
        m_yaw += dx * 0.5;
        m_pitch += dy * 0.5;
        m_pitch = qBound(-89.0, m_pitch, 89.0);
        m_context.markDirty(DirtyRegionType::View);
    }
    else if (m_panning)
    {
        m_panX += dx;
        m_panY += dy;
        m_context.markDirty(DirtyRegionType::View);
    }
}

void RenderCoreRenderer::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    if (button == 2)
        m_panning = false;
    else if (button == 1)
        m_rotating = false;

    emitStatus(m_measureMode ? QStringLiteral("测量模式") : QStringLiteral("就绪"));
}

void RenderCoreRenderer::onWheel(int delta, int viewW, int viewH)
{
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    m_distance -= delta * 0.01;
    m_distance = qBound(1.0, m_distance, 100.0);
    m_context.markDirty(DirtyRegionType::View);
}

// ============================================================================
// 选择管理
// ============================================================================

void RenderCoreRenderer::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    m_selectedPathNames.clear();
    if (m_selectionCallback)
        m_selectionCallback(nodeId);
}

QString RenderCoreRenderer::selectedNodeId() const
{
    return m_selectedNodeId;
}

QStringList RenderCoreRenderer::selectedPathNames() const
{
    return m_selectedPathNames;
}

// ============================================================================
// 回调
// ============================================================================

void RenderCoreRenderer::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
}

void RenderCoreRenderer::setSelectionCallback(SelectionCallback callback)
{
    m_selectionCallback = std::move(callback);
}

void RenderCoreRenderer::setPathCallback(PathCallback callback)
{
    m_pathCallback = std::move(callback);
}

void RenderCoreRenderer::emitStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}