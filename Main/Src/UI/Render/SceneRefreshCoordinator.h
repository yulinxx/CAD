/**
 * @file SceneRefreshCoordinator.h
 * @brief 2D 场景刷新协调器 — UI::ISceneRefreshScheduler 的 2D 实现
 *
 * 从 RenderViewport2D 中抽取，封装刷新级别管理、脏 ID 收集、
 * 定时器节流、GPU 数据提交等职责，降低视口类的复杂度。
 *
 * 刷新级别与调度语义定义在公共契约 UI/Render/ISceneRefreshScheduler.h，
 * 与 3D 侧共用同一套词汇（None/Repaint/LightUpdate/Selection/FullRefresh，
 * 严格单调升级不可降级）。本类只负责 2D 的重建与提交细节。
 *
 * 公开 API 语义：
 *   - requestRepaint():      纯视觉刷新，调用方知道渲染数据未变
 *   - requestLightRefresh():  增量刷新，调用方知道图元被修改/增删
 *   - requestFullRefresh():   全量刷新，调用方知道需要重建所有渲染数据
 *
 * P5 刷新语义统一 (2026-08-02)；2026-08-27 抽出 2D/3D 公共调度契约
 */
#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include <unordered_set>
#include <vector>

#include "Engine/EntityIdGenerator.h"
#include "Engine/Scene/SceneChangeSet.h"
#include "Engine/Scene/RenderSnapshot.h"
#include "Engine2D/Core/SceneNotifier.h"
#include "UI/Render/ISceneRefreshScheduler.h"
#include "Ut/BBox2d.h"
#include "BitmapReconciler.h"
#include "TextReconciler.h"
#include "CurveLodManager.h"

class FrameTimer;

class RenderWidget;

namespace Eg
{
    class ISceneContext;
    class SceneManager;
}

