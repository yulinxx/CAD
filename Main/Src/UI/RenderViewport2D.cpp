/**
 * @file RenderViewport2D.cpp
 * @brief 基于 Renderx 的 2D 渲染视口实现
 */
#include "RenderViewport2D.h"
#include "RenderWidget.h"
#include "SceneDocument2D.h"
#include "ISelectionService.h"
#include "UiInteractionDispatcher.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine/Scene/SceneRenderContract.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    // 命中测试容差（像素）
    constexpr double kHitTolerance = 8.0;
    // 网格步长
    constexpr double kGridStep = 50.0;
    // 场景更新节流时间（毫秒）
    constexpr int kSceneUpdateDelay = 0;
    // 默认视图范围：中心 (0,0)，半宽半高 500，即可见范围 (-500,-500)~(500,500)
    constexpr float kDefaultViewHalfW = 500.0f;
    constexpr float kDefaultViewHalfH = 500.0f;

    // 点到线段的距离
    double distPointToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
    {
        double abx = b.x() - a.x();
        double aby = b.y() - a.y();
        double apx = p.x() - a.x();
        double apy = p.y() - a.y();

        double lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-12)
            return std::hypot(apx, apy);

        double t = (apx * abx + apy * aby) / lenSq;
        t = std::max(0.0, std::min(1.0, t));

        double closestX = a.x() + t * abx;
        double closestY = a.y() + t * aby;

        return std::hypot(p.x() - closestX, p.y() - closestY);
    }

    // 将 Ut::Mat3f / Render::Mat3f 格式转换为 9 个 float 数组
    void mat3ToArray(const Render::Mat3f& mat, float out[9])
    {
        for (int i = 0; i < 9; ++i)
            out[i] = mat.data[i];
    }
}

// ==================== Camera2D 实现 ====================

void Camera2D::computeViewMatrix(float outMat[9], float vpW, float vpH) const
{
    if (vpW <= 0 || vpH <= 0)
    {
        for (int i = 0; i < 9; ++i)
            outMat[i] = (i == 0 || i == 4 || i == 8) ? 1.0f : 0.0f;
        SY_WARNF("Camera2D::computeViewMatrix: invalid viewport %.2fx%.2f, returning identity", vpW, vpH);
        return;
    }

    // 稳定、明确的 2D 正交映射：
    // world -> NDC
    // x_ndc =  2 * zoom * (x - centerX) / vpW
    // y_ndc = -2 * zoom * (y - centerY) / vpH
    const float scaleX = 2.0f * zoom / vpW;
    const float scaleY = 2.0f * zoom / vpH;
    const float centerX = -panOffset.x();
    const float centerY = -panOffset.y();

    Render::Mat3f view = Render::Mat3f::identity();
    view.at(0, 0) = scaleX;
    view.at(1, 0) = 0.0f;
    view.at(2, 0) = -centerX * scaleX;
    view.at(0, 1) = 0.0f;
    view.at(1, 1) = -scaleY;
    view.at(2, 1) = centerY * scaleY;
    view.at(0, 2) = 0.0f;
    view.at(1, 2) = 0.0f;
    view.at(2, 2) = 1.0f;

    mat3ToArray(view, outMat);
}

QPointF Camera2D::screenToWorld(const QPoint& screenPos, float vpW, float vpH) const
{
    if (vpW <= 0 || vpH <= 0)
        return QPointF(0, 0);

    float nx = (2.0f * screenPos.x() - vpW) / vpW;
    float ny = (2.0f * screenPos.y() - vpH) / vpH;

    if (zoom < 1e-6f)
        return QPointF(nx, ny);

    float scaleX = 2.0f * zoom / vpW;
    float scaleY = 2.0f * zoom / vpH;

    float wx = (nx / scaleX) - panOffset.x();
    float wy = -(ny / scaleY) - panOffset.y();

    return QPointF(wx, wy);
}

