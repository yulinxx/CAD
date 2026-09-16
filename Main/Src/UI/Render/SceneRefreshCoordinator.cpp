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
#include "Engine/SyEntity/EType.h"
#include "Engine/Parallel/EngineParallel.h"


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

void SceneRefreshCoordinator::requestCurveLodRefresh()
{
    if (!m_sceneManager || !m_renderWidget)
    {
        return;
    }

    // 收集曲线类图元（圆/弧/椭圆）ID：只有它们的离散化段数随缩放变化。
    m_curveLodQueue.clear();
    m_curveLodCursor = 0;

    const Eg::EType curveTypes[] = { Eg::EType::CIRCLE, Eg::EType::ARC, Eg::EType::ELLIPSE };
    for (Eg::EType type : curveTypes)
    {
        const Eg::VecSyEntityPtr entities = m_sceneManager->getEntitiesByType(type);
        for (const Eg::SyEntity* entity : entities)
        {
            if (entity)
            {
                m_curveLodQueue.push_back(static_cast<uint64_t>(entity->id));
            }
        }
    }

    if (!m_curveLodQueue.empty())
    {
        // 只启动定时器、不提升 RefreshLevel：曲线 LOD 是独立的后台分批任务，
        // 由 updateSceneRender 在无场景级刷新时逐帧消费。
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        {
            m_sceneUpdateTimer->start();
        }
        else if (!m_sceneUpdateTimer)
        {
            processCurveLodBatch();
        }
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

        m_lastSelectedIds = std::move(currentSelected);
    }

    // 这里判的是「这次刷新要做到什么级别」，因此看的是**有效**隐藏状态
    // （开关 && 虚线画得出来），而不是开关本身：
    //
    //   开关关着 / 开关开着但轮廓画不出来（选中集超预算）
    //       → 本体根本没被摘，选择集变化不动世界层 → 廉价的 Selection 级就够；
    //   开关开着且轮廓可画
    //       → 选中本体必须从世界层摘掉，选择集变了就得全量重建；
    //   有效状态本身翻转（上次摘了、这次不该摘；或反过来）
    //       → 必须全量重建才能把上次摘掉的本体加回来。
    //
    // 加 m_lastHideSelectedEffective（上次刷新**实际**生效的隐藏状态）就是为了第三种：
    // 只看当前值的话，「5 选（已摘本体）→ 3000 选（超预算，不该摘）」会被判成
    // 「不需要重建」，那 5 个本体就永远回不来了。
    //
    // 反过来说，之前只看开关时，第三种情形每点选一次都要付一次 O(场景) 全量重建，
    // 而画面其实毫无变化（本体本就没摘）——这是 70 万图元场景里可感知的白做。
    const bool hideNow = m_renderWidget != nullptr && m_renderWidget->hideSelectedOriginalEffective();
    if (selectionSetChanged && (hideNow || hideNow != m_lastHideSelectedEffective))
    {
        // 选中本体的增删只能靠世界层全量重建
        m_refreshLevel = RefreshLevel::FullRefresh;
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

    // 本轮脏集合里是否出现过位图 / 文字图元。下面用它决定要不要跑 reconcile*：
    // reconcileBitmaps/reconcileTexts 现在使用 getEntitiesByType 按类型索引获取，
    // 避免全场景扫描。标志在这个循环里顺手收集，不额外多做一次 findEntityById。
    bool touchedImage = false;
    bool touchedText = false;

    // 获取"选中时隐藏原图"的**有效**设置（开关 + 虚线画得出来），见 hideSelectedEffective()
    const bool hideSelected = hideSelectedEffective();
    // 记下本次实际生效的隐藏状态：onSelectionChanged 判断「这次选择集变化要不要全量重建」
    // 时要用它识别「有效状态翻转」（见该处的注释）
    m_lastHideSelectedEffective = hideSelected;

    // 如果启用隐藏选中实体，获取当前选中的实体ID集合
    std::unordered_set<uint64_t> selectedIds;
    if (hideSelected)
    {
        const auto selectedEntities = sm->getSelectedEntities();
        for (const auto* e : selectedEntities)
        {
            if (e)
            {
                selectedIds.insert(static_cast<uint64_t>(e->id));
            }
        }
    }

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
            const Eg::RenderEntitySnapshot* snapshot =
                m_pendingSnapshot.find(static_cast<Eg::EntityId>(id));
            if (snapshot == nullptr || snapshot->entity() == nullptr)
            {
                return;
            }

            RenderJob& job = jobs[index];
            job.uid = id;
            job.layerOrder = entityLayerOrders[index];

            if (entityToVertices(snapshot->entity(), job.vertices, job.primType, cameraCenter,
                                 worldToScreenScale, chordErrorPixels))
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
                m_renderWidget->modifyRenderEntity(job.uid, job.vertices.data(),
                    static_cast<uint32_t>(job.vertices.size()), job.primType, job.layerOrder);
            }
            else
            {
                m_renderWidget->addRenderEntity(job.uid, job.vertices.data(),
                    static_cast<uint32_t>(job.vertices.size()), job.primType, job.layerOrder);
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
            const Eg::RenderEntitySnapshot* snapshot =
                m_pendingSnapshot.find(static_cast<Eg::EntityId>(uid));
            if (snapshot == nullptr || snapshot->entity() == nullptr)
            {
                continue;
            }

            std::vector<Render::VertexP3C3> vertices;
            Render::PrimitiveType primType;
            // 图元无法增量转换为顶点，已跳过
            if (!entityToVertices(snapshot->entity(), vertices, primType, cameraCenter,
                                  worldToScreenScale, chordErrorPixels))
            {
                SY_WARNF("[SceneRefreshCoordinator] Entity %llu (eType=%d) cannot be incrementally converted to vertices, skipped",
                    static_cast<unsigned long long>(uid), static_cast<int>(snapshot->type));
                continue;
            }

            if (m_renderedEntityIds.count(uid))
            {
                m_renderWidget->modifyRenderEntity(uid, vertices.data(),
                    static_cast<uint32_t>(vertices.size()), primType, entityLayerOrders[index]);
            }
            else
            {
                m_renderWidget->addRenderEntity(uid, vertices.data(),
                    static_cast<uint32_t>(vertices.size()), primType, entityLayerOrders[index]);
                m_renderedEntityIds.insert(uid);
            }
        }
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
        m_curveLodQueue.clear();
        m_curveLodCursor = 0;
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
            m_renderWidget->modifyRenderEntity(uid, vertices.data(),
                static_cast<uint32_t>(vertices.size()), primType, layerIndexOfEntity(m_sceneManager, entity->id));
        }
        else
        {
            m_renderWidget->addRenderEntity(uid, vertices.data(),
                static_cast<uint32_t>(vertices.size()), primType, layerIndexOfEntity(m_sceneManager, entity->id));
            m_renderedEntityIds.insert(uid);
        }
    }

    m_renderWidget->endBatchUpload();

    m_curveLodCursor = end;

    if (m_curveLodCursor < m_curveLodQueue.size())
    {
        // 还有剩余，下一帧继续（不提升 RefreshLevel，仍是纯后台分批）
        if (m_sceneUpdateTimer && !m_sceneUpdateTimer->isActive())
        {
            m_sceneUpdateTimer->start();
        }
    }
    else
    {
        m_curveLodQueue.clear();
        m_curveLodCursor = 0;
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

    // 如果启用隐藏选中实体，获取当前选中的实体ID集合
    std::unordered_set<uint64_t> selectedIds;
    if (hideSelected)
    {
        const auto selectedEntities = sm->getSelectedEntities();
        for (const auto* e : selectedEntities)
        {
            if (e)
            {
                selectedIds.insert(static_cast<uint64_t>(e->id));
            }
        }
    }

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

    // 如果启用隐藏选中实体，获取当前选中的实体ID集合
    std::unordered_set<uint64_t> selectedIds;
    if (hideSelected)
    {
        const auto selectedEntities = sm->getSelectedEntities();
        for (const auto* e : selectedEntities)
        {
            if (e)
            {
                selectedIds.insert(static_cast<uint64_t>(e->id));
            }
        }
    }

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

    // 如果启用隐藏选中实体，获取当前选中的实体ID集合
    std::unordered_set<uint64_t> selectedIds;
    if (hideSelected)
    {
        const auto selectedEntities = sm->getSelectedEntities();
        for (const auto* e : selectedEntities)
        {
            if (e)
            {
                selectedIds.insert(static_cast<uint64_t>(e->id));
            }
        }
    }

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

    // 场景级刷新优先于曲线 LOD：全量/增量会以最新缩放重建，曲线队列的旧任务已过期
    if (m_refreshLevel != RefreshLevel::None && !m_curveLodQueue.empty())
    {
        m_curveLodQueue.clear();
        m_curveLodCursor = 0;
    }

    // 无场景级刷新时，处理曲线 LOD 分批重建（每帧一批，独立于 RefreshLevel）
    if (m_refreshLevel == RefreshLevel::None && !m_curveLodQueue.empty())
    {
        processCurveLodBatch();
        return;
    }

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    if (level == RefreshLevel::Repaint)
    {
        applyRepaintRefresh();
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
    }
    // LightUpdate / Selection 增量刷新：
    // 删除图元（m_pendingDeletedIds）必须被处理，否则会从渲染世界中被永久遗漏
    // （典型症状：删除后视图不更新）。Selection 级别的样式变更同样由 applyLightRefresh
    // 覆盖——其内部已对脏图元调用 modifyRenderEntity。
    // 注意：onSelectionChanged 会先把 m_refreshLevel 提升为 Selection(3)，若紧随其后发生场景删除
    // （onSceneChanged），scheduleSceneUpdate 不会将其降级回 LightUpdate(2)，因此这里必须显式包含
    // Selection 级别，否则待删除图元会在 Selection 分支中被跳过。
    else if ((level == RefreshLevel::LightUpdate) || (level == RefreshLevel::Selection)
        || !m_pendingDeletedIds.empty())
    {
        applyLightRefresh(sm);
    }

    m_pendingDirtyIds.clear();
    m_pendingDeletedIds.clear();
    // 本批快照已消费完：清掉条目，避免把整份 clone 副本一直挂在内存里。
    // 修订号保留在游标上（m_lastCursor），下批快照从新修订号继续累积。
    m_pendingSnapshot.reset(m_lastCursor);
}