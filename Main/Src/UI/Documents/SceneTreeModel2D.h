#pragma once

/**
 * @file SceneTreeModel2D.h
 * @brief 2D 场景树数据模型（纯数据层，面向百万级图元优化）
 *
 * 本文件只定义纯数据结构，不依赖任何引擎类型、Qt 控件或算法实现。
 * 设计要点：面向"上百万图元"场景，采用**懒加载 + 紧凑拓扑索引**，
 * 避免为每个图元生成节点/字符串。
 *
 * 三层职责划分（与属性面板 PropertyModel 同一解耦范式）：
 *   - 算法/领域层：SceneTreeBuilder2D 从引擎生成拓扑与按需元数据
 *   - 数据层：本文件（SceneTreeRow2D / SceneTreeRowMeta2D / SceneTreeTopology2D）
 *   - UI 层：SceneTreePanel2D（QTreeView + QAbstractItemModel）只消费本模型
 *
 * 拓扑只保存 id + 是否群组，元数据（名称/类型/图层/可见/锁定/选中）按行懒读取，
 * 因此 1M 图元也只产生 1M 个紧凑的行条目，而非 1M 个控件节点。
 */

#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

/// 场景树一行（紧凑：仅 id + 是否群组）
struct SceneTreeRow2D
{
    /// 引擎图元/群组 ID
    qint64 id = 0;
    /// 是否为群组节点（群组成员通过懒加载按需获取）
    bool isGroup = false;

    bool operator==(const SceneTreeRow2D& o) const
    {
        return id == o.id && isGroup == o.isGroup;
    }
};

/// 场景树一行的展示元数据（按需懒读取，字段均为纯数据）
struct SceneTreeRowMeta2D
{
    /// 显示名（图元名，空则回退到类型名）
    QString displayName;
    /// 类型显示名（如 "Line" / "Circle" / "Group"）
    QString typeName;
    /// 所属图层名（无图层时为空）
    QString layerName;
    /// 是否可见
    bool visible = true;
    /// 是否锁定
    bool locked = false;
    /// 是否被选中
    bool selected = false;
};

/// 2D 场景树拓扑（纯数据，紧凑索引）
/// topLevel 为顶层行（顶层群组 + 未入群组的图元）；群组成员不在其中，
/// 由 UI 模型通过 ChildrenProvider 在群组展开时懒加载。
struct SceneTreeTopology2D
{
    /// 顶层行：顶层群组 + 未入群组的图元
    QVector<SceneTreeRow2D> topLevel;
};
