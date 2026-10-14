#pragma once

#include <unordered_set>
#include <cstdint>

#include "Engine/EntityIdGenerator.h"

namespace Eg
{
    class SceneManager;
}

class RenderWidget;

/// @brief 世界文字层协调器 —— 从 SceneRefreshCoordinator 拆分
///
/// 与 BitmapReconciler 同一形状、同一真源规则（场景中可见 SyText 集合）。
/// 文本没有可复用的顶点块（字形四边形要随字体图集重排），因此不走
/// entityToVertices 那条增量顶点路径，由本类独立维护账本。
class TextReconciler
{
public:
    explicit TextReconciler(RenderWidget* widget);
    ~TextReconciler() = default;

    TextReconciler(const TextReconciler&) = delete;
    TextReconciler& operator=(const TextReconciler&) = delete;

    /// 视口切换时更新目标 widget（账本保留，由随后的 reconcile 对账）
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    void reconcile(Eg::SceneManager* sm, bool fullReconcile,
        const std::unordered_set<uint64_t>& selectedIds, bool hideSelected,
        const std::unordered_set<Eg::EntityId>& dirtyIds);

    bool empty() const { return m_worldTextIds.empty(); }
    bool has(uint64_t id) const { return m_worldTextIds.count(id) > 0; }
    const std::unordered_set<uint64_t>& ids() const { return m_worldTextIds; }

private:
    RenderWidget* m_renderWidget;
    std::unordered_set<uint64_t> m_worldTextIds;  ///< 已同步到 GPU 的 SyText 账本
};
