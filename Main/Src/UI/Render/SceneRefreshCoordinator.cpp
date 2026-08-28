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
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine/SyEntity/EType.h"

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
        explicit ScopedFrameTimer(FrameTimer* timer)
            : m_timer(timer)
        {
            if (m_timer)
            {
                m_timer->beginFrame();
            }
        }

        ~ScopedFrameTimer()
        {
            if (m_timer)
            {
                m_timer->endFrame();
            }
        }

    private:
        FrameTimer* m_timer;
    };
}  // namespace

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
    {
        m_sceneManager->removeObserver(this);
    }
    m_sceneManager = sm;
    if (m_sceneManager)
    {
        m_sceneManager->addObserver(this);
    }
}

void SceneRefreshCoordinator::stop()
{
    if (m_sceneUpdateTimer)
    {
        m_sceneUpdateTimer->stop();
    }
    // P5: 观察者注销 — 在 stop() 中统一处理，避免析构时 SceneManager 已销毁（UAF）
    if (m_sceneManager)
    {
        m_sceneManager->removeObserver(this);
        m_sceneManager = nullptr;
    }
}

void SceneRefreshCoordinator::markEntityDirty(uint64_t entityId)
{
    // 只补精度，不动级别：级别由 request* / onSceneChanged 决定
    m_pendingDirtyIds.insert(static_cast<Eg::EntityId>(entityId));
}

void SceneRefreshCoordinator::markEntityDeleted(uint64_t entityId)
{
    const auto id = static_cast<Eg::EntityId>(entityId);
    m_pendingDirtyIds.erase(id);
    m_pendingDeletedIds.insert(id);
}

UI::SceneRefreshLevel SceneRefreshCoordinator::pendingLevel() const
{
    return m_refreshLevel;
}

void SceneRefreshCoordinator::flushPendingRefresh()
{
    // 跳过节流立刻派发：定时器仍要停掉，否则待办已清空还会空跑一次
    if (m_sceneUpdateTimer)
    {
        m_sceneUpdateTimer->stop();
    }
    updateSceneRender();
}

void SceneRefreshCoordinator::setPerfMonitorEnabled(bool enabled)

{
    if (enabled && !m_frameTimer)
    {
        m_frameTimer = std::make_unique<FrameTimer>();
        SY_INFO("[SceneRefreshCoordinator] Frame performance monitoring enabled");
    }
    else if (!enabled && m_frameTimer)
    {
        if (m_frameTimer->frameCount() > 0)
        {
            m_frameTimer->report();
        }
        m_frameTimer.reset();
        SY_INFO("[SceneRefreshCoordinator] Frame performance monitoring disabled");
    }
}

// 场景更新节流：通过定时器合并短时间内的多次场景变更到一次 updateSceneRender() 调用
void SceneRefreshCoordinator::scheduleSceneUpdate()
{
    if (m_refreshLevel < RefreshLevel::LightUpdate)
    {
        m_refreshLevel = RefreshLevel::LightUpdate;
    }
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
    {
        m_sceneUpdateTimer->start();
    }
}

void SceneRefreshCoordinator::requestRepaint()
{
    // 纯视觉刷新：仅调用 update()，不启动定时器，不触碰渲染数据
    if (m_refreshLevel < RefreshLevel::Repaint)
    {
        m_refreshLevel = RefreshLevel::Repaint;
    }
    if (m_renderWidget)
    {
        m_renderWidget->update();
    }
}

void SceneRefreshCoordinator::scheduleFullRefresh()
{
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
    {
        m_sceneUpdateTimer->start();
    }
    else if (!m_sceneUpdateTimer)
    {
        updateSceneRender();
    }
}

void SceneRefreshCoordinator::requestLightRefresh()
{
    // 增量刷新：通过定时器合并，收集脏 ID 后增量提交
    // 如果当前已经是 FullRefresh，不降级
    if (m_refreshLevel < RefreshLevel::LightUpdate)
    {
        m_refreshLevel = RefreshLevel::LightUpdate;
    }
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
    {
        m_sceneUpdateTimer->start();
    }
}

