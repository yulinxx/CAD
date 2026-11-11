#include "RenderSubmitScheduler.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/AprEntity/AprEntity.h"
#include "Engine/Layer/AprLayer.h"
#include "Engine/AprEntity/EType.h"
#include "Engine/Render/TessParams.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"

#include <algorithm>
#include <chrono>

namespace
{
    /// 取图元所属图层的绘制序号（0 = 最上层），供增量路径构造 z-order。
    ///
    /// 增量路径的图元快照是 clone，**不复制图层指针**，所以只能查活对象。
    /// 只在主线程调用（SceneManager 的契约是只有主线程可以访问）；
    /// 并行离散化那条路径必须提前在主线程把本值取好带进 job。
    uint32_t layerIndexOfEntityForLod(Eg::SceneManager* sm, Eg::EntityId id)
    {
        if (sm == nullptr)
        {
            return 0;
        }
        const Eg::AprEntity* entity = sm->findEntityById(id);
        const Eg::AprLayer* layer = entity != nullptr ? entity->layer() : nullptr;
        if (layer == nullptr || layer->getIndex() <= 0)
        {
            return 0;
        }
        return static_cast<uint32_t>(layer->getIndex());
    }
}  // namespace

RenderSubmitScheduler::RenderSubmitScheduler(RenderWidget* widget)
    : m_renderWidget(widget)
{
}

bool RenderSubmitScheduler::isRenderable(uint64_t uid, const Eg::AprEntity* entity) const
{
    if (entity == nullptr || !entity->visible())
    {
        return false;
    }
    // 与 emitEntityGeometry 的判据对齐：非法几何不会产生任何段，
    // 不能因为「提交过」就把 id 写进已渲染账本。
    if (!entity->isValid())
    {
        return false;
    }
    const Eg::AprLayer* layer = entity->layer();
    if (layer != nullptr && !layer->isVisible())
    {
        return false;
    }
    // 「选中时隐藏本体」：选中集里的图元本体不能由 LOD 批次重新提交，
    // 否则每次缩放触发 LOD 都会把刚摘掉的本体又画回来。
    // 谓词由协调器注入，每次实时读取最新开关与选中集，不持有状态副本。
    if (m_isSelectionHidden && m_isSelectionHidden(uid))
    {
        return false;
    }
    return true;
}

void RenderSubmitScheduler::requestRefresh()
{
    // 队列还有未完成的重建（cursor 未到末尾）：不重置、不重新收集。
    // processBatch 每帧从 renderWidget 读最新 worldToScreenScale，继续重建会
    // 自然用最新缩放。否则缩放连续触发时（滚轮/拖拽每帧都变），每次都 clear 队列 +
    // 重新收集 + 归零 cursor，重建进度永远被重置，放大后精度就一直停留在低档
    // （正是「放大后曲线仍糊」的根因）。
    // 但相机确实又变了，记一笔待办：队列排空时由 finishQueue 重新评估
    // （否则平移出上次收集区域后，新进视野的图元会一直停在旧精度）。
    if (m_cursor < m_queue.size())
    {
        m_recollectPending = true;
        ensurePump();
        return;
    }

    refillQueueIfStale();
    if (!m_queue.empty())
    {
        ensurePump();
    }
}

