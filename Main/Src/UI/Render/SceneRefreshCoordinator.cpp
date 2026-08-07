/**
 * @file SceneRefreshCoordinator.cpp
 * @brief 场景刷新协调器实现 — 四级刷新策略与增量渲染管线
 *
 * P5 刷新语义统一 (2026-08-02)
 */
#include "SceneRefreshCoordinator.h"
#include "RenderWidget.h"
#include "EntityToVertices.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/Layer/SyLayer.h"

#include "Log/SyLogger.h"
#include "Log/SyPerfCounter.h"

namespace
{
    // 场景更新节流时间（毫秒）— 16ms 约等于 60fps，合并短时间内的多次场景变更
    constexpr int kSceneUpdateDelay = 16;

    // P5 收口: RAII 帧计时器，自动管理 beginFrame/endFrame，消除 4 处重复的 null 检查 + 调用
    class ScopedFrameTimer
    {
    public:
        explicit ScopedFrameTimer(FrameTimer* timer) : m_timer(timer)
        {
            if (m_timer) m_timer->beginFrame();
        }
        ~ScopedFrameTimer()
        {
            if (m_timer) m_timer->endFrame();
        }
    private:
        FrameTimer* m_timer;
    };
}

SceneRefreshCoordinator::SceneRefreshCoordinator(QObject* parent)
    : QObject(parent)
{
    m_sceneUpdateTimer = new QTimer(this);
    m_sceneUpdateTimer->setSingleShot(true);
    m_sceneUpdateTimer->setInterval(kSceneUpdateDelay);
    connect(m_sceneUpdateTimer, &QTimer::timeout, this, &SceneRefreshCoordinator::updateSceneRender);
}

SceneRefreshCoordinator::~SceneRefreshCoordinator()
{
    stop();
}

void SceneRefreshCoordinator::setRenderWidget(RenderWidget* widget)
{
    m_renderWidget = widget;
}

void SceneRefreshCoordinator::setSceneManager(Eg::SceneManager* sm)
{
    // P5: 观察者注册收敛 — 切换 SceneManager 时自动注销旧观察者、注册新观察者
    if (m_sceneManager)
        m_sceneManager->removeObserver(this);
    m_sceneManager = sm;
    if (m_sceneManager)
        m_sceneManager->addObserver(this);
}

void SceneRefreshCoordinator::stop()
{
    if (m_sceneUpdateTimer)
        m_sceneUpdateTimer->stop();
    // P5: 观察者注销 — 在 stop() 中统一处理，避免析构时 SceneManager 已销毁（UAF）
    if (m_sceneManager)
    {
        m_sceneManager->removeObserver(this);
        m_sceneManager = nullptr;
    }
}

void SceneRefreshCoordinator::setPerfMonitorEnabled(bool enabled)
{
    if (enabled && !m_frameTimer)
    {
        m_frameTimer = std::make_unique<FrameTimer>();
        SY_INFO("[SceneRefreshCoordinator] 帧性能监控已启用");
    }
    else if (!enabled && m_frameTimer)
    {
        if (m_frameTimer->frameCount() > 0)
            m_frameTimer->report();
        m_frameTimer.reset();
        SY_INFO("[SceneRefreshCoordinator] 帧性能监控已禁用");
    }
}

// 场景更新节流：通过定时器合并短时间内的多次场景变更到一次 updateSceneRender() 调用
void SceneRefreshCoordinator::scheduleSceneUpdate()
{
    if (m_refreshLevel < RefreshLevel::LightUpdate)
        m_refreshLevel = RefreshLevel::LightUpdate;
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        m_sceneUpdateTimer->start();
}

void SceneRefreshCoordinator::requestRepaint()
{
    // 纯视觉刷新：仅调用 update()，不启动定时器，不触碰渲染数据
    if (m_refreshLevel < RefreshLevel::Repaint)
        m_refreshLevel = RefreshLevel::Repaint;
    if (m_renderWidget)
        m_renderWidget->update();
    SY_TRACE("[SceneRefreshCoordinator] requestRepaint: 纯视觉刷新");
}

void SceneRefreshCoordinator::scheduleFullRefresh()
{
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        m_sceneUpdateTimer->start();
    else if (!m_sceneUpdateTimer)
        updateSceneRender();
}

void SceneRefreshCoordinator::requestLightRefresh()
{
    // 增量刷新：通过定时器合并，收集脏 ID 后增量提交
    // 如果当前已经是 FullRefresh，不降级
    if (m_refreshLevel < RefreshLevel::LightUpdate)
        m_refreshLevel = RefreshLevel::LightUpdate;
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        m_sceneUpdateTimer->start();
    SY_TRACE("[SceneRefreshCoordinator] requestLightRefresh: 增量刷新");
}

void SceneRefreshCoordinator::requestFullRefresh()
{
    // 全量刷新：重建所有渲染数据
    m_refreshLevel = RefreshLevel::FullRefresh;
    scheduleFullRefresh();
    SY_INFO("[SceneRefreshCoordinator] requestFullRefresh: 全量刷新");
}

