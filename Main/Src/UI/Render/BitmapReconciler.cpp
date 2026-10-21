#include "BitmapReconciler.h"

#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/Layer/SyLayer.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine/SyEntity/EType.h"

BitmapReconciler::BitmapReconciler(RenderWidget* widget)
    : m_renderWidget(widget)
{
}

void BitmapReconciler::reconcile(Eg::SceneManager* sm,
    bool fullReconcile,
    const std::unordered_set<uint64_t>& selectedIds,
    bool hideSelected,
    const std::unordered_set<Eg::EntityId>& dirtyIds)
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
        const bool isDirty = dirtyIds.count(id) > 0;
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