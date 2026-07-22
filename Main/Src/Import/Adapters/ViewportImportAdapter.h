#pragma once

#include <functional>

/// 视口导入适配器：导入完成后刷新视口显示
class ViewportImportAdapter
{
public:
    ViewportImportAdapter() = default;

    /// 设置视口适配回调（由视口注入）
    void setFitToContentCallback(std::function<void()> callback);

    /// 设置视口居中回调
    void setCenterOnContentCallback(std::function<void()> callback);

    /// 设置视口刷新回调
    void setRefreshCallback(std::function<void()> callback);

    /// 将视口缩放到适合所有内容
    void fitToContent();

    /// 将视口居中到内容
    void centerOnContent();

    /// 刷新视口
    void refresh();

private:
    /// 视口适配回调
    std::function<void()> m_fitCallback;
    /// 视口居中回调
    std::function<void()> m_centerCallback;
    /// 视口刷新回调
    std::function<void()> m_refreshCallback;
};
