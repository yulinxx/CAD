#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Engine/EntityIdGenerator.h"
#include "Render/EntityIdLabelTypes.h"
#include "Ut/BBox2d.h"

namespace Eg
{
    class SceneManager;
}

class RenderWidget;

/**
 * @brief 图元 ID 标注账本协调器 —— 从 SceneRefreshCoordinator 拆分
 *
 * 与 TextReconciler / BitmapReconciler 同一形状、同一真源规则：以 SceneManager
 * 中「可见 && 图层可见 && 持久 ID（id > 0）&& 与视口盒相交」的图元集合为期望集合，
 * 维护一份 {entityId → 标签项} 账本，整批推送给 RenderWidget 逐帧绘制。
 *
 * ## 视口裁剪与预算
 *
 * 大场景下不能把全场景图元都生成标签：
 *   - 视口盒（与几何层共用同一 1.25 倍余量盒）之外的图元不生成标签；
 *   - 同时显示的标签数受 RenderWidget::entityIdLabelMaxCount()（设置项下发）限制。
 *
 * 预算选择策略保证稳定不闪烁：**已在账本中的图元优先保留**（按 ID 升序截断），
 * 剩余名额再按 ID 升序补充新图元。平移/缩放时不会出现整片标签随机换人。
 *
 * ## 数据源纯净
 *
 * 只来自 SceneManager 的真实 AprEntity：绝不读取 RenderSceneBuilder 内部的
 * 合成号段（高位）或 clone 临时号段（负数），避免把内部 ID 显示给用户。
 *
 * ## 缩放字号不变
 *
 * 标签项只在对账时计算（含 getBbox 右上角锚点），绘制侧每帧用 TextQuadBuilder
 * 以 kCoordModeWorldPos + 固定像素字号换算，因此缩放视图时字号不变、位置跟随。
 */
class EntityIdLabelReconciler
{
public:
    /// 一次对账的诊断计数（回归测试与日志用）
    struct Stats
    {
        /// 通过可见性/图层/ID/包围盒判据、且与视口盒相交的候选图元数
        size_t candidates = 0;
        /// 因不与视口盒相交被裁掉的图元数。
        /// 注：视口盒有效时走空间索引范围查询（候选本就相交），此值仅在
        /// 无效盒退化为全场景遍历、或索引与包围盒不一致时非零。
        size_t culledByViewport = 0;
        /// 因预算被裁掉的候选图元数
        size_t culledByBudget = 0;
        /// 最终进入账本的标签数
        size_t shown = 0;
    };

    explicit EntityIdLabelReconciler(RenderWidget* widget);
    ~EntityIdLabelReconciler() = default;

    EntityIdLabelReconciler(const EntityIdLabelReconciler&) = delete;
    EntityIdLabelReconciler& operator=(const EntityIdLabelReconciler&) = delete;

    /// 视口切换时更新目标 widget（账本保留，由随后的 reconcile 对账）
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    /**
     * @brief 设置标签查询盒（世界坐标，通常为几何层的 1.25 倍余量盒）
     *
     * 无效盒（默认）= 不做视口裁剪，退化为全场景。
     * 盒变化本身不立即对账，由调用方随后调用 reconcile(fullReconcile=true)。
     */
    void setVisibleWorldBounds(const Ut::BBox2d& box)
    {
        m_visibleBounds = box;
    }

    void clearVisibleWorldBounds()
    {
        m_visibleBounds = Ut::BBox2d{};
    }

    Ut::BBox2d visibleWorldBounds() const
    {
        return m_visibleBounds;
    }

    /**
     * @brief 对账：全量时以场景（结合视口盒）为真源重建账本；增量时只处理
     *        dirtyIds（新增/移动）并显式移除 deletedIds，不扫描其余图元。
     *
     * 图层可见性变化不会进入图元变更流，调用方须使用 fullReconcile=true。
     * 若 RenderWidget 侧开关关闭，直接清空账本并返回。
     */
    void reconcile(Eg::SceneManager* sm, bool fullReconcile,
        const std::unordered_set<Eg::EntityId>& dirtyIds,
        const std::unordered_set<Eg::EntityId>& deletedIds = {});

    /**
     * @brief 场景切换：清空账本、视口盒与诊断计数，并把空列表推给渲染侧
     *
     * 旧场景的图元 ID 在新场景里没有意义，残留会显示错误的 ID。
     */
    void reset();

    /// 账本是否为空（供 SceneRefreshCoordinator 决定是否可跳过对账）
    bool empty() const { return m_labels.empty(); }

    /// 账本条目数（诊断与回归测试用）
    size_t size() const { return m_labels.size(); }

    /// 最近一次对账的诊断计数
    const Stats& stats() const
    {
        return m_stats;
    }

    /// 查找某个图元的标签项（回归测试用），不存在返回 nullptr
    const Render::EntityIdLabelItem* findLabel(Eg::EntityId id) const
    {
        auto it = m_labels.find(id);
        return it == m_labels.end() ? nullptr : &it->second;
    }

private:
    /// 判据 + 锚点计算；返回 false 表示该图元不应显示标签（不可见/无包围盒/出视口）
    bool buildLabel(Eg::SceneManager* sm,
        Eg::EntityId id,
        Render::EntityIdLabelItem& outItem,
        bool* outViewportCulled = nullptr) const;

    /// 预算上限；0 = 不限。每次对账实时读取 RenderWidget 上的设置值。
    size_t effectiveMaxLabels() const;

    /// 全量重建：候选集 → 预算选择 → 账本替换
    bool rebuildAll(Eg::SceneManager* sm);

    /// 把当前账本快照整批推送给渲染侧（触发重绘）
    void pushToWidget();

    RenderWidget* m_renderWidget;
    /// 已同步账本：entityId → 标签项（仅含应显示的持久图元）
    std::unordered_map<Eg::EntityId, Render::EntityIdLabelItem> m_labels;
    /// 标签查询盒（世界坐标）；无效 = 不裁剪
    Ut::BBox2d m_visibleBounds;
    /// 最近一次对账的诊断计数
    Stats m_stats;
};