void Camera2D::zoomIn(float factor, const QPointF& anchorWorld, float vpW, float vpH)
{
    float newZoom = zoom * factor;
    if (newZoom > MAX_ZOOM)
        newZoom = MAX_ZOOM;
    if (newZoom < MIN_ZOOM)
        newZoom = MIN_ZOOM;

    // 以鼠标位置为锚点缩放
    float ratio = newZoom / zoom;
    panOffset.setX(panOffset.x() + anchorWorld.x() * (1.0f - ratio));
    panOffset.setY(panOffset.y() + anchorWorld.y() * (1.0f - ratio));

    zoom = newZoom;
}

void Camera2D::zoomOut(float factor, const QPointF& anchorWorld, float vpW, float vpH)
{
    zoomIn(1.0f / factor, anchorWorld, vpW, vpH);
}

void Camera2D::pan(float dx, float dy)
{
    panOffset.setX(panOffset.x() + dx);
    panOffset.setY(panOffset.y() + dy);
}

void Camera2D::reset()
{
    zoom = 1.0f;
    panOffset = QPointF(0, 0);
}

void Camera2D::zoomToFit(float vpW, float vpH, float sceneW, float sceneH)
{
    if (sceneW <= 0 || sceneH <= 0 || vpW <= 0 || vpH <= 0)
        return;

    float scaleX = vpW / sceneW;
    float scaleY = vpH / sceneH;
    zoom = std::min(scaleX, scaleY) * 0.9f; // 留 10% 边距

    if (zoom < MIN_ZOOM) zoom = MIN_ZOOM;
    if (zoom > MAX_ZOOM) zoom = MAX_ZOOM;

    panOffset = QPointF(0, 0);
}

void Camera2D::setViewExtent(float vpW, float vpH, float centerX, float centerY, float halfW, float halfH)
{
    if (halfW <= 0 || halfH <= 0)
        return;

    // screenToWorld 中可见世界宽度 = 2/zoom
    // 要让可见范围 >= 2*halfW 且 >= 2*halfH，取 zoom = 1/max(halfW, halfH)
    zoom = 1.0f / std::max(halfW, halfH);

    if (zoom < MIN_ZOOM) zoom = MIN_ZOOM;
    if (zoom > MAX_ZOOM) zoom = MAX_ZOOM;

    // panOffset 使 centerX/Y 位于视口中心
    // screenToWorld: wx = nx/zoom - panOffset.x，视口中心 nx=0 时 wx=centerX
    // 所以 panOffset.x = -centerX, panOffset.y = -centerY
    panOffset = QPointF(-centerX, -centerY);
}

// ==================== RenderViewport2D 实现 ====================

RenderViewport2D::RenderViewport2D(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    initRenderWidget();
    initTimers();
}

RenderViewport2D::~RenderViewport2D()
{
    *m_alive = false;
    // m_sceneManager 可能已是悬垂指针（SceneManager 先于本对象析构），
    // 因此不在此处反注册回调，改为在 Callback 中通过 m_alive 标志防范。
}

void RenderViewport2D::initRenderWidget()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_renderWidget = new RenderWidget(this);
    m_renderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_renderWidget->setMouseTracking(true);
    // RenderWidget 是 QOpenGLWidget 原生窗口，鼠标事件不会冒泡到父控件，
    // 通过事件过滤器将鼠标事件从 RenderWidget 转发到 RenderViewport2D 处理
    m_renderWidget->installEventFilter(this);
    layout->addWidget(m_renderWidget);
}

void RenderViewport2D::initTimers()
{
    m_sceneUpdateTimer = new QTimer(this);
    m_sceneUpdateTimer->setSingleShot(true);
    m_sceneUpdateTimer->setInterval(kSceneUpdateDelay);
    connect(m_sceneUpdateTimer, &QTimer::timeout, this, &RenderViewport2D::updateSceneRender);
}

Eg::SceneManager* RenderViewport2D::sceneManager() const
{
    return m_sceneManager;
}

// ==================== 外部接口实现 ====================

void RenderViewport2D::setStatusCallback(std::function<void(const QString&)>&& callback)
{
    m_statusCallback = std::move(callback);
}