void RenderSubmitScheduler::processBatch(std::unordered_set<uint64_t>& renderedIds)
{
    if (!m_renderWidget || !m_sceneManager)
    {
        return;
    }

    if (m_extendCursor >= m_extendQueue.size() && m_cursor >= m_queue.size())
    {
        finishQueue();
        return;
    }

    // 「整批必然失败」的前置情形（GL 上下文不可用、渲染场景无效）：
    // 挂起队列 —— 不建批、不提交、不动游标，失败条目留在原位，恢复可用后
    // 自动续跑。只留 kPumpIntervalMs 低频探测（每 tick 一次谓词查询），
    // 不烧 CPU 也不会永久卡住：上下文恢复即继续；组件解绑则由 ensurePump
    // 的 null 检查彻底静默。
    if (!submitAvailable())
    {
        ensurePump();
        return;
    }

    // 连续失败指数退避：残余失败（几何仓写入失败、检查时可用提交时失败的
    // TOCTOU）按 2^(n-1) 个 tick 退避后原样重试，上限 kSubmitRetryBackoffMaxTicks。
    // 退避期间不建批不提交（失败条目由下方失败分支的游标回退留在原位），
    // 每 tick 只做一次递减，最迟约半秒后自动恢复重试 —— 既不空转烧 CPU，
    // 也不会把条目永久卡死或丢弃。
    if (m_retryBackoffTicks > 0)
    {
        --m_retryBackoffTicks;
        ensurePump();
        return;
    }

    ++m_processedBatches;

    // 自适应批次预算：固定 2000 个/帧不看图元复杂度 —— 轻场景浪费帧预算，
    // 重场景（万段样条/带 bulge 的多段线）单批可超几十毫秒，正是缩放卡顿来源。
    // 按上一批 upsertRenderEntities 的实测耗时反馈调节批量，目标压在
    // kBatchBudgetMs 预算内（批量上下限 kMinBatchSize/kMaxBatchSize）。
    // zoom 期间场景不变，直接在主线程读活对象（SceneManager 契约），无需 clone 快照。

    // 整批一次装配：离散化与内容比对都在 RenderSceneBuilder 内完成
    // （与全量路径同一份公式），上下文只切一次、重绘只请求一次。
    //
    // 收集之后到消费之前，图元可能已经被删除、隐藏（自身/图层）或被
    // 「选中时隐藏本体」过滤掉 —— LOD 的 upsert 不经过 applyLightRefresh 的
    // 可见性判断，如果原样提交会把它们重新画回世界层。这里在提交前按活对象
    // 再过滤一次，并只为真正提交的图元写账本。
    std::vector<uint64_t> batch;
    batch.reserve(m_adaptiveBatchSize);

    // 扩展队列优先：新进视野的图元从未上 GPU（画面缺块），比 LOD 精度更新紧急。
    // 两队列共用本批额度，扩展先填。
    //
    // 建批前的游标位置：提交失败时回退到此处，失败条目留在原队列原位置重试。
    // 回退不经过 refillQueueIfStale 的曲线类型过滤，LOD 与扩展两队列、
    // 曲线与非曲线图元都由同一条回退路径覆盖，不丢条目。
    const size_t extendBegin = m_extendCursor;
    const size_t lodBegin = m_cursor;
    for (; m_extendCursor < m_extendQueue.size() && batch.size() < m_adaptiveBatchSize; ++m_extendCursor)
    {
        const uint64_t uid = m_extendQueue[m_extendCursor];
        const Eg::AprEntity* entity = m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
        if (!isRenderable(uid, entity))
        {
            continue;
        }
        batch.push_back(uid);
    }
    for (; m_cursor < m_queue.size() && batch.size() < m_adaptiveBatchSize; ++m_cursor)
    {
        const uint64_t uid = m_queue[m_cursor];
        const Eg::AprEntity* entity = m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
        if (!isRenderable(uid, entity))
        {
            continue;
        }
        batch.push_back(uid);
    }

    if (!batch.empty())
    {
        const auto t0 = std::chrono::steady_clock::now();
        // 测试缝隙优先：注入了提交回调就走回调（无 GL 验证队列/账本语义），
        // 生产路径仍走 RenderWidget 的真实装配链路。
        const bool ok = m_submitBatch ? m_submitBatch(batch)
                                      : m_renderWidget->upsertRenderEntities(
                                            batch,
                                            [this](uint64_t uid) -> const Eg::AprEntity* {
                                                return m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
                                            },
                                            [this](uint64_t uid) -> uint32_t {
                                                return layerIndexOfEntityForLod(
                                                    m_sceneManager, static_cast<Eg::EntityId>(uid));
                                            });
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        if (ok)
        {
            // 耗时反馈：超预算减半（下限 kMinBatchSize），余量充足按 1.5 倍增长
            // （上限 kMaxBatchSize）。只在成功批上反馈 —— 失败批的耗时可能被
            // 提前失败截断、不代表整批成本，且批量变化会破坏「失败批次原样
            // 重试」的确定性（游标回退后重试批必须与失败批完全一致）。
            // 空批（全被过滤）不参与调节，避免误把「无可渲染项」当成「很轻」。
            if (elapsedMs > kBatchBudgetMs)
            {
                m_adaptiveBatchSize = (std::max)(kMinBatchSize, m_adaptiveBatchSize / 2);
            }
            else if (elapsedMs * 2.0 < kBatchBudgetMs)
            {
                m_adaptiveBatchSize = (std::min)(kMaxBatchSize, m_adaptiveBatchSize + m_adaptiveBatchSize / 2);
            }

            m_submitFailures = 0;
            m_retryBackoffTicks = 0;
            m_submitRetryPending = false;
            for (uint64_t uid : batch)
            {
                renderedIds.insert(uid);
            }
        }
        else
        {
            // 批次提交失败：游标回退到建批前位置，失败条目留在原队列原位置
            // 等待重试 —— 不写账本、不丢弃、不重置队列进度。
            // 旧实现在这里只记 recollectPending 就放游标走：refillQueueIfStale
            // 的「缩放/区域都没变就跳过」前置门让重收集空转，失败图元被
            // 永久丢弃（缺陷1 根因）。整批重试是幂等的：addPiece 按内容哈希
            // 短路，已写入的段原地跳过，不会重复上传。
            m_extendCursor = extendBegin;
            m_cursor = lodBegin;
            m_submitRetryPending = true;
            ++m_submitFailures;
            // 2^(n-1) 个 tick 指数退避，封顶 kSubmitRetryBackoffMaxTicks。
            // shift 先截到 8（2^8=256 > 上限）防大计数溢出。
            const uint32_t shift = (std::min)(m_submitFailures - 1, 8u);
            m_retryBackoffTicks =
                (std::min)(static_cast<uint32_t>(1u << shift), kSubmitRetryBackoffMaxTicks);
            ensurePump();
        }
    }

    // 扩展队列排空后回收内存；LOD 队列保留游标语义（finishQueue 统一清）
    if (m_extendCursor >= m_extendQueue.size())
    {
        m_extendQueue.clear();
        m_extendCursor = 0;
    }

    if (m_extendCursor < m_extendQueue.size() || m_cursor < m_queue.size())
    {
        // 还有剩余，下一帧继续（不提升 RefreshLevel，仍是纯后台分批）
        ensurePump();
    }
    else
    {
        finishQueue();
    }
}

