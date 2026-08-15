#pragma once

/**
 * @file SceneTreeBuilder2D.h
 * @brief 2D 场景树构建器（算法/领域层，面向百万级图元优化）
 *
 * 职责：从 Engine2D 场景（SceneManager + LayerManager）按需生成场景树的
 * 紧凑拓扑与行元数据，封装所有引擎访问细节，不依赖任何 UI 控件。
 *
 * 面向百万级优化：
 *   - buildTopology() 一次性生成顶层索引（O(N)），不做节点/字符串堆叠
 *   - groupMembers() 懒加载群组成员（群组展开时才调用）
 *   - rowMeta() 按行 O(1) 读取元数据，仅对可见行调用
 *
 * 与 EntityPropertyModel2D 同范式：static 纯函数，无状态，便于单测。
 */

#include <QSet>
#include <QString>
#include <QVector>

#include "SceneTreeModel2D.h"

namespace Eg
{
    class SceneManager;
    class LayerManager;
    enum class EType : std::int16_t;
}

/// LayerManager 位于全局命名空间（见 Engine2D/Interaction/LayerManager.h）
class LayerManager;

class SceneTreeBuilder2D
{
public:
    /// 构建顶层拓扑索引（顶层群组 + 未入群组的图元）；O(N) 一次
    static SceneTreeTopology2D buildTopology(Eg::SceneManager* scene);

    /// 懒加载某群组的直接成员（子群组 + 图元）；群组展开时才调用
    static QVector<SceneTreeRow2D> groupMembers(Eg::SceneManager* scene, qint64 groupId);

    /// 按行读取展示元数据（O(1)）；仅对可见行调用
    static SceneTreeRowMeta2D rowMeta(Eg::SceneManager* scene, LayerManager* layers, const SceneTreeRow2D& row);

    /// 查询引擎场景当前选中的图元 ID 集合
    static QSet<QString> selectedIds(Eg::SceneManager* scene);

private:
    /// 将引擎图元类型转为显示名
    static QString typeName(Eg::EType eType);
    /// 递归收集某群组的所有成员图元 ID（用于从顶层剔除已入群组的图元）
    static void collectGroupedEntityIds(Eg::SceneManager* scene, qint64 groupId, QSet<qint64>& out);
};