void RenderViewport2D::setSelectionCallback(std::function<void(const QString&, const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
}

void RenderViewport2D::setCommandStageCallback(std::function<void(const QString&)>&& callback)
{
    m_commandStageCallback = std::move(callback);
}

void RenderViewport2D::setPositionCallback(std::function<void(double, double)>&& callback)
{
    m_positionCallback = std::move(callback);
}

void RenderViewport2D::setDocument(SceneDocument2D* document)
{
    // 断开旧文档的观察者注册
    if (m_sceneManager)
    {
        m_sceneManager->notifier().removeObserver(this);
        m_sceneManager = nullptr;
    }

    m_document = document;

    // 缓存 SceneManager 指针，避免析构时通过 m_document 访问已释放内存
    if (m_document)
        m_sceneManager = m_document->sceneManager();

    // 注册为新文档的观察者（同时接收场景变更和选择变更通知）
    if (m_sceneManager)
        m_sceneManager->notifier().addObserver(this);

    // 初始刷新
    scheduleSceneUpdate();
}

void RenderViewport2D::setSelectionService(ISelectionService* service)
{
    m_selectionService = service;
}

void RenderViewport2D::setInteractionDispatcher(IInteractionDispatcher* dispatcher)
{
    m_interactionDispatcher = dispatcher;
}

void RenderViewport2D::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void RenderViewport2D::resetView()
{
    // 重置到默认视图范围：中心 (0,0)，可见 (-500,-500)~(500,500)
    float dpr = static_cast<float>(m_renderWidget ? m_renderWidget->devicePixelRatioF() : 1.0);
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height()) * dpr;
    if (vpW > 0 && vpH > 0)
        m_camera.setViewExtent(vpW, vpH, 0.0f, 0.0f, kDefaultViewHalfW, kDefaultViewHalfH);
    else
        m_camera.reset();
    updateViewMatrix();
    updateStatus(tr("2D view reset"));
}

void RenderViewport2D::zoomToFit()
{
    if (!m_document || !sceneManager())
    {
        resetView();
        return;
    }

    auto* sm = sceneManager();
    auto bbox = sm->sceneBBox2D();

    if (!bbox.isValid())
    {
        resetView();
        return;
    }

    float sceneW = static_cast<float>(bbox.maxPt.x() - bbox.minPt.x());
    float sceneH = static_cast<float>(bbox.maxPt.y() - bbox.minPt.y());
    const float centerX = static_cast<float>((bbox.minPt.x() + bbox.maxPt.x()) / 2.0);
    const float centerY = static_cast<float>((bbox.minPt.y() + bbox.maxPt.y()) / 2.0);

    if (sceneW <= 0 || sceneH <= 0)
    {
        sceneW = 1000.0f;
        sceneH = 1000.0f;
    }

    float dpr = static_cast<float>(m_renderWidget ? m_renderWidget->devicePixelRatioF() : 1.0);
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height()) * dpr;

    m_camera.zoomToFit(vpW, vpH, sceneW, sceneH);
    m_camera.pan(-centerX, -centerY);

    updateViewMatrix();
    updateStatus(tr("2D zoom extents"));
}

void RenderViewport2D::zoomToSelection()
{
    if (!m_selectionService)
    {
        updateStatus(tr("No selection service"));
        return;
    }

    auto selected = m_selectionService->selectedEntities();
    if (selected.empty())
    {
        updateStatus(tr("No entities selected"));
        return;
    }

    // 计算所有选中实体的合并包围盒
    Ut::BBox2d combinedBbox;
    for (auto* entity : selected)
    {
        if (!entity)
            continue;
        Ut::BBox2d bbox = entity->getBbox();
        if (bbox.isValid())
            combinedBbox.expand(bbox);
    }

    if (!combinedBbox.isValid())
    {
        updateStatus(tr("Invalid selection bounds"));
        return;
    }

    float sceneW = static_cast<float>(combinedBbox.maxPt.x() - combinedBbox.minPt.x());
    float sceneH = static_cast<float>(combinedBbox.maxPt.y() - combinedBbox.minPt.y());

    // 如果包围盒太小（小于1个单位），使用默认大小
    if (sceneW <= 0 || sceneH <= 0)
    {
        sceneW = 100.0f;
        sceneH = 100.0f;
    }

    float dpr = static_cast<float>(m_renderWidget ? m_renderWidget->devicePixelRatioF() : 1.0);
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height()) * dpr;

    m_camera.zoomToFit(vpW, vpH, sceneW, sceneH);

    float centerX = static_cast<float>((combinedBbox.minPt.x() + combinedBbox.maxPt.x()) / 2.0);
    float centerY = static_cast<float>((combinedBbox.minPt.y() + combinedBbox.maxPt.y()) / 2.0);
    m_camera.pan(-centerX, -centerY);

    updateViewMatrix();
    updateStatus(tr("2D zoom to selection"));
}

