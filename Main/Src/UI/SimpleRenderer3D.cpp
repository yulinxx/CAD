#include "SimpleRenderer3D.h"

#include <QPainter>
#include <QtMath>

#include "UiEntities.h"

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDegToRad = kPi / 180.0;
    constexpr double kRadToDeg = 180.0 / kPi;
    constexpr double kHitRadius = 12.0;
    constexpr double kZoomSpeed = 0.1;
    constexpr double kRotateSpeed = 0.005;
    constexpr double kPanSpeed = 0.02;
    constexpr double kDefaultDistance = 10.0;
    constexpr double kMinDistance = 1.0;
    constexpr double kMaxDistance = 100.0;
    constexpr double kMaxPitch = 89.0;
    constexpr double kAxisLength = 3.0;
    constexpr double kCubeHalfSize = 1.0;
    constexpr double kNodeHalfSize = 0.3;
    constexpr double kNodeSpacing = 2.0;
}

SimpleRenderer3D::SimpleRenderer3D() = default;
SimpleRenderer3D::~SimpleRenderer3D() = default;

// ========== 生命周期管理 ==========

bool SimpleRenderer3D::initialize(void* windowHandle)
{
    Q_UNUSED(windowHandle);
    m_ready = true;
    return true;
}

void SimpleRenderer3D::shutdown()
{
    m_ready = false;
    m_renderLoopEnabled = false;
}

bool SimpleRenderer3D::isReady() const
{
    return m_ready;
}

void SimpleRenderer3D::setRenderLoopEnabled(bool enabled)
{
    m_renderLoopEnabled = enabled;
}

bool SimpleRenderer3D::isRenderLoopRunning() const
{
    return m_renderLoopEnabled && m_ready;
}

// ========== 场景与相机 ==========

void SimpleRenderer3D::setScene(SceneDocument3D* document)
{
    m_document = document;
}

void SimpleRenderer3D::setCamera(CameraController3D* controller)
{
    m_cameraController = controller;
}

void SimpleRenderer3D::resize(int width, int height)
{
    m_viewWidth = width;
    m_viewHeight = height;
}

void SimpleRenderer3D::resetView()
{
    if (m_cameraController)
        m_cameraController->reset();
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = kDefaultDistance;
    m_panX = 0.0;
    m_panY = 0.0;
    emitStatus(QStringLiteral("3D view reset"));
}

// ========== 模式 ==========

void SimpleRenderer3D::setOrbitMode(bool enabled)
{
    m_orbitMode = enabled;
}

void SimpleRenderer3D::setMeasureMode(bool enabled)
{
    m_measureMode = enabled;
}

bool SimpleRenderer3D::isOrbitMode() const
{
    return m_orbitMode;
}

// ========== 3D 投影 ==========

bool SimpleRenderer3D::project(float x, float y, float z, int& sx, int& sy) const
{
    // 相机球坐标 → 世界坐标
    const double pitchRad = m_pitch * kDegToRad;
    const double yawRad = m_yaw * kDegToRad;

    const double camX = m_distance * std::cos(pitchRad) * std::sin(yawRad) + m_panX;
    const double camY = m_distance * std::sin(pitchRad) + m_panY;
    const double camZ = m_distance * std::cos(pitchRad) * std::cos(yawRad);

    // 目标点
    const double targetX = m_panX;
    const double targetY = m_panY;
    const double targetZ = 0.0;

    // 前方向量
    double fx = targetX - camX;
    double fy = targetY - camY;
    double fz = targetZ - camZ;
    const double fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fLen < 1e-6) return false;
    fx /= fLen;
    fy /= fLen;
    fz /= fLen;

    // 右方向量 = forward × worldUp(0,1,0)
    double rx = fy * 0.0 - fz * 1.0;
    double ry = fz * 0.0 - fx * 0.0;
    double rz = fx * 1.0 - fy * 0.0;
    const double rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rLen < 1e-6) return false;
    rx /= rLen;
    ry /= rLen;
    rz /= rLen;

    // 上方向量 = right × forward
    const double ux = ry * fz - rz * fy;
    const double uy = rz * fx - rx * fz;
    const double uz = rx * fy - ry * fx;

    // 世界坐标 → 相机空间
    const double dx = x - camX;
    const double dy = y - camY;
    const double dz = z - camZ;

    const double depth = dx * fx + dy * fy + dz * fz;
    if (depth < 0.01) return false;

    const double screenX = dx * rx + dy * ry + dz * rz;
    const double screenY = dx * ux + dy * uy + dz * uz;

    // 透视投影 + 屏幕映射
    const double scale = std::min(m_viewWidth, m_viewHeight) * 0.5;
    sx = static_cast<int>(screenX / depth * scale + m_viewWidth * 0.5);
    sy = static_cast<int>(-screenY / depth * scale + m_viewHeight * 0.5);

    return true;
}

