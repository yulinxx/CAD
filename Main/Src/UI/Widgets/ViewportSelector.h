#pragma once

#include <optional>

#include "BBox/BBox2d.hpp"

class ISelectionService;

namespace Eg
{
    class SceneManager;
}

// 选中集的几何查询器。
//
// 职责边界：本类**不参与输入分发**。点选 / 框选 / 命中判定全部由 SelectTool
// 经 ViewportInputRouter::dispatchToActiveTool 处理（SelectTool 常驻激活，
// 见 ToolManager::setActiveTool("SelectTool")），所以视口层不需要第二套拾取实现。
// 这里只保留一件事：把 ISelectionService 里的 ID 集合翻译成世界坐标包围盒，
// 供 zoom_selection（Ctrl+Shift+F）之类的视图操作使用。
//
// 选择的读写请直接用 ISelectionService / Eg::SceneManager，不要在这里加转发。
class ViewportSelector
{
public:
    ViewportSelector(Eg::SceneManager* sceneManager, ISelectionService* selectionService);

    // 计算选中图元的合并 BBox
    // 返回 nullopt 表示无选中、依赖未注入或所有选中项 BBox 均无效
    std::optional<Ut::BBox2d> selectionBBox() const;

    void setSceneManager(Eg::SceneManager* sm)
    {
        m_sceneManager = sm;
    }

    void setSelectionService(ISelectionService* svc)
    {
        m_selectionService = svc;
    }

private:
    Eg::SceneManager* m_sceneManager;
    ISelectionService* m_selectionService;
};