void SceneRefreshCoordinator::requestFullRefresh()
{
    // 全量刷新：重建所有渲染数据
    m_refreshLevel = RefreshLevel::FullRefresh;
    scheduleFullRefresh();
}

// 场景变更通知入口
// 通知链路：SceneNotifier::notifySceneChanged() → 此函数 → scheduleSceneUpdate() → updateSceneRender()
// 收集脏/删除图元 ID，升级到 LightUpdate 级别
void SceneRefreshCoordinator::onSceneChanged()
{
    if (m_sceneManager)
    {
        const auto dirtyIds = m_sceneManager->dirtyEntities();
        const auto deletedIds = m_sceneManager->deletedEntityIds();
        for (auto id : dirtyIds)
        {
            m_pendingDirtyIds.insert(id);
        }
        for (auto id : deletedIds)
        {
            m_pendingDeletedIds.insert(id);
        }
    }

    // 流水虚线轮廓覆盖层只在"选择集变化"时由 SelectTool 重建。于是当选中图元的几何被改动
    // （对齐/镜像/缩放/移动）时，选择集没变、轮廓也就不会重建，画面里的虚线会停在变换前的
    // 位置。这里补一次通知，让视口按当前场景重新离散轮廓；消费端 syncSelectionFromScene
    // 是从场景全量重读的，重复调用无副作用。

    if (!m_lastSelectedIds.empty())
    {
        emit selectionChanged();
    }

    scheduleSceneUpdate();
}

void SceneRefreshCoordinator::onSelectionChanged()
{
    // P5: 观察者注册收敛 — 发射信号供视口同步工具状态
    emit selectionChanged();

    // [E5-P1 修复] 选择变化走增量渲染，而非全量重建。
    // 旧代码调用 requestFullRefresh() 导致每次单击/悬停都触发整场 gather+tessellate+submit。
    //
    // 选中态不再改变主几何：图元本体始终以原色实线提交，选中反馈只由流水虚线轮廓覆盖层
    // 叠加表达（覆盖层由 SelectTool 在 selectionChanged 时重建）。因此这里无需把选中态
    // 翻转的图元加入脏集合——它们的顶点没有任何变化，重提交只是白做一遍离散化。
    // 仍要维护 m_lastSelectedIds：onSceneChanged 靠它判断"当前有选中"，从而在几何被变换时
    // 补发一次 selectionChanged 让轮廓跟着更新。
    if (m_sceneManager)
    {
        std::unordered_set<uint64_t> currentSelected;
        for (Eg::SyEntity* e : m_sceneManager->getSelectedEntities())
        {
            currentSelected.insert(static_cast<uint64_t>(e->id));
        }
        m_lastSelectedIds = std::move(currentSelected);
    }


    if (m_refreshLevel < RefreshLevel::Selection)
    {
        m_refreshLevel = RefreshLevel::Selection;
    }
    scheduleSceneUpdate();
}

void SceneRefreshCoordinator::applyRepaintRefresh()
{
    if (m_renderWidget)
    {
        m_renderWidget->update();
    }
}