// ========== 渲染 ==========

void SimpleRenderer3D::render(QPainter& painter, int width, int height)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 背景
    painter.fillRect(0, 0, width, height, QColor(40, 42, 48));

    drawAxes(painter);
    drawWireCube(painter, 0.0f, 0.0f, 0.0f, static_cast<float>(kCubeHalfSize));
    drawSceneNodes(painter);
    drawNodePathOverlay(painter);
}

void SimpleRenderer3D::drawAxes(QPainter& painter)
{
    const QColor axisColors[3] = {
        QColor(220, 80, 80),
        QColor(80, 220, 120),
        QColor(80, 140, 255)
    };
    const QString axisLabels[3] = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z") };

    for (int axis = 0; axis < 3; ++axis)
    {
        float start[3] = { 0, 0, 0 };
        float end[3] = { 0, 0, 0 };
        end[axis] = static_cast<float>(kAxisLength);

        int sx0, sy0, sx1, sy1;
        if (!project(start[0], start[1], start[2], sx0, sy0)) continue;
        if (!project(end[0], end[1], end[2], sx1, sy1)) continue;

        painter.setPen(QPen(axisColors[axis], 2));
        painter.drawLine(sx0, sy0, sx1, sy1);

        painter.setPen(axisColors[axis]);
        painter.drawText(sx1 + 4, sy1 + 4, axisLabels[axis]);
    }
}

void SimpleRenderer3D::drawWireCube(QPainter& painter, float cx, float cy, float cz, float halfSize)
{
    const float hs = halfSize;
    const float verts[8][3] = {
        {cx - hs, cy - hs, cz - hs}, {cx + hs, cy - hs, cz - hs},
        {cx + hs, cy + hs, cz - hs}, {cx - hs, cy + hs, cz - hs},
        {cx - hs, cy - hs, cz + hs}, {cx + hs, cy - hs, cz + hs},
        {cx + hs, cy + hs, cz + hs}, {cx - hs, cy + hs, cz + hs}
    };

    const int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };

    painter.setPen(QPen(QColor(180, 200, 220), 1));

    for (const auto& edge : edges)
    {
        int sx0, sy0, sx1, sy1;
        if (!project(verts[edge[0]][0], verts[edge[0]][1], verts[edge[0]][2], sx0, sy0)) continue;
        if (!project(verts[edge[1]][0], verts[edge[1]][1], verts[edge[1]][2], sx1, sy1)) continue;
        painter.drawLine(sx0, sy0, sx1, sy1);
    }
}

void SimpleRenderer3D::drawSceneNodes(QPainter& painter)
{
    if (!m_document)
        return;

    int nodeIndex = 0;
    const auto& entities = m_document->entities();
    for (const auto& entity : entities)
    {
        auto node = std::dynamic_pointer_cast<SceneNode>(entity);
        if (!node)
            continue;

        // 按索引排列节点
        const float angle = static_cast<float>(nodeIndex) * 0.8f;
        const float radius = 3.0f;
        const float nx = std::cos(angle) * radius;
        const float ny = static_cast<float>(nodeIndex) * 0.5f - 1.0f;
        const float nz = std::sin(angle) * radius;

        const bool isSelected = (node->id() == m_selectedNodeId);
        const float nodeSize = isSelected ? kNodeHalfSize * 1.5f : kNodeHalfSize;
        const QColor color = isSelected ? QColor(80, 200, 255) : QColor(140, 160, 180);

        drawWireCube(painter, nx, ny, nz, nodeSize);

        int sx, sy;
        if (project(nx, ny + nodeSize + 0.2f, nz, sx, sy))
        {
            painter.setPen(color);
            painter.drawText(sx - 20, sy, 40, 16, Qt::AlignCenter, node->name());
        }

        ++nodeIndex;
    }
}

void SimpleRenderer3D::drawNodePathOverlay(QPainter& painter)
{
    if (m_selectedNodeId.isEmpty() || m_selectedPathNames.isEmpty())
        return;

    const QString pathText = QStringLiteral("Path: ") + m_selectedPathNames.join(QStringLiteral(" / "));
    const QString nodeText = QStringLiteral("Node: ") + m_selectedNodeId;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRect(8, m_viewHeight - 48, 400, 40);

    painter.setPen(QColor(200, 220, 240));
    painter.drawText(16, m_viewHeight - 30, pathText);
    painter.drawText(16, m_viewHeight - 12, nodeText);
}

// ========== 输入事件 ==========