void RenderViewport2D::zoomIn()
{
    // 以视口中心为锚点放大
    if (!m_renderWidget)
        return;
    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;
    if (vpW <= 0 || vpH <= 0)
        return;
    QPointF center(vpW / 2.0f, vpH / 2.0f);
    QPointF worldCenter = m_camera.screenToWorld(QPoint(static_cast<int>(center.x() / dpr), static_cast<int>(center.y() / dpr)), vpW, vpH);
    m_camera.zoomIn(1.25f, worldCenter, vpW, vpH);
    updateViewMatrix();
}

void RenderViewport2D::zoomOut()
{
    // 以视口中心为锚点缩小
    if (!m_renderWidget)
        return;
    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;
    if (vpW <= 0 || vpH <= 0)
        return;
    QPointF center(vpW / 2.0f, vpH / 2.0f);
    QPointF worldCenter = m_camera.screenToWorld(QPoint(static_cast<int>(center.x() / dpr), static_cast<int>(center.y() / dpr)), vpW, vpH);
    m_camera.zoomOut(1.25f, worldCenter, vpW, vpH);
    updateViewMatrix();
}

void RenderViewport2D::setPanModeEnabled(bool enabled)
{
    m_panModeEnabled = enabled;
    updateStatus(enabled ? tr("2D pan mode") : tr("2D select mode"));
}

bool RenderViewport2D::isPanModeEnabled() const
{
    return m_panModeEnabled;
}

void RenderViewport2D::setDrawingEnabled(bool enabled)
{
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D draw mode") : tr("2D select mode"));
}

void RenderViewport2D::setMeasureMode(bool enabled)
{
    if (m_commandStageCallback)
        m_commandStageCallback(enabled ? tr("Waiting for first point") : tr("Idle"));
    updateStatus(enabled ? tr("2D measure mode") : tr("2D select mode"));
}

QString RenderViewport2D::selectedEntityId() const
{
    if (!m_document || !m_selectionService)
        return {};
    auto ids = m_selectionService->selectedIds();
    return ids.empty() ? QString() : QString::fromStdString(ids[0]);
}

void RenderViewport2D::deleteSelectedEntity()
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;

    // 只走 OperationBus 路径（经 SceneEditService → UndoRedoManager，确保可撤销）
    // 移除旧版 m_document->removeEntity() 直写，避免双重删除
    if (m_operationBus)
        m_operationBus->run(OperationId::Edit_Delete, {}, OperationSource::DrawTool);

    clearSelection();
    updateStatus(tr("2D entity deleted"));
    scheduleSceneUpdate();
}

void RenderViewport2D::nudgeSelectedEndpoint(const QPointF& delta)
{
    if (!m_document)
        return;
    auto selectedId = selectedEntityId();
    if (selectedId.isEmpty())
        return;
    auto* sm = sceneManager();
    if (!sm) return;
    bool ok = false;
    auto* entity = sm->findEntityById(static_cast<Eg::EntityId>(selectedId.toULongLong(&ok)));
    if (!ok || !entity || entity->eType != Eg::EType::LINE)
        return;
    auto* line = static_cast<Eg::SyLine*>(entity);
    for (auto& pt : line->vPoints)
    {
        pt.x() += delta.x();
        pt.y() += delta.y();
    }
    sm->clearSelection();
    sm->selectEntity(line);
    syncSelectionDetails();
    updateStatus(tr("2D endpoint moved"));
    scheduleSceneUpdate();
}