class SceneRefreshCoordinator : public QObject,
                                public UI::ISceneRefreshScheduler,
                                private Eg::SceneNotifier::IObserver  // P5: 观察者注册收敛到协调器
{
    Q_OBJECT

public:
    explicit SceneRefreshCoordinator(QObject* parent = nullptr);
    ~SceneRefreshCoordinator() override;

    /// 设置渲染控件（必须）
    void setRenderWidget(RenderWidget* widget);

    /// 设置场景上下文（必须）— 同时负责观察者注册/注销
    void setSceneContext(Eg::ISceneContext* ctx);

    // ==================== ISceneRefreshScheduler 实现 ====================

    /// 轻量重绘 — 纯视觉刷新，不触碰渲染数据（选择变化、光标移动等）
    void requestRepaint() override;

    /// 增量刷新 — 提交脏/删除图元到渲染设备（图元修改、少量增删后）
    void requestLightRefresh() override;

    /// 全量刷新 — 完整 gather + submit（导入、大批量修改、文档加载后）
    void requestFullRefresh() override;

    /**
     * @brief 曲线 LOD 分批重建 — 按缩放与可视区域判断是否需要（重新）收集
     *
     * 收集范围只覆盖当前可视区域（含少量余量）：曲线精度只对看得见的图元有意义，
     * 为了视野内几十个图元去重算 29 万图元既慢又白做。代价是视野移动后新进画面的
     * 图元需要补收集 —— 所以本函数在相机每次变化时都会被调用，由它自己判断是否过期：
     *   - 缩放跨过滞后阈值（放大 1.3 倍 / 缩小 2 倍），或
     *   - 可视区域越出了上次收集覆盖的矩形（平移）
     * 两者都不成立时它只做几次比较就返回。
     *
     * 收集到的 ID 放进 CurveLodManager 的队列，由 processCurveLodBatch 每帧消费一批，
     * 跨多帧完成，避免一次性重 tessellate 上万个曲线造成单帧卡顿。
     */
    void requestCurveLodRefresh();

    void markEntityDirty(uint64_t entityId) override;
    void markEntityDeleted(uint64_t entityId) override;
    UI::SceneRefreshLevel pendingLevel() const override;
    void flushPendingRefresh() override;
    std::unordered_set<uint64_t> takePendingDirtyIds() override;
    std::unordered_set<uint64_t> takePendingDeletedIds() override;

    /// 停止定时器，防止析构过程中访问已释放资源
    void stop() override;

    /// 设置拖动中是否强制显示选中图元（拖动时暂时显示原图形，拖动结束后恢复隐藏）
    void setForceShowSelectedDuringDrag(bool forceShow);

    // ==================== 场景变更回调（IObserver 实现） ====================

    /// 广播优先级：渲染刷新最先收到变更通知（见 Eg::ObserverPriority）
    int observerPriority() const override
    {
        return Eg::ObserverPriority::Render;
    }

    /// 场景数据变更通知（图元增删改）
    void onSceneChanged() override;

    /// 选择变更通知 — 发射信号 + 纯视觉重绘
    void onSelectionChanged() override;

    /// 获取帧计时器（用于外部读取性能数据）
    FrameTimer* frameTimer() const
    {
        return m_frameTimer.get();
    }

    /// 启用/禁用帧性能监控
    void setPerfMonitorEnabled(bool enabled);

signals:
    /// 选择变更信号（视口连接后用于同步工具状态）
    void selectionChanged();

    /// 选中态的几何被改动（拖动/对齐/镜像/缩放等）—— 语义只有一句：轮廓要按当前场景重建。
    /// 与 selectionChanged 分开：后者还挂着属性面板重建、菜单刷新、场景树选择同步等 UI 扇出，
    /// 拖动过程中每个几何变更都发一次的话，UI 会跟着把同一份选择重做一遍（见 onSceneChanged）。
    void selectionOutlineInvalidated();

private slots:
    void updateSceneRender();
    /// 独立的曲线 LOD 消费 slot，由 m_curveLodTimer 触发
    void onCurveLodTimer();

private:
    // 级别语义与 3D 共用，见 UI/Render/ISceneRefreshScheduler.h
    using RefreshLevel = UI::SceneRefreshLevel;


    void scheduleSceneUpdate();
    void scheduleFullRefresh();
    void applyRepaintRefresh();
    void applyLightRefresh(Eg::SceneManager* sm);
    void applyFullRefresh(Eg::SceneManager* sm);
    void processCurveLodBatch();

    /// 确保曲线 LOD 的消费泵（节流定时器）在跑
    void ensureCurveLodPump();

    /// 队列排空后的收尾：清队列 + 处理「排队期间又要求重新收集」的待办
    void finishCurveLodQueue();

    /// 「隐藏选中本体」是否有效：设置开启且虚线轮廓能绘制
    bool hideSelectedEffective() const;

    RenderWidget* m_renderWidget{ nullptr };
    Eg::SceneManager* m_sceneManager{ nullptr };

    // 场景更新节流定时器
    QTimer* m_sceneUpdateTimer{ nullptr };

    // 曲线 LOD 分批消费专用定时器（独立于场景更新，避免场景操作频繁时 LOD 进度被阻塞）
    // 间隔 32ms（约 30fps），低优先级后台任务，不与场景刷新竞争同一个时间槽。
    QTimer* m_curveLodTimer{ nullptr };

    RefreshLevel m_refreshLevel{ RefreshLevel::None };

    /// 上一次刷新生效的「隐藏选中本体」状态，用于判断是否需要全量重建
    bool m_lastHideSelectedEffective{ false };

    /// 拖动中强制显示选中图元（覆盖 hideSelectedEffective）
    bool m_forceShowSelectedDuringDrag{ false };

    // 脏标记集合（增量渲染）
    std::unordered_set<Eg::EntityId> m_pendingDirtyIds;
    std::unordered_set<Eg::EntityId> m_pendingDeletedIds;

    // 已提交到渲染系统的图元 ID 集合（区分新增 vs 修改）
    std::unordered_set<uint64_t> m_renderedEntityIds;

    // 变更流游标（用于 readChanges 增量消费）
    Eg::ISceneChangeStream::Cursor m_lastCursor{ 0 };

    // 本轮待刷新的图元快照：在 UI 线程（onSceneChanged）按变更集 clone 出来，
    // 增量刷新 —— 含并行离散化那条工作线程路径 —— 只读这份只读副本，
    // 不再去碰场景里的活对象（SceneManager 的契约是只有主线程可以访问）。
    // 按批次累积，刷完由 updateSceneRender 清空。
    Eg::SceneSnapshot m_pendingSnapshot;

    // 上一帧已同步的选中集合：用于在选择变更时计算“发生选中态翻转”的图元，
    // 将其加入待处理脏集合，驱动增量路径正确增删（见 onSelectionChanged）。
    std::unordered_set<uint64_t> m_lastSelectedIds;

    // 「隐藏选中本体」生效时，选择集变化在世界层上等价于：新选中的要从世界层摘掉，
    // 取消选中的要把本体加回来。这里只记这两个差集，由 applyLightRefresh 增量处理 ——
    // 曾经这条路径直接升 FullRefresh（重建整份场景 29 万图元），选中/取消一次要十几秒，
    // 而且顺带把视口外所有图元的 LOD 也一起重建了。
    std::unordered_set<uint64_t> m_selectionHideIds;    ///< 新选中：需从世界层移除本体
    std::unordered_set<uint64_t> m_selectionRestoreIds; ///< 取消选中：需把本体加回世界层

    // 缓存的选中 ID 集合：供 applyLightRefresh/applyFullRefresh/reconcile* 使用
    // 避免每次刷新都遍历选择集构建哈希表
    mutable std::unordered_set<uint64_t> m_cachedSelectedIds;
    mutable bool m_selectedIdsCacheValid{ false };

    // ==================== 拆分出去的职责（辅助类） ====================
    //
    // 位图层 / 世界文字层 / 曲线 LOD 分批原是本类的三段私有实现 + 一份账本成员。
    // 三者各自满足「独立状态 + 独立真源规则」的拆分条件，独立为策略类后：
    //   - 本类只保留刷新级别调度与变更流消费，代码量减半
    //   - 各协调器的账本不再暴露给调度逻辑，状态归属清晰
    BitmapReconciler m_bitmapReconciler{ nullptr };
    TextReconciler m_textReconciler{ nullptr };
    CurveLodManager m_curveLodManager{ nullptr };

    // 帧耗时追踪器（性能监控基础设施）
    std::unique_ptr<FrameTimer> m_frameTimer;
};