void SceneRefreshCoordinator::applyLightRefresh(Eg::SceneManager* sm)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    // 整批只切一次 GL 上下文、只请求一次重绘。addRenderEntity 单次调用自带一对
    // makeCurrent/doneCurrent 与一次 update()，而一次批量编辑（拖动/对齐上万个
    // 图元）会把它们全部压进脏集合，逐个做上下文切换是秒级开销。
    // 用 RAII 收口，保证中途 return 或抛异常也不会把批量状态漏在开启态。
    struct BatchGuard
    {
        RenderWidget* w;
        explicit BatchGuard(RenderWidget* widget)
            : w(widget)
        {
            w->beginBatchUpload();
        }
        ~BatchGuard()
        {
            w->endBatchUpload();
        }
    } batchGuard(m_renderWidget);

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
        {
            continue;
        }

        // 位图（SyImage）不走折线/线框顶点路径，统一由 reconcileBitmaps 处理
        if (entity->eType == Eg::EType::IMAGE)
        {
            continue;
        }

        auto uid = static_cast<uint64_t>(id);

        // 选中图元照常提交原始实体几何：选中反馈只由虚线轮廓覆盖层叠加表达，
        // 图元本身保持原色实线不变。历史实现在这里把选中图元从 GPU 上移除，
        // 于是一选中图形就"消失"只剩一圈虚线，原图看不出来了；而全量路径
        // （SceneManager::gatherGeometry）本来就不跳过，两条路径规则也是矛盾的。

        std::vector<Render::VertexP3C3> vertices;
        Render::PrimitiveType primType;
        const QPointF cam = m_renderWidget->cameraCenter();
        const double cameraCenter[2] = { cam.x(), cam.y() };
        if (!entityToVertices(entity, vertices, primType, cameraCenter))
        {
            // 该图元无法走增量路径（如文本），标记需要回退到全量刷新
            m_pendingFullRefreshFallback = true;
            continue;
        }

        if (m_renderedEntityIds.count(uid))
        {
            // SY_DEBUGF("[SceneRefreshCoordinator] modifyEntity id=%llu eType=%d primType=%d verts=%zu",
            //     uid, static_cast<int>(entity->eType), static_cast<int>(primType), vertices.size());
            m_renderWidget->modifyRenderEntity(uid, vertices.data(), static_cast<uint32_t>(vertices.size()), primType);
        }
        else
        {
            // SY_DEBUGF("[SceneRefreshCoordinator] addEntity id=%llu eType=%d primType=%d verts=%zu",
            //     uid, static_cast<int>(entity->eType), static_cast<int>(primType), vertices.size());
            m_renderWidget->addRenderEntity(uid, vertices.data(), static_cast<uint32_t>(vertices.size()), primType);
            m_renderedEntityIds.insert(uid);
        }
    }

    // 位图层协调：以场景为真源，增量处理新增/修改/删除/图层显隐
    reconcileBitmaps(sm, /*fullReconcile=*/false);

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
    {
        return;
    }

    // 全量重建时清空曲线离散化缓存，避免持有已删除实体的旧数据
    clearEntityVertexCache();

    m_renderWidget->submitSceneFromDataSource(sm);
    m_renderedEntityIds.clear();
    auto allEntities = sm->getAllEntities();
    for (auto* e : allEntities)
    {
        // 账本必须与 gatherGeometry 的提交规则一致：它不按 selected() 跳过，
        // 因此选中图元同样已在 GPU 上，账本里也要记上。否则下一轮增量会把已存在的
        // 图元当作新图元 addRenderEntity，造成重复提交。
        if (e && e->visible() && (!e->layer() || e->layer()->isVisible()))
        {
            m_renderedEntityIds.insert(static_cast<uint64_t>(e->id));
        }
    }

    // 位图层全量协调：submitSceneFromDataSource 内部 renderBeginScene 已清空 GPU 位图，
    // 这里以场景为真源整体重建，保证与场景生命周期完全一致
    reconcileBitmaps(sm, /*fullReconcile=*/true);
}