void RenderViewport2D::selectEntityById(const QString& entityId)
{
    if (!m_document)
        return;
    if (m_selectionService)
    {
        m_selectionService->clear();
        m_selectionService->select(entityId.toStdString());
    }
    syncSelectionDetails();
    updateStatus(tr("2D entity selected"));
    if (m_selectionCallback)
        m_selectionCallback(tr("2D-Select"), tr("2D entity: %1").arg(entityId));
    requestRepaint();
}

void RenderViewport2D::syncSelectionDetails()
{
    // 同步选择详情到属性面板等（由外部回调处理）
}

void RenderViewport2D::clearSelection()
{
    if (m_selectionService)
        m_selectionService->clear();
    requestRepaint();
}

QPointF RenderViewport2D::mapToScene(const QPoint& screenPos) const
{
    float dpr = static_cast<float>(m_renderWidget ? m_renderWidget->devicePixelRatioF() : 1.0);
    float vpW = static_cast<float>(m_renderWidget ? m_renderWidget->width() : width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget ? m_renderWidget->height() : height()) * dpr;

    // 将相对于 RenderViewport2D 的坐标转换为相对于 m_renderWidget 的坐标
    QPoint widgetPos = screenPos;
    if (m_renderWidget)
        widgetPos = m_renderWidget->mapFromParent(screenPos);

    return m_camera.screenToWorld(widgetPos, vpW, vpH);
}

// ==================== 事件处理 ====================

void RenderViewport2D::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // 首次显示时设置默认视图范围：中心 (0,0)，可见 (-500,-500)~(500,500)
    QTimer::singleShot(0, this, [this]() {
        resetView();
    });
}

void RenderViewport2D::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateViewMatrix();
}

bool RenderViewport2D::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_renderWidget || !m_renderWidget)
        return QWidget::eventFilter(obj, event);

    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    {
        // 将 RenderWidget 坐标系的鼠标事件转发到本视口处理
        // RenderWidget 是 QOpenGLWidget 原生窗口，事件不会自动冒泡到父控件
        auto* me = static_cast<QMouseEvent*>(event);
        QPoint parentPos = m_renderWidget->mapToParent(me->pos());
        QMouseEvent parentEvent(me->type(), parentPos, me->globalPos(),
            me->button(), me->buttons(), me->modifiers());

        switch (event->type())
        {
        case QEvent::MouseButtonPress:
            mousePressEvent(&parentEvent);
            break;
        case QEvent::MouseMove:
            mouseMoveEvent(&parentEvent);
            break;
        case QEvent::MouseButtonRelease:
            mouseReleaseEvent(&parentEvent);
            break;
        default:
            break;
        }
        return true; // 事件已处理，不再传播
    }
    case QEvent::Wheel:
    {
        auto* we = static_cast<QWheelEvent*>(event);
        wheelEvent(we);
        return true;
    }
    case QEvent::KeyPress:
    {
        auto* ke = static_cast<QKeyEvent*>(event);
        keyPressEvent(ke);
        return true;
    }
    case QEvent::ContextMenu:
    {
        auto* ce = static_cast<QContextMenuEvent*>(event);
        contextMenuEvent(ce);
        return true;
    }
    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}

