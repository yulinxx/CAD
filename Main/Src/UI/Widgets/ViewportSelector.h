#pragma once

#include <optional>

#include "BBox/BBox2d.hpp"

class ISelectionService;

namespace Eg
{
    class ISceneContext;
}

// 选中集几何查询器：将 ISelectionService 中的选中 ID 集合转换为世界坐标包围盒，
// 供 zoom_selection 等视图操作使用。不参与输入分发。
class ViewportSelector
{
public:
    ViewportSelector(Eg::ISceneContext* sceneContext, ISelectionService* selectionService);

    /// 计算选中图元的合并 BBox，返回 nullopt 表示无选中或 BBox 无效
    std::optional<Ut::BBox2d> selectionBBox() const;

    void setSceneContext(Eg::ISceneContext* ctx) { m_sceneContext = ctx; }
    void setSelectionService(ISelectionService* svc) { m_selectionService = svc; }

private:
    Eg::ISceneContext* m_sceneContext;
    ISelectionService* m_selectionService;
};
