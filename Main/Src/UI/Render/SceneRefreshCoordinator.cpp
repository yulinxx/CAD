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
#include "Engine2D/SyEntity/SyText.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine/SyEntity/EType.h"
#include "Engine/Parallel/EngineParallel.h"

#include "Ut/BBox2d.h"

#include "Log/SyLogger.h"
#include "Log/SyPerfCounter.h"

namespace
{
    // 场景更新节流时间（毫秒）— 16ms 约等于 60fps，合并短时间内的多次场景变更
    constexpr int kSceneUpdateDelay = 16;

    /// 取图元所属图层的绘制序号（0 = 最上层），供增量路径构造 z-order。
    ///
    /// 增量路径的图元快照是 clone，**不复制图层指针**，所以只能查活对象。
    /// 只在主线程调用（SceneManager 的契约是只有主线程可以访问）；
    /// 并行离散化那条路径必须提前在主线程把本值取好带进 job。
    uint32_t layerIndexOfEntity(Eg::SceneManager* sm, Eg::EntityId id)
    {
        if (sm == nullptr)
        {
            return 0;
        }
        const Eg::SyEntity* entity = sm->findEntityById(id);
        const Eg::SyLayer* layer = entity != nullptr ? entity->layer() : nullptr;
        if (layer == nullptr || layer->getIndex() <= 0)
        {
            return 0;
        }
        return static_cast<uint32_t>(layer->getIndex());
    }

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
    // 必须解绑：视口只是弱引用调度器，调度器先走的话它会留着一个悬空指针，
    // 下一次覆盖层写入就会打到已析构对象上。
    if (m_renderWidget)
    {
        m_renderWidget->setRefreshScheduler(nullptr);
        m_renderWidget = nullptr;
    }
}

void SceneRefreshCoordinator::setRenderWidget(RenderWidget* widget)
{
    // 解绑旧视口：否则它会继续把刷新意图打到一个已经不属于自己的调度器上
    if (m_renderWidget && m_renderWidget != widget)
    {
        m_renderWidget->setRefreshScheduler(nullptr);
    }
    m_renderWidget = widget;
    // 反向注入：视口内部的覆盖层 / 位图 / 文字 / 视图矩阵这几类刷新
    // 原来是裸调 update() 完全绕过调度器的，注入后统一经 requestRepaint() 汇入。
    // 这是「所有 2D 刷新来源都能从 pendingLevel() 推理」的前提。
    if (m_renderWidget)
    {
        m_renderWidget->setRefreshScheduler(this);
    }
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
        // 同步游标到当前修订号，避免程序启动后立即触发 FullRefresh
        // （场景加载后 revision 可能已经很大，但 m_lastCursor 初始为 0）
        m_lastCursor = m_sceneManager->currentRevision();
        m_sceneManager->addObserver(this);
    }
}

std::unordered_set<uint64_t> SceneRefreshCoordinator::takePendingDirtyIds()
{
    std::unordered_set<uint64_t> result;
    result.reserve(m_pendingDirtyIds.size());
    for (auto id : m_pendingDirtyIds)
    {
        result.insert(static_cast<uint64_t>(id));
    }
    m_pendingDirtyIds.clear();
    return result;
}

