/**
 * @file SceneRefreshCoordinator.cpp
 * @brief 场景刷新协调器实现 — 四级刷新策略与增量渲染管线
 *
 */
#include "SceneRefreshCoordinator.h"
#include "RenderWidget.h"

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

    // 曲线 LOD 独立消费定时器：32ms 单次触发，每次处理一批后若还有剩余则自行续跑。
    // 与 m_sceneUpdateTimer 解耦，确保场景操作密集时 LOD 分批仍能稳定推进。
    m_curveLodTimer = new QTimer(this);
    m_curveLodTimer->setSingleShot(true);
    m_curveLodTimer->setInterval(32);
    connect(m_curveLodTimer, &QTimer::timeout, this, &SceneRefreshCoordinator::onCurveLodTimer);
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
    // 拆分出的三个辅助类各自持有 widget 指针，同步更新
    m_bitmapReconciler.setRenderWidget(widget);
    m_textReconciler.setRenderWidget(widget);
    m_curveLodManager.setRenderWidget(widget);
    // 反向注入：视口内部的覆盖层 / 位图 / 文字 / 视图矩阵这几类刷新
    // 原来是裸调 update() 完全绕过调度器的，注入后统一经 requestRepaint() 汇入。
    // 这是「所有 2D 刷新来源都能从 pendingLevel() 推理」的前提。
    if (m_renderWidget)
    {
        m_renderWidget->setRefreshScheduler(this);
    }
}

void SceneRefreshCoordinator::setSceneContext(Eg::ISceneContext* ctx)
{
    // 观察者注册收敛 — 切换场景时自动注销旧观察者、注册新观察者
    // 观察者模式是 2D 特有扩展；2D 路径唯一实现是 SceneManager，
    // 全局 -fvisibility=hidden 下 dynamic_cast 跨 DLL 不可靠，用 static_cast。
    if (m_sceneManager)
    {
        m_sceneManager->removeObserver(this);
    }
    m_sceneManager = static_cast<Eg::SceneManager*>(ctx);
    m_curveLodManager.setSceneManager(m_sceneManager);
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
    if (m_curveLodTimer)
    {
        m_curveLodTimer->stop();
    }
    // 观察者注销 — 在 stop() 中统一处理，避免析构时 SceneManager 已销毁（UAF）
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
    // 拖动中强制显示选中图元，忽略隐藏设置
    if (m_forceShowSelectedDuringDrag)
    {
        return false;
    }

    // 开关 + 「虚线此刻真的画得出来」二者必须同时成立，理由见头文件。
    return m_renderWidget != nullptr && m_renderWidget->hideSelectedOriginalEffective();
}