void RenderSubmitScheduler::enqueueViewportExtension(const std::vector<uint64_t>& ids)
{
    if (ids.empty())
    {
        return;
    }
    // 与未消费的剩余项去重后再追加：对账时保留剩余项意味着同一 ID 可能被
    // 下一轮差分再次列出，重复入队会让它在批次里被反复重试，挤占真正缺块的图元。
    std::unordered_set<uint64_t> pending(m_extendQueue.begin() + static_cast<std::ptrdiff_t>(m_extendCursor),
        m_extendQueue.end());
    for (uint64_t uid : ids)
    {
        if (pending.insert(uid).second)
        {
            m_extendQueue.push_back(uid);
        }
    }
    ensurePump();
}

void RenderSubmitScheduler::finishQueue()
{
    m_queue.clear();
    m_cursor = 0;
    m_extendQueue.clear();
    m_extendCursor = 0;

    // 本轮队列还没消费完时相机可能又变了（连续缩放/平移）：那时 requestRefresh
    // 只记了待办、没打断进行中的批次。这里排空后立刻按当时的状态重新评估一次，
    // 否则「平移出上次收集区域」的新视野会一直停在旧精度（视口裁剪必须配这一步）。
    if (m_recollectPending)
    {
        m_recollectPending = false;
        refillQueueIfStale();
        if (!m_queue.empty())
        {
            ensurePump();
        }
    }
}