void SimpleRenderer3D::onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH)
{
    Q_UNUSED(modifiers);
    m_viewWidth = viewW;
    m_viewHeight = viewH;
    m_lastMousePos = QPoint(x, y);

    if (button == 1) // Qt::LeftButton
    {
        if (m_orbitMode)
        {
            m_rotating = true;
            emitStatus(QStringLiteral("3D orbit"));
        }
        else
        {
            const QString hitId = hitTest(x, y);
            if (!hitId.isEmpty())
            {
                selectNodeById(hitId);
            }
        }
    }
    else if (button == 4) // Qt::MiddleButton
    {
        m_panning = true;
        emitStatus(QStringLiteral("3D pan"));
    }
}

void SimpleRenderer3D::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    Q_UNUSED(buttons);
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    const int dx = x - m_lastMousePos.x();
    const int dy = y - m_lastMousePos.y();
    m_lastMousePos = QPoint(x, y);

    if (m_rotating)
    {
        m_yaw += dx * kRotateSpeed * kRadToDeg;
        m_pitch += dy * kRotateSpeed * kRadToDeg;
        m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);
        emitStatus(QStringLiteral("3D orbiting"));
    }
    else if (m_panning)
    {
        const double panScale = m_distance * kPanSpeed;
        m_panX -= dx * panScale;
        m_panY += dy * panScale;
        emitStatus(QStringLiteral("3D panning"));
    }
}

void SimpleRenderer3D::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    if (button == 1) // Qt::LeftButton
    {
        if (m_rotating)
        {
            m_rotating = false;
            emitStatus(QStringLiteral("3D ready"));
        }
    }
    else if (button == 4) // Qt::MiddleButton
    {
        m_panning = false;
        emitStatus(QStringLiteral("3D ready"));
    }
}

void SimpleRenderer3D::onWheel(int delta, int viewW, int viewH)
{
    m_viewWidth = viewW;
    m_viewHeight = viewH;

    m_distance -= delta * kZoomSpeed;
    m_distance = std::clamp(m_distance, kMinDistance, kMaxDistance);
    emitStatus(QStringLiteral("3D zoom: %.1f").arg(m_distance));
}

// ========== 选择管理 ==========

void SimpleRenderer3D::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    if (m_document)
    {
        m_document->selection().clear();
        if (auto entity = m_document->entityById(nodeId))
            m_document->selection().add(entity);
    }
    rebuildTreeHighlight();

    if (m_selectionCallback)
        m_selectionCallback(nodeId);
    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QStringLiteral("3D selected: %1").arg(nodeId));
}

QString SimpleRenderer3D::selectedNodeId() const
{
    return m_selectedNodeId;
}

QStringList SimpleRenderer3D::selectedPathNames() const
{
    return m_selectedPathNames;
}

// ========== 回调 ==========

void SimpleRenderer3D::setStatusCallback(StatusCallback callback)
{
    m_statusCallback = std::move(callback);
}

void SimpleRenderer3D::setSelectionCallback(SelectionCallback callback)
{
    m_selectionCallback = std::move(callback);
}

void SimpleRenderer3D::setPathCallback(PathCallback callback)
{
    m_pathCallback = std::move(callback);
}

// ========== 内部方法 ==========

void SimpleRenderer3D::emitStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}

QString SimpleRenderer3D::hitTest(int screenX, int screenY) const
{
    if (!m_document)
        return {};

    double bestDist = kHitRadius;
    QString bestId;

    int nodeIndex = 0;
    for (const auto& entity : m_document->entities())
    {
        auto node = std::dynamic_pointer_cast<SceneNode>(entity);
        if (!node)
            continue;

        const float angle = static_cast<float>(nodeIndex) * 0.8f;
        const float radius = 3.0f;
        const float nx = std::cos(angle) * radius;
        const float ny = static_cast<float>(nodeIndex) * 0.5f - 1.0f;
        const float nz = std::sin(angle) * radius;

        int sx, sy;
        if (!project(nx, ny, nz, sx, sy))
        {
            ++nodeIndex;
            continue;
        }

        const double dx = screenX - sx;
        const double dy = screenY - sy;
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist < bestDist)
        {
            bestDist = dist;
            bestId = node->id();
        }

        ++nodeIndex;
    }

    return bestId;
}

void SimpleRenderer3D::rebuildTreeHighlight()
{
    m_selectedPathNames.clear();

    if (!m_document || m_selectedNodeId.isEmpty())
        return;

    const auto entity = m_document->entityById(m_selectedNodeId);
    const auto node = std::dynamic_pointer_cast<SceneNode>(entity);
    if (!node)
        return;

    m_selectedPathNames = node->pathNamesRecursive();

    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QStringLiteral("3D path: %1").arg(m_selectedPathNames.join(QStringLiteral(" / "))));
}