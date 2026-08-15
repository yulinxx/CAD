#pragma once

/**
 * @file SceneTreeBuilder2D.h
 * @brief 2D 场景树构建器（算法/领域层）
 *
 * 职责：读取 Engine2D 场景（SceneManager + LayerManager），生成
 * SceneTreeModel2D 纯数据模型。本层封装所有引擎访问细节，不依赖任何
 * UI 控件；UI 层（SceneTreePanel2D）只消费生成的数据模型。
 *
 * 与 EntityPropertyModel2D 同范式：static 纯函数，无状态，便于单测。
 */

#include <QSet>

#include "SceneTreeModel2D.h"

namespace Eg
{
    class SceneManager;
    class SyGroup;
    struct SyEntity;
}

/// LayerManager 位于全局命名空间（见 Engine2D/Interaction/LayerManager.h）
class LayerManager;

class SceneTreeBuilder2D
{
public:
    /// 从引擎场景构建 2D 场景树数据模型
    /// @param scene 引擎 2D 场景（可为 nullptr，返回空模型）
    /// @param layers 图层管理器（可为 nullptr，图元无图层信息）
    static SceneTreeModel2D build(Eg::SceneManager* scene, LayerManager* layers);

    /// 查询引擎场景当前选中的图元 ID 集合
    static QSet<QString> selectedIds(Eg::SceneManager* scene);

private:
    /// 将引擎图元类型转为显示名
    static QString typeName(int eType);
    /// 递归构建群组节点（含子群组与成员）
    static SceneTreeNode2D buildGroupNode(Eg::SceneManager* scene, LayerManager* layers, Eg::SyGroup* group);
    /// 构建单个图元节点
    static SceneTreeNode2D buildEntityNode(Eg::SceneManager* scene, LayerManager* layers, const Eg::SyEntity* entity);
};