void RenderSubmitScheduler::clearAfterFullRefresh(double worldToScreenScale)
{
    // 全量重建已按当前缩放重新离散了整份场景：清队列、锚定缩放基准，
    // 并置区域记录为无效 —— 全场景都已是当前精度，平移不需要再补收集。
    // 扩展队列同样作废：全量重建已把账本内全部图元重新上传。
    m_queue.clear();
    m_cursor = 0;
    m_extendQueue.clear();
    m_extendCursor = 0;
    m_recollectPending = false;
    m_scaleAtBuild = worldToScreenScale;
    m_collectedBoxValid = false;
    // 全量重建已把全部图元重新上传：未补交的失败条目被取代，重试状态作废
    m_submitRetryPending = false;
    m_submitFailures = 0;
    m_retryBackoffTicks = 0;
}

void RenderSubmitScheduler::clearQueue()
{
    m_queue.clear();
    m_cursor = 0;
    m_extendQueue.clear();
    m_extendCursor = 0;
    m_recollectPending = false;
    // 队列被整体清空（全量刷新前置兜底）：重试状态一并作废
    m_submitRetryPending = false;
    m_submitFailures = 0;
    m_retryBackoffTicks = 0;
}

void RenderSubmitScheduler::markViewportReconciled(const Ut::BBox2d& coveredBox)
{
    // 视口范围已增量对账：旧 LOD 队列是按上一个可视盒收集的，其条目可能已被
    // 对账回收（removeRenderEntities），留着会被 processBatch 重新上传 —— 必须清掉。
    m_queue.clear();
    m_cursor = 0;
    m_recollectPending = false;
    // 记录新的已覆盖盒：平移回该盒内时 refillQueueIfStale 的 regionMoved 判据为假，
    // 不会无谓重收集。缩放基准 m_scaleAtBuild 故意不动 —— 见头文件说明。
    m_collectedBox = coveredBox;
    m_collectedBoxValid = coveredBox.isValid();

    // 失败条目兜底：上面的清队列会把还没成功补交的重试条目一并丢弃
    // （LOD 曲线已在账本里，协调器对账的 toAdd 差分不会把它们补回来）。
    // 若存在未补交的失败条目，强制一次重收集：m_scaleAtBuild 归零让
    // isScaleStale 恒真，refillQueueIfStale 的「缩放/区域都没变就跳过」
    // 前置门随之失效 —— 失败的 LOD 曲线被重新收集进队列；
    // 扩展队列的非曲线图元由对账的账本差分 toAdd 补回（失败不写账本，
    // 必出现在 toAdd 里），两条路径合起来覆盖全部失败条目。
    // ensurePump 保证无需相机事件也能在下一 tick 经 finishQueue 触发重收集。
    if (m_submitRetryPending)
    {
        m_submitRetryPending = false;
        m_recollectPending = true;
        m_scaleAtBuild = 0.0;
        ensurePump();
    }
}

