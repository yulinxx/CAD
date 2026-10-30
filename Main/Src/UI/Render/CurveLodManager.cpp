#include "CurveLodManager.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/Layer/SyLayer.h"
#include "Engine/SyEntity/EType.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"

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
        const Eg::SyEntity* entity = sm->findEntityById(id);
        const Eg::SyLayer* layer = entity != nullptr ? entity->layer() : nullptr;
        if (layer == nullptr || layer->getIndex() <= 0)
        {
            return 0;
        }
        return static_cast<uint32_t>(layer->getIndex());
    }
}  // namespace

CurveLodManager::CurveLodManager(RenderWidget* widget)
    : m_renderWidget(widget)
{
}

bool CurveLodManager::isRenderable(uint64_t uid, const Eg::SyEntity* entity) const
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
    const Eg::SyLayer* layer = entity->layer();
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

void CurveLodManager::requestRefresh()
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

void CurveLodManager::processBatch(std::unordered_set<uint64_t>& renderedIds)
{
    if (!m_renderWidget || !m_sceneManager)
    {
        return;
    }

    if (m_cursor >= m_queue.size())
    {
        finishQueue();
        return;
    }

    ++m_processedBatches;

    // 每帧预算：串行重建 2000 个曲线图元，亚毫秒级。zoom 期间场景不变，
    // 直接在主线程读活对象（SceneManager 契约），无需 clone 快照。
    constexpr size_t kBatchSize = 2000;
    const size_t end = (std::min)(m_cursor + kBatchSize, m_queue.size());

    // 整批一次装配：离散化与内容比对都在 RenderSceneBuilder 内完成
    // （与全量路径同一份公式），上下文只切一次、重绘只请求一次。
    //
    // 收集之后到消费之前，图元可能已经被删除、隐藏（自身/图层）或被
    // 「选中时隐藏本体」过滤掉 —— LOD 的 upsert 不经过 applyLightRefresh 的
    // 可见性判断，如果原样提交会把它们重新画回世界层。这里在提交前按活对象
    // 再过滤一次，并只为真正提交的图元写账本。
    std::vector<uint64_t> batch;
    batch.reserve(end - m_cursor);
    for (size_t i = m_cursor; i < end; ++i)
    {
        const uint64_t uid = m_queue[i];
        const Eg::SyEntity* entity = m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
        if (!isRenderable(uid, entity))
        {
            continue;
        }
        batch.push_back(uid);
    }
    if (!batch.empty())
    {
        if (m_renderWidget->upsertRenderEntities(
                batch,
                [this](uint64_t uid) -> const Eg::SyEntity* {
                    return m_sceneManager->findEntityById(static_cast<Eg::EntityId>(uid));
                },
                [this](uint64_t uid) -> uint32_t {
                    return layerIndexOfEntityForLod(m_sceneManager, static_cast<Eg::EntityId>(uid));
                }))
        {
            for (uint64_t uid : batch)
            {
                renderedIds.insert(uid);
            }
        }
        else
        {
            // LOD 批次提交失败：不把这些 ID 写入账本，下一批或下一帧重试
            m_recollectPending = true;
            ensurePump();
        }
    }

    m_cursor = end;

    if (m_cursor < m_queue.size())
    {
        // 还有剩余，下一帧继续（不提升 RefreshLevel，仍是纯后台分批）
        ensurePump();
    }
    else
    {
        finishQueue();
    }
}

void CurveLodManager::finishQueue()
{
    m_queue.clear();
    m_cursor = 0;

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

void CurveLodManager::clearAfterFullRefresh(double worldToScreenScale)
{
    // 全量重建已按当前缩放重新离散了整份场景：清队列、锚定缩放基准，
    // 并置区域记录为无效 —— 全场景都已是当前精度，平移不需要再补收集。
    m_queue.clear();
    m_cursor = 0;
    m_recollectPending = false;
    m_scaleAtBuild = worldToScreenScale;
    m_collectedBoxValid = false;
}

void CurveLodManager::clearQueue()
{
    m_queue.clear();
    m_cursor = 0;
    m_recollectPending = false;
}

void CurveLodManager::refillQueueIfStale()
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
    //   缩放滞后（放大到 1.3 倍就升级、缩小到 2 倍以下才降级，中间留死区避免抖动），或
    //   可视区域越出上次收集覆盖的矩形（平移）。
    // 降级阈值从 3.0 调整为 2.0：原先 3 倍死区导致缩小时图元长期携带高精度顶点，
    // GPU 绘制不必要的大量顶点（例如 256 段圆只需 8 段时的冗余是 32 倍）。
    // 2.0 在抖动和冗余顶点之间取得更好的平衡：
    //   - 视口裁剪（1.25 倍余量）已保证平移时小幅缩小不触发重收集
    //   - 配合容差公式修复（max(1e-6)），每次降级的重建代价本身也更小
    constexpr double kCurveLodUpgradeRatio = 1.3;
    constexpr double kCurveLodDowngradeRatio = 2.0;
    const bool firstTime = m_scaleAtBuild <= 0.0;
    const bool needsUpgrade = firstTime || worldToScreenScale > m_scaleAtBuild * kCurveLodUpgradeRatio;
    const bool needsDowngrade = !firstTime && worldToScreenScale * kCurveLodDowngradeRatio < m_scaleAtBuild;
    const bool regionMoved = hasVisibleBox && m_collectedBoxValid && !m_collectedBox.contains(visibleBox);
    if (!needsUpgrade && !needsDowngrade && !regionMoved)
    {
        return;
    }

    m_scaleAtBuild = worldToScreenScale;
    m_collectedBox = collectBox;
    m_collectedBoxValid = hasVisibleBox;

    // 收集曲线类图元 ID：只有它们的离散化段数/顶点数随缩放变化。
    // 必须覆盖所有「按 worldToScreenScale 自适应离散化/简化」的类型，否则放大后
    // 图元不会重建、精度停留低档。
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

    m_queue.clear();
    m_cursor = 0;

    for (const Eg::EType type : curveTypes)
    {
        const bool isPolygon = (type == Eg::EType::POLYGON);
        // forEachEntityByType 走类型索引，O(k) 且不拷贝整份指针数组
        // （getEntitiesByType 按值返回，POLYGON 那 29 万个指针会白拷 2.3MB）。
        m_sceneManager->forEachEntityByType(type, [&](Eg::SyEntity* entity) {
            if (!entity)
            {
                return;
            }
            // 隐藏图层 / 隐藏图元 / 「选中时隐藏本体」过滤：LOD 提交不经过
            // applyLightRefresh 的可见性判断，队列里不能收这些图元。
            if (!isRenderable(static_cast<uint64_t>(entity->id), entity))
            {
                return;
            }
            // POLYGON 的离散结果在两种情况下随缩放变化：
            //   1. 顶点数 > kPolygonSimplifyMinVertices → zoom out 时 Douglas-Peucker 简化；
            //   2. 带 bulge（DXF 圆弧）→ 任何缩放都要按当前弦高容差重新展开。
            // 其余多边形在任意缩放下输出都相同，入 LOD 队列纯浪费。
            if (isPolygon)
            {
                const auto* polygon = static_cast<const Eg::SyPolygon*>(entity);
                if (!polygon->hasBulges() && polygon->vertices().size() <= Eg::kPolygonSimplifyMinVertices)
                {
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
            m_queue.push_back(static_cast<uint64_t>(entity->id));
        });
    }
}

void CurveLodManager::ensurePump()
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