void SceneRefreshCoordinator::setForceShowSelectedDuringDrag(bool forceShow)
{
    if (m_forceShowSelectedDuringDrag != forceShow)
    {
        m_forceShowSelectedDuringDrag = forceShow;
        // 状态变化后需要刷新场景以应用新的隐藏/显示逻辑
        scheduleSceneUpdate();
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

// 曲线 LOD 消费泵：启动独立的 LOD 定时器，与场景更新定时器完全解耦。
// 场景操作频繁时两个定时器互不干扰，LOD 分批能稳定每 32ms 推进一次。
void SceneRefreshCoordinator::ensureCurveLodPump()
{
    if (m_curveLodTimer && !m_curveLodTimer->isActive())
    {
        m_curveLodTimer->start();
    }
    else if (!m_curveLodTimer)
    {
        processCurveLodBatch();
    }
}

// LOD 定时器 slot：执行一批，队列未空则自动续跑
void SceneRefreshCoordinator::onCurveLodTimer()
{
    if (m_curveLodManager.empty())
    {
        m_curveLodManager.finishQueue();
        return;
    }

    processCurveLodBatch();
}

void SceneRefreshCoordinator::finishCurveLodQueue()
{
    m_curveLodManager.finishQueue();
    // finishQueue 内部会处理「排队期间又要求重新收集」的待办（含重收集 + 续跑判断），
    // 这里只需在重收集出队列后把泵拉起来。
    if (!m_curveLodManager.empty())
    {
        ensureCurveLodPump();
    }
}

void SceneRefreshCoordinator::requestCurveLodRefresh()
{
    if (!m_sceneManager || !m_renderWidget)
    {
        return;
    }

    // 过期判据与队列收集的语义已整体移交 CurveLodManager。
    // 队列未排空时它只记待办不打断批次；排空后由 finishCurveLodQueue 重新评估。
    m_curveLodManager.requestRefresh();
    if (!m_curveLodManager.empty())
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
                // 判据是「图元**此刻**还在不在场景里」，而不是 kinds 里的 Removed 位。
                //
                // 撤销删除的路径是「先 Removed（删除）、再 Added（撤销归还）」，
                // 只信 Removed 位会把刚回到场景的图元判成已删除并 removeRenderEntity() ——
                // 症状是图元在场景树里、能被选中、数据完好，但画面上永远不出现。
                // mergeSceneChangeKind 已保证掩码不会自相矛盾，这里再按活对象复核一次：
                // 两道防线互为兜底（掩码可能因变更日志被截断而丢失上下文）。
                const bool stillInScene = m_sceneManager->findEntityById(change.entityId) != nullptr;
                if (!stillInScene)
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
    // 观察者注册收敛 — 发射信号供视口同步工具状态
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
    // 无需创建 BatchGuard（GL 上下文切换）、无需 BitmapReconciler/TextReconciler 对账。
    // 选择轮廓/手柄已由 onSelectionChanged → syncSelectionToolState 直接更新到 GPU，
    // 此处只需触发一次 repaint 即可。
    if (m_pendingDirtyIds.empty() && m_pendingDeletedIds.empty() && m_selectionHideIds.empty() &&
        m_selectionRestoreIds.empty())
    {
        m_renderWidget->update();
        return;
    }

    // 删除图元：从渲染世界与账本中摘除。
    //
    // 删除的图元不会出现在快照里（captureSnapshot 只抓活对象），所以不能混进
    // 下面的脏集合装配，必须在消费快照之前按键显式移除。否则本轮的
    // m_pendingDeletedIds 会在 updateSceneRender 末尾被清空，而 GPU 上的旧几何
    // 从未被摘除 —— 画面会一直显示已删除的图元，直到下一次全量重建才消失
    // （典型症状：删除后场景树已更新，视口却仍有残影）。
    //
    // 同一批次里既脏又删的以删除为准：先清掉脏集合/选中差集里的同 ID 条目，
    // 避免下面的装配或「取消选中恢复本体」把已删除的图元重新提交上去。
    if (!m_pendingDeletedIds.empty())
    {
        std::vector<uint64_t> removedIds;
        removedIds.reserve(m_pendingDeletedIds.size());
        for (auto id : m_pendingDeletedIds)
        {
            const auto uid = static_cast<uint64_t>(id);
            m_pendingDirtyIds.erase(id);
            m_selectionHideIds.erase(uid);
            m_selectionRestoreIds.erase(uid);
            m_renderedEntityIds.erase(uid);
            removedIds.push_back(uid);
        }
        m_renderWidget->removeRenderEntities(removedIds);
        m_pendingDeletedIds.clear();
    }

    // 增量装配：调用方只给「哪些图元变了」，离散化与内容比对都在
    // RenderSceneBuilder 内完成（与全量路径同一份公式），不再由本类
    // 逐图元算出顶点数组后往几何仓里塞。
    //
    // 图元来源是主线程 clone 好的快照：entityFor 只在主线程调用，
    // builder 内部的 emit* 回调同步完成离散化，不涉及其它线程。
    std::vector<uint64_t> entityIdsToProcess;
    entityIdsToProcess.reserve(m_pendingDirtyIds.size() + m_selectionRestoreIds.size());

    // 本轮脏集合里是否出现过位图 / 文字图元。下面用它决定要不要跑 reconcile*：
    // BitmapReconciler/TextReconciler 现在使用 getEntitiesByType 按类型索引获取，
    // 避免全场景扫描。标志在这个循环里顺手收集，不额外多做一次 findEntityById。
    bool touchedImage = false;
    bool touchedText = false;

    for (auto id : m_pendingDirtyIds)
    {
        // 读快照而不是活对象：快照副本是主线程 clone 好的
        const Eg::RenderEntitySnapshot* snapshot = m_pendingSnapshot.find(id);
        if (snapshot == nullptr || snapshot->entity() == nullptr)
        {
            continue;
        }

        auto uid = static_cast<uint64_t>(id);

        // 图元可见性 = 自身可见 且 所在图层可见。图层指针不随 clone 复制，
        // 这个判断在主线程上做。
        if (!snapshot->effectiveVisible())
        {
            // 图元不可见：从渲染中移除（如果之前有渲染的话）
            if (m_renderedEntityIds.count(uid))
            {
                m_renderWidget->removeRenderEntities({ uid });
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
                m_renderWidget->removeRenderEntities({ uid });
                m_renderedEntityIds.erase(uid);
            }
            continue;
        }

        // 位图（SyImage）不走折线/线框顶点路径，统一由 BitmapReconciler 处理
        if (snapshot->type == Eg::EType::IMAGE)
        {
            touchedImage = true;
            continue;
        }

        // 文本（SyText）同理：字形四边形由 TextReconciler 一路处理。
        if (snapshot->type == Eg::EType::TEXT)
        {
            touchedText = true;
            continue;
        }

        entityIdsToProcess.push_back(uid);
    }

    // 「隐藏选中本体」取消选中的那批：把本体重新加回世界层。
    // 没有快照可用（选择变更不进变更流），这里在主线程直接读活对象 ——
    // 装配入口与上面完全同一条（都走 sink），结果与全量重建一致。
    // 位图/文字不走这条顶点路径，交给下面的 reconcile*。
    if (!m_selectionRestoreIds.empty())
    {
        for (uint64_t uid : m_selectionRestoreIds)
        {
            // 此刻仍该被摘的（仍然被选中且隐藏有效）不动它 —— 与 hide 侧同一个判据，
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

            entityIdsToProcess.push_back(uid);
        }
        m_selectionRestoreIds.clear();
    }

    // 一次装配全部脏图元：上下文只切一次，重绘只请求一次
    // （对比旧实现逐图元一对 makeCurrent/update）。
    if (!entityIdsToProcess.empty())
    {
        m_renderWidget->upsertRenderEntities(entityIdsToProcess,
            [this, sm](uint64_t uid) -> const Eg::SyEntity* {
                // 先查本轮快照（clone 副本，主线程可读）；不在快照里再查活对象
                // （恢复本体的那批没有快照）。
                const Eg::RenderEntitySnapshot* snapshot = m_pendingSnapshot.find(
                    static_cast<Eg::EntityId>(uid));
                if (snapshot != nullptr && snapshot->entity() != nullptr)
                {
                    return snapshot->entity();
                }
                return sm->findEntityById(static_cast<Eg::EntityId>(uid));
            },
            [this, sm](uint64_t uid) -> uint32_t {
                return layerIndexOfEntity(sm, static_cast<Eg::EntityId>(uid));
            });

        for (uint64_t uid : entityIdsToProcess)
        {
            m_renderedEntityIds.insert(uid);
        }
    }

    // 位图层协调：以场景为真源，增量处理新增/修改/删除/图层显隐。
    //
    // 两个短路条件是「或」而不是「与」，缺哪个都会漏画：
    //   - touchedImage：本轮有位图变脏/新增，必须对账；
    //   - 账本非空：图元或图层转为不可见**不会**进脏集合（LayerManager::setLayerVisible
    //     只改标志位），只能靠这里的全量对账发现。账本非空时跳过就会留下残影。
    // 两者都不成立时（账本空 && 本轮没碰位图）对账必然是空转，可以安全跳过。
    if (touchedImage || !m_bitmapReconciler.empty())
    {
        m_bitmapReconciler.reconcile(sm, /*fullReconcile=*/false, m_cachedSelectedIds, hideSelected,
            m_pendingDirtyIds);
    }

    // 世界文字层协调：同上。文本自己一路后，增量刷新不再因文本升级为全量。
    if (touchedText || !m_textReconciler.empty())
    {
        m_textReconciler.reconcile(sm, /*fullReconcile=*/false, m_cachedSelectedIds, hideSelected,
            m_pendingDirtyIds);
    }
}

void SceneRefreshCoordinator::processCurveLodBatch()
{
    // 批量重建 + GL 上下文管理 + 账本写回的语义已整体移交 CurveLodManager。
    // 它内部会在有剩余时通过 ensureCurveLodPump 续跑独立定时器。
    m_curveLodManager.processBatch(m_renderedEntityIds);
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

    // 账本必须与 GPU 实际内容严格一致。视口裁剪生效时，gatherGeometry 只提交了
    // 视口内图元；视口外图元已被 RenderSceneBuilder::endRebuild 回收。
    // 因此账本只记视口内已提交的图元：视口外图元不入账，下次它们出现在脏集合时，
    // 增量路径会走 addRenderEntity 补传，而不是误用 modifyRenderEntity（找不到 GPU 块）。
    Ut::BBox2d accountBox;
    bool hasAccountBox = false;
    {
        float bounds[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (m_renderWidget->visibleWorldBounds(bounds))
        {
            constexpr double kCullMargin = 1.25;
            const double cx = 0.5 * (static_cast<double>(bounds[0]) + bounds[2]);
            const double cy = 0.5 * (static_cast<double>(bounds[1]) + bounds[3]);
            const double hw = 0.5 * (bounds[2] - bounds[0]) * kCullMargin;
            const double hh = 0.5 * (bounds[3] - bounds[1]) * kCullMargin;
            accountBox = Ut::BBox2d(cx - hw, cy - hh, cx + hw, cy + hh);
            hasAccountBox = true;
        }
    }

    // 过滤条件与 gatherGeometry（SceneManager::gatherGeometry）严格对齐：
    // 几何非法 / 无图层 / 图层隐藏 / 本体隐藏 / bbox 无效 / 视口裁剪的图元都不会上 GPU，也就不入账。
    // 旧实现「layer==null 也入账、bbox 无效也入账」会把 GPU 上不存在的图元记进账本，
    // 增量路径对账时可能误走 modifyRenderEntity。
    // isValid() 必须与空间索引 isIndexableEntity 一致：非法几何不在索引里，
    // 若 GPU 仍渲染（旧 gatherGeometry 不查 isValid），queryByBox 账本会漏记。
    auto accountEntity = [this, hideSelected, &selectedIds](Eg::SyEntity* e) {
        if (!e || !e->visible())
            return;
        if (!e->isValid())
            return;
        const Eg::SyLayer* layer = e->layer();
        if (!layer || !layer->isVisible())
            return;
        if (!e->getBbox().isValid())
            return;
        auto uid = static_cast<uint64_t>(e->id);
        if (hideSelected && selectedIds.count(uid))
            return;  // 隐藏选中图元已从 GPU 移除，不入账
        m_renderedEntityIds.insert(uid);
    };

    if (hasAccountBox)
    {
        // 视口裁剪走空间索引：RTree 范围查询 O(log n + k)，避免全场景 O(N) 扫描。
        // queryByBox 用索引缓存的 AABB 判相交（查询前 flush 待修索引），与
        // gatherGeometry 的 getBbox()+intersects 等价；无效 bbox 图元不在索引里，天然跳过。
        const Eg::VecSyEntityPtr candidates = sm->queryByBox(accountBox, /*containedOnly=*/false);
        for (Eg::SyEntity* e : candidates)
        {
            accountEntity(e);
        }
    }
    else
    {
        // 矩阵退化拿不到视口盒时退化为全量遍历（与 gatherGeometry 无裁剪分支一致）
        sm->forEachEntity(accountEntity);
    }

    // "选中时隐藏原图"：上面的 submitSceneFromDataSource 已把全部图元（含选中）上传到 GPU，
    // 这里把选中图元的本体从 GPU 移除，使画面只剩流水虚线轮廓。账本已在上面循环中排除它们，
    // GPU 与账本因此保持一致。
    if (hideSelected)
    {
        std::vector<uint64_t> toRemove(selectedIds.begin(), selectedIds.end());
        m_renderWidget->removeRenderEntities(toRemove);
    }

    // 全量重建已按当前选中集把本体的增删做到位，待处理的选中差集就此过期：
    // 留着会在下一轮增量里多摘/多加一次（比如上一轮的"取消选中要加回来"撞上本次重建的"应摘除"）。
    m_selectionHideIds.clear();
    m_selectionRestoreIds.clear();

    // 全量重建还把整份场景按当前缩放重新离散了一遍，曲线 LOD 的「已收集区域」记录
    // 就此作废：重新锚定缩放基准（否则下一帧拿旧基准比较会白白重收集一次），
    // 并把区域记录置为无效 —— 全场景都已是当前精度，平移不需要再补收集。
    const float pxToWorldNow = m_renderWidget->pixelToWorldScale();
    m_curveLodManager.clearAfterFullRefresh(
        pxToWorldNow > 0.0f ? 1.0 / static_cast<double>(pxToWorldNow) : 0.0);

    // 位图层全量协调：submitSceneFromDataSource 内部 renderBeginScene 已清空 GPU 位图，
    // 这里以场景为真源整体重建，保证与场景生命周期完全一致
    m_bitmapReconciler.reconcile(sm, /*fullReconcile=*/true, m_cachedSelectedIds, hideSelected, {});

    // 世界文字层全量协调：同上
    m_textReconciler.reconcile(sm, /*fullReconcile=*/true, m_cachedSelectedIds, hideSelected, {});
}

// 渲染刷新分发：按 RefreshLevel 级别选择刷新策略
void SceneRefreshCoordinator::updateSceneRender()
{
    if (!m_renderWidget || (m_refreshLevel == RefreshLevel::None && m_curveLodManager.empty()))
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
    // 因为 CurveLodManager 的缩放基准已被更新为新缩放值，后续不会再触发 LOD 重建。
    // 注意：applyFullRefresh 内部已按当前缩放重新锚定基准，这里只需在未走到全量
    // 路径时兜底清空（例如全量被 isInitialized 提前返回的那一帧）。
    if (m_refreshLevel == RefreshLevel::FullRefresh && !m_curveLodManager.empty())
    {
        m_curveLodManager.clearQueue();
    }

    RefreshLevel level = m_refreshLevel;
    m_refreshLevel = RefreshLevel::None;

    if (level == RefreshLevel::Repaint)
    {
        applyRepaintRefresh();
        // 场景刷新帧顺带推进 LOD 一次，独立定时器负责持续续跑
        if (!m_curveLodManager.empty())
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
    // 覆盖——其内部已对脏图元走一遍 sink 装配。
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

    // 曲线 LOD 分批：场景刷新帧顺带推进一次。
    // LOD 的持续推进主要由独立的 m_curveLodTimer（32ms）承担；
    // 这里额外推进一次是为了利用"场景刷新已经切过 GL 上下文"的时机，
    // 减少单独触发时的上下文切换开销。processCurveLodBatch 内部有剩余时
    // 会自行调用 ensureCurveLodPump 续跑独立定时器。
    if (!m_curveLodManager.empty())
    {
        processCurveLodBatch();
    }
}