void RenderSubmitScheduler::refillQueueIfStale()
{
    if (!m_renderWidget || !m_sceneManager)
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

            const double centerX = 0.5 * (minX + maxX);
            const double centerY = 0.5 * (minY + maxY);
            const double halfW = 0.5 * (maxX - minX) * kCollectBoxMargin;
            const double halfH = 0.5 * (maxY - minY) * kCollectBoxMargin;
            collectBox = Ut::BBox2d(centerX - halfW, centerY - halfH, centerX + halfW, centerY + halfH);
            hasVisibleBox = true;
        }
    }

    // 过期判据：
    //   缩放滞后（isScaleStale：放大到 kLodUpgradeRatio 倍就升级、缩小到
    //   kLodDowngradeRatio 倍以下才降级，中间留死区避免抖动），或
    //   可视区域越出上次收集覆盖的矩形（平移）。
    // 降级阈值从 3.0 调整为 2.0：原先 3 倍死区导致缩小时图元长期携带高精度顶点，
    // GPU 绘制不必要的大量顶点（例如 256 段圆只需 8 段时的冗余是 32 倍）。
    // 2.0 在抖动和冗余顶点之间取得更好的平衡：
    //   - 视口裁剪（kCollectBoxMargin 余量）已保证平移时小幅缩小不触发重收集
    //   - 配合容差公式修复（max(1e-6)），每次降级的重建代价本身也更小
    const bool scaleStale = isScaleStale(worldToScreenScale, m_scaleAtBuild);
    const bool regionMoved = hasVisibleBox && m_collectedBoxValid && !m_collectedBox.contains(visibleBox);
    if (!scaleStale && !regionMoved)
    {
        return;
    }

    m_scaleAtBuild = worldToScreenScale;
    m_collectedBox = collectBox;
    m_collectedBoxValid = hasVisibleBox;

    m_queue.clear();
    m_cursor = 0;

    // 单个候选的入队判据（两套收集路径共用）：
    // 隐藏图层 / 隐藏图元 / 「选中时隐藏本体」过滤 —— LOD 提交不经过
    // applyLightRefresh 的可见性判断，队列里不能收这些图元。
    const auto tryEnqueue = [&](Eg::AprEntity* entity) {
        if (!entity || !Eg::tess::isLodCurveType(entity->eType))
        {
            return;
        }
        if (!isRenderable(static_cast<uint64_t>(entity->id), entity))
        {
            return;
        }
        // POLYGON 的离散结果在两种情况下随缩放变化：
        //   1. 顶点数 > kPolygonSimplifyMinVertices → zoom out 时 Douglas-Peucker 简化；
        //   2. 带 bulge（DXF 圆弧）→ 任何缩放都要按当前弦高容差重新展开。
        // 其余多边形在任意缩放下输出都相同，入 LOD 队列纯浪费。
        if (entity->eType == Eg::EType::POLYGON)
        {
            const auto* polygon = static_cast<const Eg::AprPolygon*>(entity);
            if (!polygon->hasBulges() && polygon->vertices().size() <= Eg::kPolygonSimplifyMinVertices)
            {
                return;
            }
        }
        m_queue.push_back(static_cast<uint64_t>(entity->id));
    };

    if (hasVisibleBox)
    {
        // 主路径：空间索引范围查询。旧实现是 9 个类型索引的全场景遍历 ——
        // 每次跨滞后阈值都要扫 29 万图元（放大时主线程直接卡住数百毫秒）；
        // queryByBox 走 RTree，O(log N + k)，且查询前 flush 待修索引，
        // 与逐类型扫描 + getBbox 相交判断的结果等价（无效 bbox 不在索引里，天然跳过）。
        const Eg::VecAprEntityPtr candidates = m_sceneManager->queryByBox(collectBox, /*containedOnly=*/false);
        for (Eg::AprEntity* entity : candidates)
        {
            tryEnqueue(entity);
        }
    }
    else
    {
        // 矩阵退化（拿不到可视盒）：无范围可用，退回逐类型扫描（与旧行为一致）。
        // forEachEntityByType 走类型索引，O(k) 且不拷贝整份指针数组。
        // 类型清单引用单一来源 Eg::tess::lodCurveTypes()，不再本地维护副本。
        for (const Eg::EType type : Eg::tess::lodCurveTypes())
        {
            m_sceneManager->forEachEntityByType(type, [&](Eg::AprEntity* entity) { tryEnqueue(entity); });
        }
    }
}

bool RenderSubmitScheduler::submitAvailable() const
{
    // 判定优先级：注入谓词（测试/特殊视口）> 注入提交回调（测试无 GL，默认
    // 可用）> RenderWidget::isUploadAvailable()（与 upsertRenderEntities 的
    // 环境前置同源：场景有效 + GL 有上下文 / Metal 恒可用）。
    if (m_uploadAvailable)
    {
        return m_uploadAvailable();
    }
    if (m_submitBatch)
    {
        return true;
    }
    return m_renderWidget != nullptr && m_renderWidget->isUploadAvailable();
}

void RenderSubmitScheduler::ensurePump()
{
    // 本类不持有定时器：由协调器注入的回调负责启动/续跑消费泵。
    // 队列还有剩余、或 finishQueue 重收集出新队列时，都必须真正唤醒定时器；
    // 单次定时器触发完就停了，只留一个空标记会让剩余图元永久停在旧精度。
    // 场景/视口解绑后不再唤醒，避免对已失效的队列空转。
    if (m_pump && m_renderWidget && m_sceneManager)
    {
        m_pump();
    }
}
