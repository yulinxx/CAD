#pragma once

#include <QString>

class RenderViewport2D;

/**
 * @brief 视口动作中枢 — 将视图操作（缩放/平移/重置）路由到当前活动视口
 *
 * 由组合根持有；2D 工作台在视口创建后调用 setViewport() 注入当前视口，
 * 工作台切换时调用 clearViewport() 清空。菜单 Zoom 子菜单与右键菜单
 * 的 View_* 操作统一经 handle() 分发，避免各入口各自持有视口指针。
 */
class ViewportActionHub
{
public:
    void setViewport(RenderViewport2D* viewport);
    void clearViewport();

    RenderViewport2D* viewport() const
    {
        return m_viewport;
    }

    /// 按动作名分发：zoom_in / zoom_out / zoom_fit / zoom_selection / reset / pan
    void handle(const QString& action);

private:
    RenderViewport2D* m_viewport{ nullptr };
};
