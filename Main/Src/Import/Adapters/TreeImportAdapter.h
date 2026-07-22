#pragma once

#include <functional>

/// 场景树导入适配器：导入完成后刷新场景树结构
class TreeImportAdapter
{
public:
    TreeImportAdapter() = default;

    /// 设置树重建回调（由场景树面板注入）
    void setRebuildCallback(std::function<void()> callback);

    /// 重建场景树
    void rebuild();

private:
    /// 树重建回调
    std::function<void()> m_rebuildCallback;
};