// 场景变更通知入口
// 通知链路：SceneNotifier::notifySceneChanged() → 此函数 → scheduleSceneUpdate() → updateSceneRender()
// 收集脏/删除图元 ID，升级到 LightUpdate 级别
void SceneRefreshCoordinator::onSceneChanged()
{
    if (m_sceneManager)
    {
        for (auto id : m_sceneManager->dirtyEntities())
            m_pendingDirtyIds.insert(id);
        for (auto id : m_sceneManager->deletedEntityIds())
            m_pendingDeletedIds.insert(id);
    }
    scheduleSceneUpdate();
    SY_TRACEF("[SceneRefreshCoordinator] onSceneChanged: dirty=%zu, deleted=%zu",
        m_pendingDirtyIds.size(), m_pendingDeletedIds.size());
}

void SceneRefreshCoordinator::onSelectionChanged()
{
    // P5: 观察者注册收敛 — 发射信号供视口同步工具状态，再触发纯视觉重绘
    emit selectionChanged();
    requestRepaint();
}

void SceneRefreshCoordinator::applyRepaintRefresh()
{
    if (m_renderWidget)
        m_renderWidget->update();
}

void SceneRefreshCoordinator::applyLightRefresh(Eg::SceneManager* sm)
{
    if (!m_renderWidget || !sm)
        return;

    for (auto id : m_pendingDeletedIds)
    {
        auto uid = static_cast<uint64_t>(id);
        m_renderWidget->removeRenderEntity(uid);
        m_renderedEntityIds.erase(uid);
    }

    for (auto id : m_pendingDirtyIds)
    {
        auto* entity = sm->findEntityById(id);
        if (!entity)
            continue;

        std::vector<render::VertexP3C3> vertices;
        render::PrimitiveType primType;
        if (!entityToVertices(entity, vertices, primType))
        {
            // 该图元无法走增量路径（如文本），标记需要回退到全量刷新
            m_pendingFullRefreshFallback = true;
            continue;
        }

        auto uid = static_cast<uint64_t>(id);
        if (m_renderedEntityIds.count(uid))
        {
            m_renderWidget->modifyRenderEntity(uid, vertices.data(), static_cast<uint32_t>(vertices.size()));
        }
        else
        {
            m_renderWidget->addRenderEntity(uid, vertices.data(), static_cast<uint32_t>(vertices.size()), primType);
            m_renderedEntityIds.insert(uid);
        }
    }

    // 遍历完成后，若存在不可增量图元（如文本），自动升级为全量刷新
    // 全量刷新会重建所有渲染数据，覆盖前面的增量更新，确保文本显示正确
    if (m_pendingFullRefreshFallback)
    {
        applyFullRefresh(sm);
        m_pendingFullRefreshFallback = false;
    }

    m_renderWidget->update();
}

void SceneRefreshCoordinator::applyFullRefresh(Eg::SceneManager* sm)
{
    if (!m_renderWidget || !sm)
        return;

    // 全量重建时清空曲线离散化缓存，避免持有已删除实体的旧数据
    clearEntityVertexCache();

    m_renderWidget->submitSceneFromDataSource(sm);
    m_renderedEntityIds.clear();
    auto allEntities = sm->getAllEntities();
    for (auto* e : allEntities)
    {
        if (e && e->visible() && (!e->layer() || e->layer()->isVisible()))
            m_renderedEntityIds.insert(static_cast<uint64_t>(e->id));
    }
}

// 渲染刷新分发：按 RefreshLevel 级别选择刷新策略
void SceneRefreshCoordinator::updateSceneRender()
{
    if (!m_renderWidget || m_refreshLevel == RefreshLevel::None)
        return;

    if (!m_renderWidget->isInitialized())
    {
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
            m_sceneUpdateTimer->start();
        return;
    }

    // P5 收口: RAII 帧计时器，自动管理 beginFrame/endFrame，消除散落的手动调用
    ScopedFrameTimer scopedTimer(m_frameTimer.get());

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    auto* sm = m_sceneManager;
    if (!sm)
        return;

    if (level == RefreshLevel::Repaint)
    {
        applyRepaintRefresh();
        return;
    }

    // LightUpdate 增量刷新：applyLightRefresh 内部遇不可增量图元（如文本）
    // 会通过 m_pendingFullRefreshFallback 自动回退到 applyFullRefresh，
    // 因此此处无需对 LightUpdate 再做全量处理
    if (level == RefreshLevel::LightUpdate)
        applyLightRefresh(sm);

    // 仅显式 FullRefresh 级别走此分支，避免与 LightUpdate 的内部回退重复执行
    if (level >= RefreshLevel::FullRefresh)
        applyFullRefresh(sm);

    sm->markClean();
    m_pendingDirtyIds.clear();
    m_pendingDeletedIds.clear();
}