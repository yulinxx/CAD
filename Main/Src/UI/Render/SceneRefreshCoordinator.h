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

#include "Engine/EntityIdGenerator.h"
#include "Engine2D/Core/SceneNotifier.h"
#include "UI/Render/ISceneRefreshScheduler.h"

class FrameTimer;

class RenderWidget;

namespace Eg
{
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

    /// 设置场景管理器（必须）— 同时负责观察者注册/注销
    void setSceneManager(Eg::SceneManager* sm);

    // ==================== ISceneRefreshScheduler 实现 ====================

    /// 轻量重绘 — 纯视觉刷新，不触碰渲染数据（选择变化、光标移动等）
    void requestRepaint() override;

    /// 增量刷新 — 提交脏/删除图元到渲染设备（图元修改、少量增删后）
    void requestLightRefresh() override;

    /// 全量刷新 — 完整 gather + submit（导入、大批量修改、文档加载后）
    void requestFullRefresh() override;

    void markEntityDirty(uint64_t entityId) override;
    void markEntityDeleted(uint64_t entityId) override;
    UI::SceneRefreshLevel pendingLevel() const override;
    void flushPendingRefresh() override;

    /// 停止定时器，防止析构过程中访问已释放资源
    void stop() override;

    // ==================== 场景变更回调（IObserver 实现） ====================

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

private slots:
    void updateSceneRender();

private:
    // 级别语义与 3D 共用，见 UI/Render/ISceneRefreshScheduler.h
    using RefreshLevel = UI::SceneRefreshLevel;


    void scheduleSceneUpdate();
    void scheduleFullRefresh();
    void applyRepaintRefresh();
    void applyLightRefresh(Eg::SceneManager* sm);
    void applyFullRefresh(Eg::SceneManager* sm);

    RenderWidget* m_renderWidget{ nullptr };
    Eg::SceneManager* m_sceneManager{ nullptr };

    // 场景更新节流定时器
    QTimer* m_sceneUpdateTimer{ nullptr };

    RefreshLevel m_refreshLevel{ RefreshLevel::None };

    // 脏标记集合（增量渲染）
    std::unordered_set<Eg::EntityId> m_pendingDirtyIds;
    std::unordered_set<Eg::EntityId> m_pendingDeletedIds;

    // 已提交到渲染系统的实体 ID 集合（区分新增 vs 修改）
    std::unordered_set<uint64_t> m_renderedEntityIds;

    // 上一帧已同步的选中集合：用于在选择变更时计算“发生选中态翻转”的图元，
    // 将其加入待处理脏集合，驱动增量路径正确增删（见 onSelectionChanged）。
    std::unordered_set<uint64_t> m_lastSelectedIds;

    // 当前已同步到位图渲染层的 SyImage 实体 ID 集合（多图支持，本地账本）
    std::unordered_set<uint64_t> m_bitmapImageIds;

    // 位图层协调（单源真值 = 场景中可见 SyImage 集合）：
    // 统一处理 新增/修改/删除/图层显隐/全量重建，增量与全量路径收敛于此。
    //   fullReconcile=true：先清空位图层（renderBeginScene 已清 GPU 位图），整体重传
    //   fullReconcile=false：仅按 dirty/新增增量上传，并移除场景中已不存在的位图
    void reconcileBitmaps(Eg::SceneManager* sm, bool fullReconcile);

    // 当前已同步到世界文字层的 SyText 实体 ID 集合（本地账本）
    std::unordered_set<uint64_t> m_worldTextIds;

    // 世界文字层协调：与 reconcileBitmaps 同一形状、同一真源规则。
    // 文本没有可复用的顶点块（字形四边形要随字体图集重排），因此不走
    // entityToVertices 那条增量顶点路径，而是自己一路。
    void reconcileTexts(Eg::SceneManager* sm, bool fullReconcile);

    // 帧耗时追踪器（性能监控基础设施）
    std::unique_ptr<FrameTimer> m_frameTimer;
};