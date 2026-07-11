#include "SimpleRenderer3D.h"

#include <QPainter>
#include <QObject>

#include "UiEntities.h"

namespace
{
    constexpr double kHitRadius = 12.0;
    constexpr double kAxisLength = 3.0;
    constexpr double kCubeHalfSize = 1.0;
    constexpr double kNodeHalfSize = 0.3;
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
    m_camera.setViewportSize(width, height);
}

void SimpleRenderer3D::resetView()
{
    if (m_cameraController)
        m_cameraController->reset();
    m_camera.reset();
    emitStatus(QObject::tr("3D view reset"));
}

void SimpleRenderer3D::setOrbitMode(bool enabled)
{
    m_camera.setOrbitMode(enabled);
}

void SimpleRenderer3D::setMeasureMode(bool enabled)
{
    m_camera.setMeasureMode(enabled);
}

bool SimpleRenderer3D::isOrbitMode() const
{
    return m_camera.isOrbitMode();
}

// ========== 3D 投影 ==========

bool SimpleRenderer3D::project(float x, float y, float z, int& sx, int& sy) const
{
    return m_camera.project(x, y, z, sx, sy);
}

// ========== 渲染 ==========

void SimpleRenderer3D::render(QPainter& painter, int width, int height)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    m_camera.setViewportSize(width, height);

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

        const bool isSelected = (node->id() == m_selectedNodeId.toStdString());
        const float nodeSize = isSelected ? kNodeHalfSize * 1.5f : kNodeHalfSize;
        const QColor color = isSelected ? QColor(80, 200, 255) : QColor(140, 160, 180);

        drawWireCube(painter, nx, ny, nz, nodeSize);

        int sx, sy;
        if (project(nx, ny + nodeSize + 0.2f, nz, sx, sy))
        {
            painter.setPen(color);
            painter.drawText(sx - 20, sy, 40, 16, Qt::AlignCenter, QString::fromStdString(node->name()));
        }

        ++nodeIndex;
    }
}

void SimpleRenderer3D::drawNodePathOverlay(QPainter& painter)
{
    if (m_selectedNodeId.isEmpty() || m_selectedPathNames.isEmpty())
        return;

    const QString pathText = QObject::tr("Path: ") + m_selectedPathNames.join(QObject::tr(" / "));
    const QString nodeText = QObject::tr("Node: ") + m_selectedNodeId;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRect(8, painter.viewport().height() - 48, 400, 40);

    painter.setPen(QColor(200, 220, 240));
    painter.drawText(16, painter.viewport().height() - 30, pathText);
    painter.drawText(16, painter.viewport().height() - 12, nodeText);
}

// ========== 输入事件 ==========

void SimpleRenderer3D::onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH)
{
    if (button == 1 && !m_camera.isOrbitMode())
    {
        const QString hitId = hitTest(x, y);
        if (!hitId.isEmpty())
        {
            selectNodeById(hitId);
            return;
        }
    }

    m_camera.onMousePress(x, y, button, modifiers, viewW, viewH);

    if (m_camera.isRotating())
        emitStatus(QObject::tr("3D orbit"));
    else if (m_camera.isPanning())
        emitStatus(QObject::tr("3D pan"));
}

void SimpleRenderer3D::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    if (m_camera.onMouseMove(x, y, buttons, viewW, viewH))
    {
        if (m_camera.isRotating())
            emitStatus(QObject::tr("3D orbiting"));
        else if (m_camera.isPanning())
            emitStatus(QObject::tr("3D panning"));
    }
}

void SimpleRenderer3D::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    m_camera.onMouseRelease(x, y, button, viewW, viewH);
    emitStatus(QObject::tr("3D ready"));
}

void SimpleRenderer3D::onWheel(int delta, int viewW, int viewH)
{
    m_camera.onWheel(delta, viewW, viewH);
    emitStatus(QObject::tr("3D zoom: %.1f").arg(m_camera.distance()));
}

// ========== 选择管理 ==========

void SimpleRenderer3D::selectNodeById(const QString& nodeId)
{
    m_selectedNodeId = nodeId;
    if (m_document)
    {
        m_document->selection().clear();
        if (auto entity = m_document->nodeById(nodeId.toStdString()))
            m_document->selection().add(entity);
    }
    rebuildTreeHighlight();

    if (m_selectionCallback)
        m_selectionCallback(nodeId);
    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QObject::tr("3D selected: %1").arg(nodeId)); // 3D 已选中: %1
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
            bestId = QString::fromStdString(node->id());
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

    const auto node = m_document->nodeById(m_selectedNodeId.toStdString());
    if (!node)
        return;

    const auto names = node->pathNamesRecursive();
    for (const auto& name : names)
        m_selectedPathNames.append(QString::fromStdString(name));

    if (m_pathCallback)
        m_pathCallback(m_selectedPathNames);
    if (m_statusCallback)
        m_statusCallback(QObject::tr("3D path: %1").arg(m_selectedPathNames.join(QObject::tr(" / "))));
}