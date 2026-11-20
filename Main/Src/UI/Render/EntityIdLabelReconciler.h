#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>

#include "Engine/EntityIdGenerator.h"
#include "Render/EntityIdLabelTypes.h"

namespace Eg
{
    class SceneManager;
}

class RenderWidget;

/// @brief 图元 ID 标注账本协调器 —— 从 SceneRefreshCoordinator 拆分
///
/// 与 TextReconciler / BitmapReconciler 同一形状、同一真源规则：以 SceneManager
/// 中「可见 && 图层可见 && 持久 ID（id > 0）」的图元集合为期望集合，维护一份
/// {entityId → 标签项} 账本，整批推送给 RenderWidget 逐帧绘制。
///
/// 数据源唯一来自 SceneManager 的真实 AprEntity：绝不读取 RenderSceneBuilder
/// 内部的合成号段（高位）或 clone 临时号段（负数），避免把内部 ID 显示给用户。
///
/// 标签项只在对账时计算（含 getBbox 右上角锚点），绘制侧每帧用 TextQuadBuilder
/// 以 kCoordModeWorldPos + 固定像素字号换算，因此缩放视图时字号不变、位置跟随。
class EntityIdLabelReconciler
{
public:
    explicit EntityIdLabelReconciler(RenderWidget* widget);
    ~EntityIdLabelReconciler() = default;

    EntityIdLabelReconciler(const EntityIdLabelReconciler&) = delete;
    EntityIdLabelReconciler& operator=(const EntityIdLabelReconciler&) = delete;

    /// 视口切换时更新目标 widget（账本保留，由随后的 reconcile 对账）
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    /// 对账：全量时扫描场景；增量时只处理 dirtyIds，并显式移除 deletedIds。
    /// 图层可见性变化不会进入图元变更流，调用方须使用 fullReconcile=true。
    /// 若 RenderWidget 侧开关关闭，直接清空账本并返回。
    void reconcile(Eg::SceneManager* sm, bool fullReconcile,
        const std::unordered_set<Eg::EntityId>& dirtyIds,
        const std::unordered_set<Eg::EntityId>& deletedIds = {});

    /// 账本是否为空（供 SceneRefreshCoordinator 决定是否可跳过对账）
    bool empty() const { return m_labels.empty(); }

    /// 账本条目数（诊断与回归测试用）
    size_t size() const { return m_labels.size(); }

    /// 查找某个图元的标签项（回归测试用），不存在返回 nullptr
    const Render::EntityIdLabelItem* findLabel(Eg::EntityId id) const
    {
        auto it = m_labels.find(id);
        return it == m_labels.end() ? nullptr : &it->second;
    }

private:
    /// 为单个图元计算标签项；返回 false 表示该图元不应显示标签
    bool buildLabel(Eg::SceneManager* sm, Eg::EntityId id, Render::EntityIdLabelItem& outItem) const;

    /// 把当前账本快照整批推送给渲染侧（触发重绘）
    void pushToWidget();

    RenderWidget* m_renderWidget;
    /// 已同步账本：entityId → 标签项（仅含应显示的持久图元）
    std::unordered_map<Eg::EntityId, Render::EntityIdLabelItem> m_labels;
};
