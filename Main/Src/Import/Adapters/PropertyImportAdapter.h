#pragma once

#include <functional>

/// 属性面板导入适配器：导入完成后刷新属性面板
class PropertyImportAdapter
{
public:
    PropertyImportAdapter() = default;

    /// 设置选择刷新回调（由属性面板注入）
    void setRefreshSelectionCallback(std::function<void()> callback);

    /// 刷新当前选择对象的属性显示
    void refreshSelection();

private:
    /// 选择刷新回调
    std::function<void()> m_refreshCallback;
};
