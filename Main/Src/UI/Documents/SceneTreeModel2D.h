#pragma once

/**
 * @file SceneTreeModel2D.h
 * @brief 2D 场景树数据模型（纯数据层）
 *
 * 本文件只定义纯数据结构，不依赖任何引擎类型、Qt 控件或算法实现。
 * 三方职责划分（与属性面板 PropertyModel 同一解耦范式）：
 *   - 算法/领域层：SceneTreeBuilder2D 根据引擎场景生成本模型
 *   - 数据层：本文件（SceneTreeNode2D / SceneTreeModel2D，纯数据）
 *   - UI 层：SceneTreePanel2D 仅消费本模型进行渲染
 *
 * 因此 Scene 页（树）可以随时新增/删除/替换/定制，不影响算法层与数据层。
 */

#include <QList>
#include <QSet>
#include <QString>

/// 2D 场景树节点（纯数据）
struct SceneTreeNode2D
{
    /// 引擎图元/群组 ID（字符串化，跨模块稳定标识）
    QString id;
    /// 显示名（图元名，空则回退到类型名）
    QString displayName;
    /// 类型显示名（如 "Line" / "Circle" / "Group"）
    QString typeName;
    /// 所属图层名（无图层时为空）
    QString layerName;
    /// 是否被选中
    bool selected = false;
    /// 是否可见
    bool visible = true;
    /// 是否锁定
    bool locked = false;
    /// 是否为群组节点（群组成员作为其子节点）
    bool isGroup = false;
    /// 子节点（群组成员）
    QList<SceneTreeNode2D> children;
};

/// 2D 场景树数据模型（纯数据）
struct SceneTreeModel2D
{
    /// 顶层节点列表（图元 + 顶层群组）
    QList<SceneTreeNode2D> nodes;
    /// 节点总数（含子节点）
    int totalCount = 0;
    /// 选中节点数
    int selectedCount = 0;
};
