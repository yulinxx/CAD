#pragma once

#include <QPointF>
#include <QString>
#include <QColor>
#include <functional>
#include <string>
#include <vector>
#include <optional>

#include "BBox/BBox2d.hpp"

struct Camera2D;
class ISelectionService;
class RenderWidget;

namespace Eg
{
    class SceneManager;
}

class ViewportSelector
{
public:
    ViewportSelector(Eg::SceneManager* sceneManager,
        ISelectionService* selectionService,
        const Camera2D* camera,
        RenderWidget* renderWidget);

    bool handleClick(const QPointF& worldPos);

    bool isBoxSelecting() const
    {
        return m_boxSelecting;
    }

    void beginBoxSelect(const QPointF& worldPos);
    void updateBoxSelect(const QPointF& worldPos);
    size_t endBoxSelect(const QPointF& worldPos);

    // ---- 选择查询/管理（P5 从 RenderViewport2D 下沉） ----

    // 获取第一个选中图元的 ID
    QString selectedEntityId() const;
    // 按 ID 选中图元（先清空再选中）
    void selectEntityById(const QString& entityId);
    // 清空选择
    void clearSelection();

    // 计算选中图元的合并 BBox（P5 从 RenderViewport2D 下沉）
    // 返回 nullopt 表示无选中或 BBox 无效
    std::optional<Ut::BBox2d> selectionBBox() const;

    void setSceneManager(Eg::SceneManager* sm)
    {
        m_sceneManager = sm;
    }

    void setSelectionService(ISelectionService* svc)
    {
        m_selectionService = svc;
    }

    void setStatusCallback(std::function<void(const QString&)> callback)
    {
        m_statusCallback = std::move(callback);
    }

    void setSelectionCallback(std::function<void(const QString&, const QString&)> callback)
    {
        m_selectionCallback = std::move(callback);
    }

private:
    void performHitTest(const QPointF& worldPos);

    Eg::SceneManager* m_sceneManager;
    ISelectionService* m_selectionService;
    const Camera2D* m_camera;
    RenderWidget* m_renderWidget;

    bool m_boxSelecting{ false };
    QPointF m_boxSelectStart;
    QPointF m_boxSelectEnd;

    std::function<void(const QString&)> m_statusCallback;
    std::function<void(const QString&, const QString&)> m_selectionCallback;
};
