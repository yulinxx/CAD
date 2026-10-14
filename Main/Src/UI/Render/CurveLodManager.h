#pragma once

#include <unordered_set>
#include <vector>
#include <cstdint>

#include "Ut/BBox2d.h"

namespace Eg
{
    class SceneManager;
    class SyEntity;
}

class RenderWidget;

/// @brief 曲线 LOD 分批重建管理器 —— 从 SceneRefreshCoordinator 拆分
///
/// 职责：按当前缩放/可视区域判断曲线图元是否需要重建精度，过期时收集
/// 曲线类图元 ID 进队列，由调用方每帧消费一批（processBatch），
/// 跨多帧完成，避免一次性重 tessellate 上万个曲线造成单帧卡顿。
class CurveLodManager
{
public:
    explicit CurveLodManager(RenderWidget* widget);
    ~CurveLodManager() = default;

    CurveLodManager(const CurveLodManager&) = delete;
    CurveLodManager& operator=(const CurveLodManager&) = delete;

    /// 视口切换时更新目标 widget
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    /// 相机变化时调用：按「缩放滞后 + 可视区域」判断是否重新收集队列
    void requestRefresh();

    /// 每帧消费一批（kBatchSize 个），整批只切一次 GL 上下文、只请求一次重绘。
    /// renderedIds 为「已提交到渲染系统的图元 ID」账本，新增的会写回其中。
    void processBatch(std::unordered_set<uint64_t>& renderedIds);

    /// 绑定/解绑场景（与协调器共用同一入口时机）
    void setSceneManager(Eg::SceneManager* sm) { m_sceneManager = sm; }
    Eg::SceneManager* sceneManager() const { return m_sceneManager; }

    /// 队列排空后的收尾：清队列 + 处理「排队期间又要求重新收集」的待办
    void finishQueue();

    /// 全量重建后调用：清队列并把缩放基准锚定到当前值
    void clearAfterFullRefresh(double worldToScreenScale);

    /// 仅清空队列与游标，**不动缩放基准**（供 updateSceneRender 的全量前置兜底；
    /// 真正的锚定由随后的 applyFullRefresh → clearAfterFullRefresh 完成）
    void clearQueue();

    bool empty() const { return m_queue.empty(); }
    size_t size() const { return m_queue.size(); }

private:
    void refillQueueIfStale();
    void ensurePump();

    RenderWidget* m_renderWidget;
    Eg::SceneManager* m_sceneManager{ nullptr };
    std::vector<uint64_t> m_queue;
    size_t m_cursor = 0;

    /// 上次收集队列时的 worldToScreenScale（缩放滞后判据的基准）。<= 0 表示还没收集过。
    double m_scaleAtBuild{ 0.0 };
    /// 上次收集队列时覆盖的世界矩形（含余量）。可视区域一旦越出它就必须重新收集，
    /// 否则平移后新进视野的图元会一直停在旧精度。
    Ut::BBox2d m_collectedBox;
    bool m_collectedBoxValid{ false };
    /// 队列尚在消费期间又出现了新的收集请求：排空后立即按当时的状态重新评估
    bool m_recollectPending{ false };
};
