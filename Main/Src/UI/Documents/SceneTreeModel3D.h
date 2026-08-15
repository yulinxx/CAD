#pragma once

/**
 * @file SceneTreeModel3D.h
 * @brief 3D 场景树数据模型（纯数据层）
 *
 * 本文件只定义纯数据结构，不依赖任何引擎类型、Qt 控件或算法实现。
 * 三方职责划分（与 2D 场景树 / 属性面板 PropertyModel 同一解耦范式）：
 *   - 算法/领域层：SceneTreeBuilder3D 根据引擎场景生成本模型
 *   - 数据层：本文件（SceneTreeNode3D / SceneTreeModel3D，纯数据）
 *   - UI 层：SceneTreePanel3D 仅消费本模型进行渲染
 *
 * 因此 3D 场景树面板（UI）可以随时新增/删除/替换/定制，不影响算法层与数据层。
 */

#include <QList>
#include <QSet>
#include <QString>

/// 3D 场景树节点（纯数据）
struct SceneTreeNode3D
{
    /// 引擎图元 ID（字符串化，跨模块稳定标识）
    QString id;
    /// 显示名（图元名，空则回退到类型名）
    QString displayName;
    /// 类型显示名（如 "Mesh"）
    QString typeName;
    /// 附加信息（如三角形数量，可为空）
    QString info;
    /// 是否被选中
    bool selected = false;
    /// 是否可见
    bool visible = true;
    /// 是否锁定
    bool locked = false;
    /// 子节点（3D 场景目前为扁平结构，预留层级扩展）
    QList<SceneTreeNode3D> children;
};

/// 3D 场景树数据模型（纯数据）
struct SceneTreeModel3D
{
    /// 顶层节点列表
    QList<SceneTreeNode3D> nodes;
    /// 节点总数（含子节点）
    int totalCount = 0;
    /// 选中节点数
    int selectedCount = 0;
};