void RenderViewport2D::mousePressEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (m_panModeEnabled)
        {
            m_panning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
        else if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            m_interactionDispatcher->forwardMouseDown(
                static_cast<int>(worldPos.x()),
                static_cast<int>(worldPos.y()));
        }
        else
        {
            beginBoxSelect(worldPos);
        }
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void RenderViewport2D::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    // 始终更新鼠标位置到状态栏（无论后续如何处理）
    if (m_positionCallback)
        m_positionCallback(worldPos.x(), worldPos.y());

    if (m_panning)
    {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        float worldDx = -static_cast<float>(delta.x()) / m_camera.zoom;
        float worldDy = static_cast<float>(delta.y()) / m_camera.zoom;

        m_camera.pan(worldDx, worldDy);
        updateViewMatrix();
        event->accept();
        return;
    }

    if (m_boxSelecting)
    {
        updateBoxSelect(worldPos);
        event->accept();
        return;
    }

    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        m_interactionDispatcher->forwardMouseMove(
            static_cast<int>(worldPos.x()),
            static_cast<int>(worldPos.y()));
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void RenderViewport2D::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->pos());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        if (m_panning)
        {
            m_panning = false;
            unsetCursor();
            event->accept();
            return;
        }

        if (m_boxSelecting)
        {
            endBoxSelect(worldPos);
            event->accept();
            return;
        }

        if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
        {
            m_interactionDispatcher->forwardMouseUp(
                static_cast<int>(worldPos.x()),
                static_cast<int>(worldPos.y()));
            event->accept();
            return;
        }

        handleLeftClick(worldPos);
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void RenderViewport2D::wheelEvent(QWheelEvent* event)
{
    if (!m_renderWidget)
    {
        QWidget::wheelEvent(event);
        return;
    }

    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;

    if (vpW <= 0 || vpH <= 0)
        return;

    QPoint widgetPos = m_renderWidget->mapFromParent(event->position().toPoint());
    QPointF worldPos = m_camera.screenToWorld(widgetPos, vpW, vpH);

    float factor = (event->angleDelta().y() > 0) ? 1.1f : 0.9f;
    m_camera.zoomIn(factor, worldPos, vpW, vpH);
    updateViewMatrix();
    event->accept();
}