void SceneRefreshCoordinator::reconcileBitmaps(Eg::SceneManager* sm, bool fullReconcile)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    if (fullReconcile)
    {
        // 全量重建：GPU 位图已被 renderBeginScene 清空，重置本地账本后整体重传
        m_renderWidget->clearBitmaps();
        m_bitmapImageIds.clear();
    }

    // 期望集合：场景中所有可见 SyImage（可见 = 实体可见 && 图层可见）
    std::unordered_set<uint64_t> desired;
    for (auto* e : sm->getAllEntities())
    {
        if (!e || e->eType != Eg::EType::IMAGE || !e->visible())
        {
            continue;
        }
        if (e->layer() && !e->layer()->isVisible())
        {
            continue;
        }
        desired.insert(static_cast<uint64_t>(e->id));
    }

    // 移除：本地账本中存在但场景已不期望（删除 / 隐藏 / 图层隐藏）
    for (auto it = m_bitmapImageIds.begin(); it != m_bitmapImageIds.end();)
    {
        if (!desired.count(*it))
        {
            m_renderWidget->removeBitmapImage(*it);
            it = m_bitmapImageIds.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 新增/更新：期望但尚未上传 → 上传；已上传但本轮 dirty（内容/几何变化）→ 重新上传。
    // 图层显隐切换由上面的"移除逻辑"间接驱动：隐藏时从账本去除 → 重新显示时 isNew=true。
    for (auto id : desired)
    {
        const bool isNew = m_bitmapImageIds.count(id) == 0;
        const bool isDirty = m_pendingDirtyIds.count(id) > 0;
        if (!isNew && !isDirty)
        {
            continue;
        }

        auto* e = sm->findEntityById(static_cast<Eg::EntityId>(id));
        if (!e)
        {
            continue;
        }

        // 无像素数据的 SyImage 不纳入位图层（移除残留，且不记入账本）
        const auto* image = static_cast<const Eg::SyImage*>(e);
        if (!image->pixelData() || image->nWidth <= 0 || image->nHeight <= 0)
        {
            m_renderWidget->removeBitmapImage(id);
            m_bitmapImageIds.erase(id);
            continue;
        }

        m_renderWidget->setBitmapImage(id, image);
        m_bitmapImageIds.insert(id);
    }
}

// 渲染刷新分发：按 RefreshLevel 级别选择刷新策略
void SceneRefreshCoordinator::updateSceneRender()
{
    if (!m_renderWidget || m_refreshLevel == RefreshLevel::None)
    {
        return;
    }

    if (!m_renderWidget->isInitialized())
    {
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        {
            m_sceneUpdateTimer->start();
        }
        return;
    }

    // P5 收口: RAII 帧计时器，自动管理 beginFrame/endFrame，消除散落的手动调用
    ScopedFrameTimer scopedTimer(m_frameTimer.get());

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    auto* sm = m_sceneManager;
    if (!sm)
    {
        return;
    }

    if (level == RefreshLevel::Repaint)
    {
        applyRepaintRefresh();
        return;
    }

    // LightUpdate / Selection 增量刷新：
    // 删除图元（m_pendingDeletedIds）必须被处理，否则会从渲染世界中被永久遗漏
    // （典型症状：删除后视图不更新）。Selection 级别的样式变更同样由 applyLightRefresh
    // 覆盖——其内部已对脏图元调用 modifyRenderEntity，并对选中图元执行移除（改由虚线轮廓覆盖层表示）。
    // 因此无论 LightUpdate 还是 Selection，只要有增量待办就走 applyLightRefresh。
    // 注意：onSelectionChanged 会先把 m_refreshLevel 提升为 Selection(3)，若紧随其后发生场景删除
    // （onSceneChanged），scheduleSceneUpdate 不会将其降级回 LightUpdate(2)，因此这里必须显式包含
    // Selection 级别，否则待删除图元会在 Selection 分支中被跳过。
    const bool needApplyLight =
        (level == RefreshLevel::LightUpdate) || (level == RefreshLevel::Selection) || !m_pendingDeletedIds.empty();
    if (needApplyLight)
    {
        applyLightRefresh(sm);
    }

    // 仅显式 FullRefresh 级别走此分支，避免与 LightUpdate 的内部回退重复执行
    if (level >= RefreshLevel::FullRefresh)
    {
        applyFullRefresh(sm);
    }

    sm->markClean();
    m_pendingDirtyIds.clear();
    m_pendingDeletedIds.clear();
}