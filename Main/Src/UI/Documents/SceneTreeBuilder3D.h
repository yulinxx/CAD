#pragma once

/**
 * @file SceneTreeBuilder3D.h
 * @brief 3D 场景树构建器（算法/领域层）
 *
 * 职责：读取 Engine3D 场景（SceneManager3D），生成 SceneTreeModel3D
 * 纯数据模型。本层封装所有引擎访问细节，不依赖任何 UI 控件；UI 层
 * （SceneTreePanel3D）只消费生成的数据模型。
 *
 * 与 SceneTreeBuilder2D 同范式：static 纯函数，无状态，便于单测与替换。
 */

#include <QSet>

#include "SceneTreeModel3D.h"

namespace Eg
{
    class SceneManager3D;
    struct SyMeshEntity;
}

class SceneTreeBuilder3D
{
public:
    /// 从引擎 3D 场景构建场景树数据模型
    /// @param scene 引擎 3D 场景（可为 nullptr，返回空模型）
    static SceneTreeModel3D build(Eg::SceneManager3D* scene);

    /// 查询引擎场景当前选中的图元 ID 集合
    static QSet<QString> selectedIds(Eg::SceneManager3D* scene);

private:
    /// 将单个网格图元转为数据节点（selected 由场景选择列表提供，非实体标志位）
    static SceneTreeNode3D buildMeshNode(const struct Eg::SyMeshEntity* mesh, bool selected);
};