void RenderViewport2D::keyPressEvent(QKeyEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        if (event->key() == Qt::Key_Escape)
        {
            m_interactionDispatcher->cancel();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            m_interactionDispatcher->submit();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Delete)
    {
        deleteSelectedEntity();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RenderViewport2D::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_interactionDispatcher && m_interactionDispatcher->hasActiveCommand())
    {
        m_interactionDispatcher->cancel();
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

// ==================== 内部方法 ====================

void RenderViewport2D::updateViewMatrix()
{
    if (!m_renderWidget)
        return;

    float dpr = static_cast<float>(m_renderWidget->devicePixelRatioF());
    float vpW = static_cast<float>(m_renderWidget->width()) * dpr;
    float vpH = static_cast<float>(m_renderWidget->height()) * dpr;

    if (vpW <= 0 || vpH <= 0)
        return;

    float viewMat[9];
    m_camera.computeViewMatrix(viewMat, vpW, vpH);

    Render::Mat3f mat;
    for (int i = 0; i < 9; ++i)
        mat.data[i] = viewMat[i];

    // 即使渲染设备尚未初始化，也先缓存视图矩阵；initializeGL 后会补发一次
    m_renderWidget->setViewMatrix(mat);
}

void RenderViewport2D::scheduleSceneUpdate()
{
    if (m_refreshLevel < RefreshLevel::LightUpdate)
        m_refreshLevel = RefreshLevel::LightUpdate;
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        m_sceneUpdateTimer->start();
}

void RenderViewport2D::requestRepaint()
{
    // 仅重绘，不触发全量 gather（适用于选择变化等仅需视觉刷新的场景）
    if (m_refreshLevel < RefreshLevel::Repaint)
        m_refreshLevel = RefreshLevel::Repaint;
    if (m_renderWidget)
        m_renderWidget->update();
}

void RenderViewport2D::requestSceneRefresh()
{
    m_refreshLevel = RefreshLevel::FullRefresh;
    if (m_sceneUpdateTimer)
    {
        if (!m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
    }
    else
    {
        updateSceneRender();
    }
}

void RenderViewport2D::requestFullRefresh()
{
    // 强制全量 gather + submit（导入、大批量修改后）
    m_refreshLevel = RefreshLevel::FullRefresh;
    if (m_sceneUpdateTimer)
    {
        if (!m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
    }
    else
    {
        updateSceneRender();
    }
}

void RenderViewport2D::onSceneChanged()
{
    // 记录脏实体 ID，为未来增量渲染提供输入
    if (auto* sm = sceneManager())
    {
        for (auto id : sm->dirtyEntities())
            m_pendingDirtyIds.insert(id);
        for (auto id : sm->deletedEntityIds())
            m_pendingDeletedIds.insert(id);
    }
    scheduleSceneUpdate();
}

void RenderViewport2D::onSelectionChanged()
{
    // 选择变更仅需重绘，无需全量 gather
    requestRepaint();
}

void RenderViewport2D::updateSceneRender()
{
    if (!m_renderWidget || m_refreshLevel == RefreshLevel::None)
        return;

    qInfo() << "RenderViewport2D::updateSceneRender: refreshLevel=" << static_cast<int>(m_refreshLevel)
            << "initialized=" << m_renderWidget->isInitialized();

    // GL 尚未初始化时保留刷新标记，延迟重试
    if (!m_renderWidget->isInitialized())
    {
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
        return;
    }

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    auto* sm = sceneManager();
    if (!sm)
        return;

    updateViewMatrix();

    if (level == RefreshLevel::Repaint)
    {
        // 纯视觉重绘（选择变化、叠加层更新），不触碰渲染数据
        m_renderWidget->update();
        return;
    }

    if (level == RefreshLevel::LightUpdate && !m_pendingDeletedIds.empty())
    {
        // 增量删除路径 — 先移除已删除的实体
        // TODO: 当 RenderWidget 支持 removeEntity() 时，逐个移除
        // 目前 fallback 到全量 gather
    }

    if (level == RefreshLevel::LightUpdate && !m_pendingDirtyIds.empty())
    {
        // 增量更新路径 — 对脏实体逐个更新
        // TODO: 当 RenderWidget 支持 updateEntity()/addEntity() 时，逐个增/改
        // 目前 fallback 到全量 gather
    }

    if (level >= RefreshLevel::LightUpdate)
    {
        // 全量 gather + submit（兜底路径，也是当前唯一实现）
        m_renderWidget->submitSceneFromDataSource(sm);
    }

    // 清理已处理的脏实体标记
    sm->markClean();
    m_pendingDirtyIds.clear();
    m_pendingDeletedIds.clear();
}

void RenderViewport2D::handleLeftClick(const QPointF& worldPos)
{
    // 简单的点击选择
    performHitTest(worldPos);
}

void RenderViewport2D::performHitTest(const QPointF& worldPos)
{
    if (!m_document || !sceneManager() || !m_selectionService)
        return;

    auto* sm = sceneManager();
    double tol = kHitTolerance / m_camera.zoom; // 转换为世界坐标容差

    std::string hitId;
    double minDist = tol;

    for (const auto* entity : sm->getAllEntities())
    {
        if (!entity) continue;

        double dist = std::numeric_limits<double>::max();

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            if (line->vPoints.size() >= 2)
            {
                for (size_t i = 1; i < line->vPoints.size(); ++i)
                {
                    const auto& p0 = line->vPoints[i - 1];
                    const auto& p1 = line->vPoints[i];
                    double d = distPointToSegment(worldPos,
                        QPointF(p0.x(), p0.y()),
                        QPointF(p1.x(), p1.y()));
                    if (d < dist) dist = d;
                }
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE)
        {
            auto* circle = static_cast<const Eg::SyCircle*>(entity);
            const auto& c = entity->basePoint;
            double d = std::abs(std::hypot(worldPos.x() - c.x(), worldPos.y() - c.y()) - circle->dRadius);
            if (d < dist) dist = d;
        }
        else if (entity->eType == Eg::EType::ARC)
        {
            auto* arc = static_cast<const Eg::SyArc*>(entity);
            const auto& c = entity->basePoint;
            double d = std::abs(std::hypot(worldPos.x() - c.x(), worldPos.y() - c.y()) - arc->dRadius);
            if (d < dist) dist = d;
        }

        if (dist < minDist)
        {
            minDist = dist;
            hitId = std::to_string(entity->id);
        }
    }

    m_selectionService->clear();
    if (!hitId.empty())
    {
        m_selectionService->select(hitId);
        updateStatus(tr("2D entity selected"));
        if (m_selectionCallback)
            m_selectionCallback(tr("2D-Select"), tr("2D entity: %1").arg(QString::fromStdString(hitId)));
    }
    else
    {
        updateStatus(tr("2D selection cleared"));
    }

    scheduleSceneUpdate();
}

void RenderViewport2D::beginBoxSelect(const QPointF& worldPos)
{
    m_boxSelecting = true;
    m_boxSelectStart = worldPos;
    m_boxSelectEnd = worldPos;

    // 设置选择框
    if (m_renderWidget)
    {
        Render::BBox2d bbox(
            worldPos.x(), worldPos.y(),
            worldPos.x(), worldPos.y());
        m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
    }
}

void RenderViewport2D::updateBoxSelect(const QPointF& worldPos)
{
    m_boxSelectEnd = worldPos;

    if (m_renderWidget)
    {
        Render::BBox2d bbox(
            m_boxSelectStart.x(), m_boxSelectStart.y(),
            worldPos.x(), worldPos.y());
        m_renderWidget->setSelectionBox(&bbox, QColor(204, 102, 0, 200));
    }
}

void RenderViewport2D::endBoxSelect(const QPointF& worldPos)
{
    m_boxSelecting = false;

    // 清除选择框
    if (m_renderWidget)
    {
        m_renderWidget->setSelectionBox(nullptr, QColor());
    }

    // 如果起点和终点几乎相同，视为点击
    double dx = std::abs(worldPos.x() - m_boxSelectStart.x());
    double dy = std::abs(worldPos.y() - m_boxSelectStart.y());
    double tol = kHitTolerance / m_camera.zoom;

    if (dx < tol && dy < tol)
    {
        handleLeftClick(worldPos);
        return;
    }

    // 框选
    if (!m_document || !sceneManager() || !m_selectionService)
        return;

    auto* sm = sceneManager();
    double minX = std::min(m_boxSelectStart.x(), worldPos.x());
    double maxX = std::max(m_boxSelectStart.x(), worldPos.x());
    double minY = std::min(m_boxSelectStart.y(), worldPos.y());
    double maxY = std::max(m_boxSelectStart.y(), worldPos.y());

    std::vector<std::string> hitIds;

    for (const auto* entity : sm->getAllEntities())
    {
        if (!entity) continue;

        bool inside = false;

        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<const Eg::SyLine*>(entity);
            for (const auto& pt : line->vPoints)
            {
                if (pt.x() >= minX && pt.x() <= maxX &&
                    pt.y() >= minY && pt.y() <= maxY)
                {
                    inside = true;
                    break;
                }
            }
        }
        else if (entity->eType == Eg::EType::CIRCLE || entity->eType == Eg::EType::ARC)
        {
            const auto& c = entity->basePoint;
            if (c.x() >= minX && c.x() <= maxX &&
                c.y() >= minY && c.y() <= maxY)
            {
                inside = true;
            }
        }
        else if (entity->eType == Eg::EType::POLYGON)
        {
            auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
            for (const auto& v : polygon->vertices())
            {
                if (v.x() >= minX && v.x() <= maxX &&
                    v.y() >= minY && v.y() <= maxY)
                {
                    inside = true;
                    break;
                }
            }
        }

        if (inside)
            hitIds.push_back(std::to_string(entity->id));
    }

    m_selectionService->clear();
    if (!hitIds.empty())
    {
        std::vector<std::string> ids;
        for (const auto& id : hitIds)
            ids.push_back(id);
        m_selectionService->selectMultiple(ids);
        updateStatus(tr("2D %n entities selected", "", static_cast<int>(hitIds.size())));
    }
    else
    {
        updateStatus(tr("2D selection cleared"));
    }

    scheduleSceneUpdate();
}

void RenderViewport2D::updateStatus(const QString& text)
{
    if (m_statusCallback)
        m_statusCallback(text);
}