#pragma once

#include <unordered_set>
#include <cstdint>

#include "Engine/EntityIdGenerator.h"

namespace Eg
{
    class SceneManager;
}

class RenderWidget;

/// @brief 位图层协调器 —— 从 SceneRefreshCoordinator 拆分
///
/// 单源真值 = 场景中可见 SyImage 集合。统一处理 新增/删除/图层显隐/全量重建：
///   fullReconcile=true：先清空位图层（renderBeginScene 已清 GPU 位图），整体重传
///   fullReconcile=false：按账本增量对账，并移除场景中已不存在的位图
class BitmapReconciler
{
public:
    explicit BitmapReconciler(RenderWidget* widget);
    ~BitmapReconciler() = default;

    BitmapReconciler(const BitmapReconciler&) = delete;
    BitmapReconciler& operator=(const BitmapReconciler&) = delete;

    /// 视口切换时更新目标 widget（账本保留，由随后的 reconcile 对账）
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    void reconcile(Eg::SceneManager* sm, bool fullReconcile,
        const std::unordered_set<uint64_t>& selectedIds, bool hideSelected,
        const std::unordered_set<Eg::EntityId>& dirtyIds);

    /// 本轮是否出现过位图变脏/新增（决定调用方是否需要对账）
    bool empty() const { return m_bitmapImageIds.empty(); }
    bool has(uint64_t id) const { return m_bitmapImageIds.count(id) > 0; }
    const std::unordered_set<uint64_t>& ids() const { return m_bitmapImageIds; }

private:
    RenderWidget* m_renderWidget;
    std::unordered_set<uint64_t> m_bitmapImageIds;  ///< 已同步到 GPU 的 SyImage 账本
};