std::unordered_set<uint64_t> SceneRefreshCoordinator::takePendingDeletedIds()
{
    std::unordered_set<uint64_t> result;
    result.reserve(m_pendingDeletedIds.size());
    for (auto id : m_pendingDeletedIds)
    {
        result.insert(static_cast<uint64_t>(id));
    }
    m_pendingDeletedIds.clear();
    return result;
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
        SY_DEBUG("[SceneRefreshCoordinator] Frame performance monitoring enabled");
    }
    else if (!enabled && m_frameTimer)
    {
        if (m_frameTimer->frameCount() > 0)
        {
            m_frameTimer->report();
        }
        m_frameTimer.reset();
        SY_DEBUG("[SceneRefreshCoordinator] Frame performance monitoring disabled");
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

bool SceneRefreshCoordinator::hideSelectedEffective() const
{
    // 开关 + 「虚线此刻真的画得出来」二者必须同时成立，理由见头文件。
    return m_renderWidget != nullptr && m_renderWidget->hideSelectedOriginalEffective();
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

// 曲线 LOD 消费泵：只启动节流定时器、不提升 RefreshLevel —— 曲线 LOD 是独立的
// 后台分批任务，由 updateSceneRender 在每帧末尾无条件推进一步。
void SceneRefreshCoordinator::ensureCurveLodPump()
{
    if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
    {
        m_sceneUpdateTimer->start();
    }
    else if (!m_sceneUpdateTimer)
    {
        processCurveLodBatch();
    }
}

void SceneRefreshCoordinator::finishCurveLodQueue()
{
    m_curveLodQueue.clear();
    m_curveLodCursor = 0;

    // 本轮队列还没消费完时相机可能又变了（连续缩放/平移）：那时 requestCurveLodRefresh
    // 只记了待办、没打断进行中的批次。这里排空后立刻按当时的状态重新评估一次，
    // 否则「平移出上次收集区域」的新视野会一直停在旧精度（视口裁剪必须配这一步）。
    if (m_curveLodRecollectPending)
    {
        m_curveLodRecollectPending = false;
        refillCurveLodQueueIfStale();
        if (!m_curveLodQueue.empty())
        {
            ensureCurveLodPump();
        }
    }
}

void SceneRefreshCoordinator::refillCurveLodQueueIfStale()
{
    if (!m_sceneManager || !m_renderWidget)
    {
        return;
    }

    const float pixelToWorld = m_renderWidget->pixelToWorldScale();
    if (!(pixelToWorld > 0.0f))
    {
        return;
    }
    const double worldToScreenScale = 1.0 / static_cast<double>(pixelToWorld);

    // 可视世界矩形（含 1.25 倍余量）。曲线 LOD 的精度只对看得见的图元有意义 ——
    // 为了恢复视野内几十/几百个图元的精度去重算整份场景（29 万图元）既慢又白做。
    // 余量是为了减少平移/微缩放时反复重新收集的边缘抖动。
    // 矩阵退化（visibleWorldBounds 返回 false）时退化为不裁剪，与旧行为一致。
    Ut::BBox2d collectBox;
    Ut::BBox2d visibleBox;
    bool hasVisibleBox = false;
    {
        float bounds[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (m_renderWidget->visibleWorldBounds(bounds))
        {
            const double minX = static_cast<double>(bounds[0]);
            const double minY = static_cast<double>(bounds[1]);
            const double maxX = static_cast<double>(bounds[2]);
            const double maxY = static_cast<double>(bounds[3]);
            visibleBox = Ut::BBox2d(minX, minY, maxX, maxY);

            constexpr double kVisibleMargin = 1.25;
            const double centerX = 0.5 * (minX + maxX);
            const double centerY = 0.5 * (minY + maxY);
            const double halfW = 0.5 * (maxX - minX) * kVisibleMargin;
            const double halfH = 0.5 * (maxY - minY) * kVisibleMargin;
            collectBox = Ut::BBox2d(centerX - halfW, centerY - halfH, centerX + halfW, centerY + halfH);
            hasVisibleBox = true;
        }
    }

    // 过期判据：
    //   缩放滞后（放大到 1.3 倍就升级、缩小到 3 倍以下才降级，中间留死区避免抖动），或
    //   可视区域越出上次收集覆盖的矩形（平移）。
    // 降级阈值 2.0 → 3.0：缩小过程中 LOD 重收集过于频繁（29 万多边形全量入队时一次
    // 就要处理数秒），放大幅降级死区，让缩小时尽量沿用已建好的高精度数据，
    // 只有缩得足够远（精度冗余明显）时才一次性降级。
    constexpr double kCurveLodUpgradeRatio = 1.3;
    constexpr double kCurveLodDowngradeRatio = 3.0;
    const bool firstTime = m_curveLodScaleAtBuild <= 0.0;
    const bool needsUpgrade =
        firstTime || worldToScreenScale > m_curveLodScaleAtBuild * kCurveLodUpgradeRatio;
    const bool needsDowngrade =
        !firstTime && worldToScreenScale * kCurveLodDowngradeRatio < m_curveLodScaleAtBuild;
    const bool regionMoved = hasVisibleBox && m_curveLodCollectedBoxValid &&
        !m_curveLodCollectedBox.contains(visibleBox);
    if (!needsUpgrade && !needsDowngrade && !regionMoved)
    {
        return;
    }

    m_curveLodScaleAtBuild = worldToScreenScale;
    m_curveLodCollectedBox = collectBox;
    m_curveLodCollectedBoxValid = hasVisibleBox;

    // 收集曲线类图元 ID：只有它们的离散化段数/顶点数随缩放变化。
    // 必须覆盖所有「按 worldToScreenScale 自适应离散化/简化」的类型（见
    // EntityGeometryEmitter::emitEntityGeometry），否则放大后图元不会重建、精度停留低档。
    //   - CIRCLE/ARC/ELLIPSE：GPU 原生，弦误差随 zoom 变化
    //   - BEZIER2/BEZIER/SPLINE/NURBS/SMARTLINE：BezierAlgorithms 自适应离散化
    //   - POLYGON：zoom out 时 Douglas-Peucker 简化，zoom in 要恢复原始顶点
    const Eg::EType curveTypes[] = { Eg::EType::CIRCLE,
        Eg::EType::ARC,
        Eg::EType::ELLIPSE,
        Eg::EType::BEZIER2,
        Eg::EType::BEZIER,
        Eg::EType::SPLINE,
        Eg::EType::NURBS,
        Eg::EType::SMARTLINE,
        Eg::EType::POLYGON };

    m_curveLodQueue.clear();
    m_curveLodCursor = 0;

    // 诊断：统计各类型入队数量，定位「放大不恢复精度」时是哪些类型没被覆盖；
    // polyInvariantSkipped 记「顶点数不超过简化阈值、随缩放不会变化，因而被跳过」的多边形数。
    // 这类多边形在每个缩放下都原样发射顶点（重新离散得到相同结果），纳入 LOD 队列
    // 只会白做一遍顶点解码 + GPU 重传（抽稀图纸里 29 万图元中绝大多数属此类）。
    size_t perTypeCounts[sizeof(curveTypes) / sizeof(curveTypes[0])] = { 0 };
    size_t polygonInvariantSkipped = 0;
    for (size_t t = 0; t < sizeof(curveTypes) / sizeof(curveTypes[0]); ++t)
    {
        size_t& typeCount = perTypeCounts[t];
        const bool isPolygon = (curveTypes[t] == Eg::EType::POLYGON);
        // forEachEntityByType 走类型索引，O(k) 且不拷贝整份指针数组
        // （getEntitiesByType 按值返回，POLYGON 那 29 万个指针会白拷 2.3MB）。
        m_sceneManager->forEachEntityByType(curveTypes[t], [&](Eg::SyEntity* entity) {
            if (!entity)
            {
                return;
            }
            // POLYGON 只有顶点数 > kPolygonSimplifyMinVertices 时才走 Douglas-Peucker 简化
            // （见 EntityGeometryEmitter emitEntityGeometry 的 POLYGON 分支）。
            // 顶点数不超过阈值的多边形在任意缩放下离散结果都相同，入 LOD 队列纯浪费。
            if (isPolygon)
            {
                const auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
                if (polygon->vertices().size() <= Eg::kPolygonSimplifyMinVertices)
                {
                    ++polygonInvariantSkipped;
                    return;
                }
            }
            if (hasVisibleBox)
            {
                // 包围盒命中缓存（且导入时已由文件里的 cached_bbox 播种），这里是 O(1) 判断
                const Ut::BBox2d box = entity->getBbox();
                if (!box.isValid() || !box.intersects(collectBox))
                {
                    return;
                }
            }
            m_curveLodQueue.push_back(static_cast<uint64_t>(entity->id));
            ++typeCount;
        });
    }

    // 全场景类型总览（含不在 LOD 队列里的类型），一次性看清图元构成
    size_t totalEntities = m_sceneManager->getEntityCount();
    SY_DEBUGF("[SceneRefreshCoordinator] curveLodRefresh: total=%zu queued=%zu clipped=%d scale=%.4f "
              "polyInvariant=%zu circ=%zu arc=%zu ell=%zu b2=%zu bez=%zu spl=%zu nurbs=%zu smart=%zu poly=%zu",
        totalEntities,
        m_curveLodQueue.size(),
        hasVisibleBox ? 1 : 0,
        worldToScreenScale,
        polygonInvariantSkipped,
        perTypeCounts[0], perTypeCounts[1], perTypeCounts[2], perTypeCounts[3], perTypeCounts[4],
        perTypeCounts[5], perTypeCounts[6], perTypeCounts[7], perTypeCounts[8]);
}

void SceneRefreshCoordinator::requestCurveLodRefresh()
{
    if (!m_sceneManager || !m_renderWidget)
    {
        return;
    }

    // 队列还有未完成的重建（cursor 未到末尾）：不重置、不重新收集。
    // processCurveLodBatch 每帧从 renderWidget 读最新 worldToScreenScale，继续重建会
    // 自然用最新缩放。否则缩放连续触发时（滚轮/拖拽每帧都变），每次都 clear 队列 +
    // 重新收集 + 归零 cursor，重建进度永远被重置，放大后精度就一直停留在低档
    // （正是「放大后曲线仍糊」的根因）。
    // 但相机确实又变了，记一笔待办：队列排空时由 finishCurveLodQueue 重新评估
    // （否则平移出上次收集区域后，新进视野的图元会一直停在旧精度）。
    if (m_curveLodCursor < m_curveLodQueue.size())
    {
        m_curveLodRecollectPending = true;
        ensureCurveLodPump();
        return;
    }

    refillCurveLodQueueIfStale();
    if (!m_curveLodQueue.empty())
    {
        ensureCurveLodPump();
    }
}

// 场景变更通知入口
// 通知链路：SceneNotifier::notifySceneChanged() → 此函数 → scheduleSceneUpdate() → updateSceneRender()
// 收集脏/删除图元 ID，升级到 LightUpdate 级别
void SceneRefreshCoordinator::onSceneChanged()
{
    if (m_sceneManager)
    {
        Eg::SceneChangeSet changes;
        if (m_sceneManager->readChanges(m_lastCursor, changes))
        {
            // 在这里（UI 线程）就把变更图元 clone 成只读快照：增量刷新可能在
            // 工作线程里离散化，那时再去读活对象就会和用户的编辑并发。
            if (!m_sceneManager->captureSnapshot(changes, m_pendingSnapshot))
            {
                // 快照拿不全就别走增量 —— 否则失败的那些图元会被当成「没变」而漏刷
                m_refreshLevel = RefreshLevel::FullRefresh;
                scheduleFullRefresh();
                return;
            }

            for (const auto& change : changes.changes)
            {
                if (hasSceneChangeKind(change.kinds, Eg::SceneChangeKind::Removed))
                {
                    m_pendingDeletedIds.insert(change.entityId);
                }
                else
                {
                    m_pendingDirtyIds.insert(change.entityId);
                }
            }
            m_lastCursor = changes.toRevision;
        }
        else
        {
            // readChanges 失败，触发 FullRefresh
            // 同时更新游标到当前修订号，避免后续每次 notifySceneChanged 都重复触发 FullRefresh
            m_lastCursor = m_sceneManager->currentRevision();
            m_refreshLevel = RefreshLevel::FullRefresh;
            scheduleFullRefresh();
            return;
        }
    }

    // 流水虚线轮廓覆盖层只在"选择集变化"时由 SelectTool 重建。于是当选中图元的几何被改动
    // （对齐/镜像/缩放/移动）时，选择集没变、轮廓也就不会重建，画面里的虚线会停在变换前的
    // 位置。这里补发一次轮廓失效通知（而非 selectionChanged：后者会让视口连带动属性面板
    // 重建与场景树选择重设，拖动时每步都做一遍太重）；消费端 syncSelectionToolState
    // 是从场景全量重读的，重复调用无副作用。

    if (!m_lastSelectedIds.empty())
    {
        // 只发"轮廓要重建"这一个意图：此处并非真的选择集变化，走公开的 selectionChanged
        // 会连带触发属性面板重建与场景树的选择重设 —— 拖动中每次几何变更都做一遍，是卡顿主因。
        emit selectionOutlineInvalidated();
    }

    scheduleSceneUpdate();
}

void SceneRefreshCoordinator::onSelectionChanged()
{
    // P5: 观察者注册收敛 — 发射信号供视口同步工具状态
    emit selectionChanged();

    // 选择变化走增量渲染。选中态不改变主几何：图元本体始终以原色实线提交，
    // 选中反馈只由流水虚线轮廓覆盖层叠加表达（覆盖层由 SelectTool 在 selectionChanged 时重建）。
    // 因此这里无需把选中态翻转的图元加入脏集合——它们的顶点没有变化，重提交只是白做离散化。
    // 仍要维护 m_lastSelectedIds：onSceneChanged 靠它判断"当前有选中"，从而在几何被变换时
    // 补发一次 selectionOutlineInvalidated 让轮廓跟着更新。
    bool selectionSetChanged = false;
    if (m_sceneManager)
    {
        std::unordered_set<uint64_t> currentSelected;
        m_sceneManager->forEachSelected([&currentSelected](Eg::SyEntity* e) {
            currentSelected.insert(static_cast<uint64_t>(e->id));
        });

        // 对称差非空 = 选择集真的变了（有图元被选中或取消选中）
        for (uint64_t id : currentSelected)
        {
            if (!m_lastSelectedIds.count(id))
            {
                selectionSetChanged = true;
                break;
            }
        }
        if (!selectionSetChanged)
        {
            for (uint64_t id : m_lastSelectedIds)
            {
                if (!currentSelected.count(id))
                {
                    selectionSetChanged = true;
                    break;
                }
            }
        }

        // 「隐藏选中本体」有效时，选择集变化在世界层的效果就是：
        //   新选中的 → 本体要从世界层摘掉（m_selectionHideIds）
        //   取消选中的 → 本体要加回世界层（m_selectionRestoreIds）
        // 只记差集，交给 applyLightRefresh 增量处理。这里必须用**上一次实际生效**的
        // 隐藏状态来分情形，否则「有效状态翻转」那两种情形会漏掉整批本体：
        //   hidePrev=true, hideNow=false → 上次被摘掉的选中集（含本次已取消选中的）
        //                                  全都要加回来；
        //   hidePrev=false, hideNow=true → 当前所有选中都要摘掉。
        if (selectionSetChanged)
        {
            const bool hidePrev = m_lastHideSelectedEffective;
            const bool hideNow = hideSelectedEffective();
            if (hideNow && hidePrev)
            {
                for (uint64_t id : currentSelected)
                {
                    if (!m_lastSelectedIds.count(id))
                    {
                        m_selectionHideIds.insert(id);
                    }
                }
                for (uint64_t id : m_lastSelectedIds)
                {
                    if (!currentSelected.count(id))
                    {
                        m_selectionRestoreIds.insert(id);
                    }
                }
            }
            else if (hideNow)
            {
                m_selectionHideIds.insert(currentSelected.begin(), currentSelected.end());
            }
            else if (hidePrev)
            {
                // 上次摘掉过的本体必须回来：上次的选中集 ∪ 本次选中集
                m_selectionRestoreIds.insert(m_lastSelectedIds.begin(), m_lastSelectedIds.end());
                m_selectionRestoreIds.insert(currentSelected.begin(), currentSelected.end());
            }
        }

        m_lastSelectedIds = std::move(currentSelected);
        // 同步缓存：onSelectionChanged 是选中集变更的唯一入口
        m_cachedSelectedIds = m_lastSelectedIds;
        m_selectedIdsCacheValid = true;
    }

    // 这里判的是「这次刷新要做到什么级别」，因此看的是**有效**隐藏状态
    // （开关 && 虚线画得出来），而不是开关本身：
    //
    //   开关关着 / 开关开着但轮廓画不出来（选中集超预算）
    //       → 本体根本没被摘，选择集变化不动世界层 → 廉价的 Selection 级就够；
    //   开关开着且轮廓可画
    //       → 选中本体的增删需要动世界层，但只需要动**差集**那几个图元；
    //   有效状态本身翻转（上次摘了、这次不该摘；或反过来）
    //       → 差集可能是一整批，仍然只处理这批。
    //
    // 历史上第三种情形走的是 FullRefresh（重建整份场景）：29 万图元、高缩放下单次
    // 十几秒，而且顺带把视口外所有图元的 LOD 一并重建（用户感知就是"选一下/移动一下
    // 卡很久，然后连视图范围外的图元精度也全部恢复了"）。
    // 现在改由 m_selectionHideIds / m_selectionRestoreIds 记差集，applyLightRefresh
    // 里增删这几个图元即可 —— 效果与全量重建一致，代价与选中数量成正比。
    if (!m_selectionHideIds.empty() || !m_selectionRestoreIds.empty())
    {
        if (m_refreshLevel < RefreshLevel::LightUpdate)
        {
            m_refreshLevel = RefreshLevel::LightUpdate;
        }
    }
    else if (m_refreshLevel < RefreshLevel::Selection)
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

    // 获取"选中时隐藏原图"的**有效**设置（开关 + 虚线画得出来），见 hideSelectedEffective()
    const bool hideSelected = hideSelectedEffective();

    // 有效状态翻转不一定由选择集变化触发：改轮廓预算、缩放导致轮廓顶点数越过预算都会翻，
    // 那些触发方补不出差集。这里统一兜底 —— 只要状态翻了，就按当前选中集补出整批差集，
    // 交给本函数下面的增量路径处理，不必再让任何触发方去走 O(场景) 的全量重建
    // （29 万图元、高缩放下单次十几秒，且会顺带把视口外所有图元的 LOD 一并重建）。
    if (hideSelected != m_lastHideSelectedEffective)
    {
        if (hideSelected)
        {
            m_selectionHideIds.insert(m_cachedSelectedIds.begin(), m_cachedSelectedIds.end());
        }
        else
        {
            m_selectionRestoreIds.insert(m_cachedSelectedIds.begin(), m_cachedSelectedIds.end());
        }
    }
    // 记下本次实际生效的隐藏状态：onSelectionChanged 判断「这次选择集变化要补哪些差集」
    // 时要用它识别「有效状态翻转」（见该处的注释）
    m_lastHideSelectedEffective = hideSelected;

    // 缓存的选中 ID 集合：判断某个图元的本体此刻该不该在世界层里，只看它。
    const std::unordered_set<uint64_t>& selectedIds = m_cachedSelectedIds;

    // 纯选择变更优化：脏集合、删除集合、选中差集都为空时，场景几何未变，
    // 无需创建 BatchGuard（GL 上下文切换）、无需 reconcileBitmaps/Texts。
    // 选择轮廓/手柄已由 onSelectionChanged → syncSelectionToolState 直接更新到 GPU，
    // 此处只需触发一次 repaint 即可。
    if (m_pendingDirtyIds.empty() && m_pendingDeletedIds.empty() && m_selectionHideIds.empty() &&
        m_selectionRestoreIds.empty())
    {
        m_renderWidget->update();
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

    // 「隐藏选中本体」新选中的那批：直接从世界层摘掉本体。
    // 与删除走同一条形状 —— 摘本体不需要任何离散化，只要账本跟着改。
    // 这里再按「此刻该不该摘」复核一次：差集是上一轮选择变化记下的，这一轮之间
    // 选择集或有效隐藏状态还可能再翻（例如连续两次选择变化），过期条目在这里被过滤，
    // 保证最终状态只由**当前**的 hideSelected + selectedIds 决定。
    for (uint64_t uid : m_selectionHideIds)
    {
        if (!hideSelected || !selectedIds.count(uid))
        {
            continue;
        }
        if (m_renderedEntityIds.count(uid))
        {
            m_renderWidget->removeRenderEntity(uid);
            m_renderedEntityIds.erase(uid);
        }
    }
    m_selectionHideIds.clear();

    // 本轮脏集合里是否出现过位图 / 文字图元。下面用它决定要不要跑 reconcile*：
    // reconcileBitmaps/reconcileTexts 现在使用 getEntitiesByType 按类型索引获取，
    // 避免全场景扫描。标志在这个循环里顺手收集，不额外多做一次 findEntityById。
    bool touchedImage = false;
    bool touchedText = false;

    // 优化：收集需要处理的图元ID（可见的、非 IMAGE/TEXT 的）
    // 用于后续的并行处理
    std::vector<uint64_t> entityIdsToProcess;
    entityIdsToProcess.reserve(m_pendingDirtyIds.size());

    // 与 entityIdsToProcess 同下标的图层绘制序号。必须在这条主线程循环里取好：
    // 下面那条并行分支在工作线程里离散化，工作线程不允许访问 SceneManager。
    std::vector<uint32_t> entityLayerOrders;
    entityLayerOrders.reserve(m_pendingDirtyIds.size());

    for (auto id : m_pendingDirtyIds)
    {
        // 读快照而不是活对象：下面那条并行分支会在工作线程里离散化
        const Eg::RenderEntitySnapshot* snapshot = m_pendingSnapshot.find(id);
        if (snapshot == nullptr || snapshot->entity() == nullptr)
        {
            continue;
        }

        auto uid = static_cast<uint64_t>(id);

        // 图元可见性 = 自身可见 且 所在图层可见。图层指针不随 clone 复制，
        // 但这个判断仍在主线程上做，工作线程只负责顶点离散化。
        if (!snapshot->effectiveVisible())
        {
            // 图元不可见：从渲染中移除（如果之前有渲染的话）
            if (m_renderedEntityIds.count(uid))
            {
                m_renderWidget->removeRenderEntity(uid);
                m_renderedEntityIds.erase(uid);
            }
            continue;
        }

        // 如果启用了"选中时隐藏原图"且实体被选中，则跳过渲染
        if (hideSelected && selectedIds.count(uid))
        {
            // 从渲染中移除（如果之前有渲染的话）
            if (m_renderedEntityIds.count(uid))
            {
                m_renderWidget->removeRenderEntity(uid);
                m_renderedEntityIds.erase(uid);
            }
            continue;
        }

        // 位图（SyImage）不走折线/线框顶点路径，统一由 reconcileBitmaps 处理
        if (snapshot->type == Eg::EType::IMAGE)
        {
            touchedImage = true;
            continue;
        }

        // 文本（SyText）同理：字形四边形由 reconcileTexts 一路处理。
        if (snapshot->type == Eg::EType::TEXT)
        {
            touchedText = true;
            continue;
        }

        // 收集需要处理的图元 ID 与其图层绘制序号（两向量同下标）
        entityIdsToProcess.push_back(uid);
        entityLayerOrders.push_back(layerIndexOfEntity(sm, static_cast<Eg::EntityId>(uid)));
    }

    // 如果需要处理的图元数量大于阈值，使用并行处理
    // 否则使用串行处理（减少线程调度开销）
    constexpr size_t kParallelThreshold = 100;
    const QPointF cam = m_renderWidget->cameraCenter();
    const double cameraCenter[2] = { cam.x(), cam.y() };
    // 世界单位 → 屏幕像素 = 1 / 每像素世界单位。曲线离散化按此自适应段数，
    // 与全量路径 RenderSceneBuilder 的 setWorldToScreenScale 用同一个来源。
    const float pixelToWorld = m_renderWidget->pixelToWorldScale();
    const double worldToScreenScale = pixelToWorld > 0.0f ? 1.0 / static_cast<double>(pixelToWorld) : 1.0;
    // 曲线 LOD 目标弦高误差（用户可在设置里调节），与全量路径取同一个来源
    const double chordErrorPixels = m_renderWidget->lodChordErrorPixels();

    if (entityIdsToProcess.size() >= kParallelThreshold && Eg::EngineParallel::isEnabled())
    {
        // 并行处理：转换顶点
        struct RenderJob
        {
            uint64_t uid;
            std::vector<Render::VertexP3C3> vertices;
            Render::PrimitiveType primType;
            /// 图层绘制序号（0 = 最上层）。主线程已取好，工作线程只做搬运
            uint32_t layerOrder = 0;
            bool valid = false;
        };

        const size_t count = entityIdsToProcess.size();
        std::vector<RenderJob> jobs(count);

        Eg::EngineParallel::parallelForIndex(count, [&](size_t index) {
            auto id = entityIdsToProcess[index];
            // 只读快照：副本是主线程 clone 好的，工作线程不再触碰场景数据
            const Eg::RenderEntitySnapshot* snapshot = m_pendingSnapshot.find(static_cast<Eg::EntityId>(id));
            if (snapshot == nullptr || snapshot->entity() == nullptr)
            {
                return;
            }

            RenderJob& job = jobs[index];
            job.uid = id;
            job.layerOrder = entityLayerOrders[index];

            if (entityToVertices(
                    snapshot->entity(), job.vertices, job.primType, cameraCenter, worldToScreenScale, chordErrorPixels))
            {
                job.valid = true;
            }
        });

        // 串行提交到 GPU（保证顺序）
        for (size_t i = 0; i < count; ++i)
        {
            const auto& job = jobs[i];
            if (!job.valid)
            {
                continue;
            }

            if (m_renderedEntityIds.count(job.uid))
            {
                m_renderWidget->modifyRenderEntity(job.uid,
                    job.vertices.data(),
                    static_cast<uint32_t>(job.vertices.size()),
                    job.primType,
                    job.layerOrder);
            }
            else
            {
                m_renderWidget->addRenderEntity(job.uid,
                    job.vertices.data(),
                    static_cast<uint32_t>(job.vertices.size()),
                    job.primType,
                    job.layerOrder);
                m_renderedEntityIds.insert(job.uid);
            }
        }
    }
    else
    {
        // 串行处理（小批量时更高效）
        for (size_t index = 0; index < entityIdsToProcess.size(); ++index)
        {
            const uint64_t uid = entityIdsToProcess[index];
            const Eg::RenderEntitySnapshot* snapshot = m_pendingSnapshot.find(static_cast<Eg::EntityId>(uid));
            if (snapshot == nullptr || snapshot->entity() == nullptr)
            {
                continue;
            }

            std::vector<Render::VertexP3C3> vertices;
            Render::PrimitiveType primType;
            // 图元无法增量转换为顶点，已跳过
            if (!entityToVertices(
                    snapshot->entity(), vertices, primType, cameraCenter, worldToScreenScale, chordErrorPixels))
            {
                SY_WARNF("[SceneRefreshCoordinator] Entity %llu (eType=%d) cannot be incrementally converted to "
                         "vertices, skipped",
                    static_cast<unsigned long long>(uid),
                    static_cast<int>(snapshot->type));
                continue;
            }

            if (m_renderedEntityIds.count(uid))
            {
                m_renderWidget->modifyRenderEntity(
                    uid, vertices.data(), static_cast<uint32_t>(vertices.size()), primType, entityLayerOrders[index]);
            }
            else
            {
                m_renderWidget->addRenderEntity(
                    uid, vertices.data(), static_cast<uint32_t>(vertices.size()), primType, entityLayerOrders[index]);
                m_renderedEntityIds.insert(uid);
            }
        }
    }

    // 「隐藏选中本体」取消选中的那批：把本体重新加回世界层。
    // 没有快照可用（选择变更不进变更流），这里在主线程直接读活对象 —— 与
    // applyFullRefresh 同源、同一条 entityToVertices 口径，结果与全量重建一致。
    // 位图/文字不走这条顶点路径，交给下面的 reconcile*。
    if (!m_selectionRestoreIds.empty())
    {
        for (uint64_t uid : m_selectionRestoreIds)
        {
            // 此刻仍该被摘的（仍然被选中且隐藏有效）不动它 —— 与上面 hide 侧同一个判据，
            // 两边对称，连续两次选择变化时过期的差集条目不会把本体错误地放回去。
            if (hideSelected && selectedIds.count(uid))
            {
                continue;
            }

            // 已经在世界层里（例如同时也在脏集合里被重提交过）就不重复加
            if (m_renderedEntityIds.count(uid))
            {
                continue;
            }

            const Eg::SyEntity* e = sm->findEntityById(static_cast<Eg::EntityId>(uid));
            if (e == nullptr || !e->visible())
            {
                continue;
            }
            const Eg::SyLayer* layer = e->layer();
            if (layer != nullptr && !layer->isVisible())
            {
                continue;
            }

            if (e->eType == Eg::EType::IMAGE)
            {
                touchedImage = true;
                continue;
            }
            if (e->eType == Eg::EType::TEXT)
            {
                touchedText = true;
                continue;
            }

            std::vector<Render::VertexP3C3> vertices;
            Render::PrimitiveType primType;
            if (!entityToVertices(e, vertices, primType, cameraCenter, worldToScreenScale, chordErrorPixels))
            {
                continue;
            }

            m_renderWidget->addRenderEntity(uid,
                vertices.data(),
                static_cast<uint32_t>(vertices.size()),
                primType,
                layerIndexOfEntity(sm, e->id));
            m_renderedEntityIds.insert(uid);
        }
        m_selectionRestoreIds.clear();
    }

    // 位图层协调：以场景为真源，增量处理新增/修改/删除/图层显隐。
    //
    // 两个短路条件是「或」而不是「与」，缺哪个都会漏画：
    //   - touchedImage：本轮有位图变脏/新增，必须对账；
    //   - 账本非空：图元或图层转为不可见**不会**进脏集合（LayerManager::setLayerVisible
    //     只改标志位），只能靠这里的全量对账发现。账本非空时跳过就会留下残影。
    // 两者都不成立时（账本空 && 本轮没碰位图）对账必然是空转，可以安全跳过。
    if (touchedImage || !m_bitmapImageIds.empty())
    {
        reconcileBitmaps(sm, /*fullReconcile=*/false);
    }

    // 世界文字层协调：同上。文本自己一路后，增量刷新不再因文本升级为全量。
    if (touchedText || !m_worldTextIds.empty())
    {
        reconcileTexts(sm, /*fullReconcile=*/false);
    }

    // 位图/文字协调完成后不需要额外 update() ——
    // BatchGuard 析构时 endBatchUpload() 已调用一次 QOpenGLWidget::update()。
}

void SceneRefreshCoordinator::processCurveLodBatch()
{
    if (!m_renderWidget || !m_sceneManager)
    {
        return;
    }

    if (m_curveLodCursor >= m_curveLodQueue.size())
    {
        finishCurveLodQueue();
        return;
    }

    const QPointF cam = m_renderWidget->cameraCenter();
    const double cameraCenter[2] = { cam.x(), cam.y() };
    const float pixelToWorld = m_renderWidget->pixelToWorldScale();
    const double worldToScreenScale = pixelToWorld > 0.0f ? 1.0 / static_cast<double>(pixelToWorld) : 1.0;
    const double chordErrorPixels = m_renderWidget->lodChordErrorPixels();

    // 每帧预算：串行重建 2000 个曲线图元，亚毫秒级。zoom 期间场景不变，
    // 直接在主线程读活对象（SceneManager 契约），无需 clone 快照。
    constexpr size_t kBatchSize = 2000;
    const size_t end = (std::min)(m_curveLodCursor + kBatchSize, m_curveLodQueue.size());

    // 整批只切一次 GL 上下文、只请求一次重绘（与 applyLightRefresh 同策略）
    m_renderWidget->beginBatchUpload();

    for (size_t i = m_curveLodCursor; i < end; ++i)
    {
        const uint64_t uid = m_curveLodQueue[i];
        Eg::SyEntity* entity = m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
        if (!entity)
        {
            continue;
        }

        std::vector<Render::VertexP3C3> vertices;
        Render::PrimitiveType primType;
        if (!entityToVertices(entity, vertices, primType, cameraCenter, worldToScreenScale, chordErrorPixels))
        {
            continue;
        }

        if (m_renderedEntityIds.count(uid))
        {
            m_renderWidget->modifyRenderEntity(uid,
                vertices.data(),
                static_cast<uint32_t>(vertices.size()),
                primType,
                layerIndexOfEntity(m_sceneManager, entity->id));
        }
        else
        {
            m_renderWidget->addRenderEntity(uid,
                vertices.data(),
                static_cast<uint32_t>(vertices.size()),
                primType,
                layerIndexOfEntity(m_sceneManager, entity->id));
            m_renderedEntityIds.insert(uid);
        }
    }

    m_renderWidget->endBatchUpload();

    m_curveLodCursor = end;

    if (m_curveLodCursor < m_curveLodQueue.size())
    {
        // 还有剩余，下一帧继续（不提升 RefreshLevel，仍是纯后台分批）
        ensureCurveLodPump();
    }
    else
    {
        finishCurveLodQueue();
    }
}

void SceneRefreshCoordinator::applyFullRefresh(Eg::SceneManager* sm)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    // 全量重建：几何与台账都由 RenderSceneBuilder 的装配轮次自行对齐
    // （本轮没出现的图元会被回收），这里不需要再做任何缓存失效
    m_renderWidget->submitSceneFromDataSource(sm);

    // 游标必须对齐到当前修订号：全量重建走到这里通常是因为 readChanges 判定 cursor stale
    // （变更日志被 prune 截断，旧游标落在 floor 之前）。若不更新游标，之后每次
    // notifySceneChanged 都会再次 stale，70 万图元场景下退化成「每次编辑都全量重建」。
    // 全量结果已反映当前场景状态，直接把游标锚定到最新修订号即可恢复后续增量链路。
    m_lastCursor = sm->currentRevision();

    m_renderedEntityIds.clear();

    // 获取"选中时隐藏原图"的**有效**设置（开关 + 虚线画得出来），见 hideSelectedEffective()
    const bool hideSelected = hideSelectedEffective();
    // 记下本次实际生效的隐藏状态：onSelectionChanged 判断「这次选择集变化要不要全量重建」
    // 时要用它识别「有效状态翻转」（见该处的注释）
    m_lastHideSelectedEffective = hideSelected;

    // 如果启用隐藏选中实体，使用缓存的选中 ID 集合
    const std::unordered_set<uint64_t>& selectedIds = m_cachedSelectedIds;

    sm->forEachEntity([this, hideSelected, &selectedIds](Eg::SyEntity* e) {
        // 账本必须与 gatherGeometry 的提交规则一致：它不按 selected() 跳过，
        // 因此选中图元同样已在 GPU 上，账本里也要记上。否则下一轮增量会把已存在的
        // 图元当作新图元 addRenderEntity，造成重复提交。
        // 但如果启用了"选中时隐藏原图"，则选中实体不应被渲染，账本也不应记录
        if (e && e->visible() && (!e->layer() || e->layer()->isVisible()))
        {
            auto uid = static_cast<uint64_t>(e->id);
            if (hideSelected && selectedIds.count(uid))
            {
                // 选中且启用了隐藏，不记录到渲染账本（也不会被渲染）
                return;
            }
            m_renderedEntityIds.insert(uid);
        }
    });

    // "选中时隐藏原图"：上面的 submitSceneFromDataSource 已把全部图元（含选中）上传到 GPU，
    // 这里把选中图元的本体从 GPU 移除，使画面只剩流水虚线轮廓。账本已在上面循环中排除它们，
    // GPU 与账本因此保持一致。
    if (hideSelected)
    {
        for (uint64_t uid : selectedIds)
        {
            m_renderWidget->removeRenderEntity(uid);
        }
    }

    // 全量重建已按当前选中集把本体的增删做到位，待处理的选中差集就此过期：
    // 留着会在下一轮增量里多摘/多加一次（比如上一轮的"取消选中要加回来"撞上本次重建的"应摘除"）。
    m_selectionHideIds.clear();
    m_selectionRestoreIds.clear();

    // 全量重建还把整份场景按当前缩放重新离散了一遍，曲线 LOD 的「已收集区域」记录
    // 就此作废：重新锚定缩放基准（否则下一帧拿旧基准比较会白白重收集一次），
    // 并把区域记录置为无效 —— 全场景都已是当前精度，平移不需要再补收集。
    m_curveLodQueue.clear();
    m_curveLodCursor = 0;
    m_curveLodRecollectPending = false;
    const float pxToWorldNow = m_renderWidget->pixelToWorldScale();
    m_curveLodScaleAtBuild = pxToWorldNow > 0.0f ? 1.0 / static_cast<double>(pxToWorldNow) : 0.0;
    m_curveLodCollectedBoxValid = false;

    // 位图层全量协调：submitSceneFromDataSource 内部 renderBeginScene 已清空 GPU 位图，
    // 这里以场景为真源整体重建，保证与场景生命周期完全一致
    reconcileBitmaps(sm, /*fullReconcile=*/true);

    // 世界文字层全量协调：同上
    reconcileTexts(sm, /*fullReconcile=*/true);
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

    // 获取"选中时隐藏原图"的**有效**设置（开关 + 虚线画得出来），见 hideSelectedEffective()
    const bool hideSelected = hideSelectedEffective();

    // 如果启用隐藏选中实体，使用缓存的选中 ID 集合
    const std::unordered_set<uint64_t>& selectedIds = m_cachedSelectedIds;

    // 期望集合：场景中所有可见 SyImage（可见 = 图元可见 && 图层可见）
    std::unordered_set<uint64_t> desired;
    // 使用类型索引避免全场景扫描
    auto imageEntities = sm->getEntitiesByType(Eg::EType::IMAGE);
    for (auto* e : imageEntities)
    {
        if (!e || !e->visible())
        {
            continue;
        }
        if (e->layer() && !e->layer()->isVisible())
        {
            continue;
        }
        auto uid = static_cast<uint64_t>(e->id);
        // 如果启用了"选中时隐藏原图"且实体被选中，跳过
        if (hideSelected && selectedIds.count(uid))
        {
            continue;
        }
        desired.insert(uid);
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

void SceneRefreshCoordinator::reconcileTexts(Eg::SceneManager* sm, bool fullReconcile)
{
    if (!m_renderWidget || !sm)
    {
        return;
    }

    if (fullReconcile)
    {
        // 全量重建：重置本地账本与渲染侧账本后整体重传
        m_renderWidget->clearWorldTexts();
        m_worldTextIds.clear();
    }

    // 获取"选中时隐藏原图"的**有效**设置（开关 + 虚线画得出来），见 hideSelectedEffective()
    const bool hideSelected = hideSelectedEffective();

    // 如果启用隐藏选中实体，使用缓存的选中 ID 集合
    const std::unordered_set<uint64_t>& selectedIds = m_cachedSelectedIds;

    // 期望集合：场景中所有可见 SyText（可见 = 图元可见 && 图层可见）
    std::unordered_set<uint64_t> desired;
    // 使用类型索引避免全场景扫描
    auto textEntities = sm->getEntitiesByType(Eg::EType::TEXT);
    for (auto* e : textEntities)
    {
        if (!e || !e->visible())
        {
            continue;
        }
        if (e->layer() && !e->layer()->isVisible())
        {
            continue;
        }
        auto uid = static_cast<uint64_t>(e->id);
        // 如果启用了"选中时隐藏原图"且实体被选中，跳过
        if (hideSelected && selectedIds.count(uid))
        {
            continue;
        }
        desired.insert(uid);
    }

    // 移除：本地账本中存在但场景已不期望（删除 / 隐藏 / 图层隐藏）
    for (auto it = m_worldTextIds.begin(); it != m_worldTextIds.end();)
    {
        if (!desired.count(*it))
        {
            m_renderWidget->removeWorldText(*it);
            it = m_worldTextIds.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 新增/更新：期望但尚未登记 → 登记；已登记但本轮 dirty（内容/位置/字高变化）→ 重登记
    for (auto id : desired)
    {
        const bool isNew = m_worldTextIds.count(id) == 0;
        const bool isDirty = m_pendingDirtyIds.count(static_cast<Eg::EntityId>(id)) > 0;
        if (!isNew && !isDirty)
        {
            continue;
        }

        auto* e = sm->findEntityById(static_cast<Eg::EntityId>(id));
        if (!e)
        {
            continue;
        }

        // 空串或非正字高画不出任何东西，不纳入账本（移除残留）
        const auto* text = static_cast<const Eg::SyText*>(e);
        if (text->text()[0] == '\0' || text->dHeight <= 0.0)
        {
            m_renderWidget->removeWorldText(id);
            m_worldTextIds.erase(id);
            continue;
        }

        m_renderWidget->setWorldText(id, text);
        m_worldTextIds.insert(id);
    }
}

// 渲染刷新分发：按 RefreshLevel 级别选择刷新策略
void SceneRefreshCoordinator::updateSceneRender()
{
    if (!m_renderWidget || (m_refreshLevel == RefreshLevel::None && m_curveLodQueue.empty()))
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

    auto* sm = m_sceneManager;
    if (!sm)
    {
        return;
    }

    /// 本帧是否真的跑过一条场景级刷新路径（决定要不要清账）
    bool consumed = false;

    // 全量刷新时才清空曲线 LOD 队列：全量重建会以最新缩放重新离散所有图元，
    // 此时 LOD 队列里的旧任务确实已过期。
    // 增量刷新（LightUpdate / Selection / Repaint）只更新脏图元，未变更的曲线图元
    // 仍保持旧 LOD 精度，需要保留队列继续分批重建。否则会出现"部分曲线永久低精度"——
    // 因为 m_curveLodScaleAtBuild 已被更新为新缩放值，后续不会再触发 LOD 重建。
    if (m_refreshLevel == RefreshLevel::FullRefresh && !m_curveLodQueue.empty())
    {
        m_curveLodQueue.clear();
        m_curveLodCursor = 0;
    }

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    if (level == RefreshLevel::Repaint)
    {
        applyRepaintRefresh();
        // 曲线 LOD 与场景级刷新解耦：Repaint 也要继续推进队列，否则这一帧的
        // 定时器被 Repaint 消费掉、processCurveLodBatch 又没跑，队列就永久搁浅。
        if (!m_curveLodQueue.empty())
        {
            processCurveLodBatch();
        }
        return;
    }

    // 全量优先，且两条路径互斥。
    //
    // 曾经这里是「先判 needApplyLight 跑增量，再判 level >= FullRefresh 跑全量」两个
    // 独立的 if：当 level == FullRefresh 且有待删图元时两者同时命中，于是先逐个
    // remove/upsert 一遍，紧接着 submitSceneFromDataSource 把成果整体丢弃。
    // 全量重建以场景为真源，被删掉的图元本来就不会出现在重建结果里
    // （reconcile* 的 fullReconcile 分支同样是清空后整体重传），不需要增量兜底。
    if (level >= RefreshLevel::FullRefresh)
    {
        applyFullRefresh(sm);
        consumed = true;
    }
    // LightUpdate / Selection 增量刷新：
    // 删除图元（m_pendingDeletedIds）必须被处理，否则会从渲染世界中被永久遗漏
    // （典型症状：删除后视图不更新）。Selection 级别的样式变更同样由 applyLightRefresh
    // 覆盖——其内部已对脏图元调用 modifyRenderEntity。
    // 注意：onSelectionChanged 会先把 m_refreshLevel 提升为 Selection(3)，若紧随其后发生场景删除
    // （onSceneChanged），scheduleSceneUpdate 不会将其降级回 LightUpdate(2)，因此这里必须显式包含
    // Selection 级别，否则待删除图元会在 Selection 分支中被跳过。
    else if ((level == RefreshLevel::LightUpdate) || (level == RefreshLevel::Selection) || !m_pendingDeletedIds.empty())
    {
        applyLightRefresh(sm);
        consumed = true;
    }

    // 只在真的跑过一次场景级刷新时清账：否则「纯 LOD 批」的那一帧（level == None）
    // 会把还没被消费的脏/删除集合与快照一起丢掉。
    if (consumed)
    {
        m_pendingDirtyIds.clear();
        m_pendingDeletedIds.clear();
        // 本批快照已消费完：清掉条目，避免把整份 clone 副本一直挂在内存里。
        // 修订号保留在游标上（m_lastCursor），下批快照从新修订号继续累积。
        m_pendingSnapshot.reset(m_lastCursor);
    }

    // 曲线 LOD 分批：始终推进，不受本次场景级刷新影响。
    // 曾经的写法是「仅当 m_refreshLevel == None 时才处理 LOD 且随即 return」，
    // 于是任何一次 LightUpdate/Selection/FullRefresh 都会把这一帧的（单次触发的）
    // 节流定时器消费掉而不重启 —— 队列随即永久搁浅，放大后曲线一直停在低精度，
    // 只有等到下一次全量重建才恢复（正是「选图元移动后才恢复精度，连视口外的也恢复」）。
    if (!m_curveLodQueue.empty())
    {
        processCurveLodBatch();
    }
}