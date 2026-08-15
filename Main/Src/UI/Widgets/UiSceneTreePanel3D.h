#pragma once

/**
 * @file UiSceneTreePanel3D.h
 * @brief 3D 场景树面板（UI 层，纯 Qt Widgets）
 *
 * 本控件只消费 SceneTreeModel3D（纯数据），并发出选择/可见性/重命名请求
 * 信号，完全不感知引擎与算法细节：
 *   - 数据层：SceneTreeModel3D
 *   - 算法层：SceneTreeBuilder3D（把引擎 3D 场景转成数据模型）
 *   - UI 层：本类
 *
 * 因此本面板可以随时被替换/移除/定制（例如换成自定义树、列表或第三方
 * 控件），只要继续消费同一数据模型并发出相同信号即可，不影响业务逻辑。
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

struct SceneTreeModel3D;

class QTreeWidget;
class QTreeWidgetItem;

class SceneTreePanel3D final : public QWidget
{
    Q_OBJECT

public:
    enum Column
    {
        ColName = 0,  // 名称（可双击重命名）
        ColType,      // 类型
        ColInfo,      // 附加信息（如三角形数量）
        ColumnCount
    };

    explicit SceneTreePanel3D(QWidget* parent = nullptr);

    /// 全量重建树（展示数据模型）
    void setModel(const SceneTreeModel3D& model);
    /// 仅更新选中高亮（不重建，避免折叠/展开状态丢失）
    void setSelectedIds(const QSet<QString>& ids);
    /// 当前选中的节点 ID 列表
    QStringList selectedIds() const;

signals:
    /// 用户在树中改变选择（ids 为选中的引擎图元 ID）
    void selectionChanged(const QStringList& ids);
    /// 双击激活节点
    void itemActivated(const QString& id);
    /// 可见性切换请求
    void visibilityToggled(const QString& id, bool visible);
    /// 重命名请求
    void renameRequested(const QString& id, const QString& newName);

private:
    void rebuildTree(const SceneTreeModel3D& model);
    void addNodeItem(QTreeWidgetItem* parent, const struct SceneTreeNode3D& node);
    QTreeWidgetItem* findItemById(const QString& id) const;
    void collectSelectedIds(QStringList& ids) const;

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onSelectionChanged();
    void onItemActivated(QTreeWidgetItem* item, int column);

private:
    QTreeWidget* m_tree{ nullptr };
    /// 程序化更新期间的抑制标志，避免 itemChanged 信号被误当作用户操作
    bool m_updating{ false };
};
