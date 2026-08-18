#pragma once

/**
 * @file UiSceneTreePanel2D.h
 * @brief 2D 场景树面板（UI 层，纯 Qt Widgets，面向百万级图元）
 *
 * 本控件使用 **QTreeView + 自写 QAbstractItemModel**，模型实现
 * `canFetchMore/fetchMore`：群组展开时才懒加载其成员，绝不 `expandAll`，
 * 因此上百万图元也不会为每个图元生成控件节点（视图按可见区虚拟化渲染）。
 *
 * 解耦设计（与属性面板同一范式）：
 *   - 数据层：SceneTreeTopology2D / SceneTreeRow2D / SceneTreeRowMeta2D
 *   - 算法层：SceneTreeBuilder2D（通过注入的 MetaProvider / ChildrenProvider）
 *   - UI 层：本类，只消费数据 + 回调，不感知引擎细节
 *
 * 本面板可随时被替换/移除/定制（换成列表、第三方树等），只要继续消费同一
 * 数据与回调并发出相同信号即可，不影响业务逻辑。
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

struct SceneTreeTopology2D;
struct SceneTreeRow2D;
struct SceneTreeRowMeta2D;

class QMenu;
class QTreeView;
class SceneTreeTableModel2D;

class SceneTreePanel2D final : public QWidget
{
    Q_OBJECT

public:
    /// 行元数据提供者（UI 不感知引擎，由控制器注入算法层）
    using MetaProvider = std::function<SceneTreeRowMeta2D(qint64 id, bool isGroup)>;
    /// 群组成员懒加载提供者（群组展开时调用）
    using ChildrenProvider = std::function<QVector<SceneTreeRow2D>(qint64 groupId)>;

    explicit SceneTreePanel2D(QWidget* parent = nullptr);
    ~SceneTreePanel2D() override;

    /// 设置拓扑 + 元数据/成员提供者（会 reset 模型，但不展开任何节点）
    void setTopology(const SceneTreeTopology2D& topology, MetaProvider metaProvider, ChildrenProvider childrenProvider);

    /// 仅更新选中高亮（不重建拓扑）
    void setSelectedIds(const QSet<QString>& ids);

    /// 当前选中的实体节点 ID 列表（不含群组）
    QStringList selectedIds() const;

signals:
    /// 用户在树中改变选择（ids 为选中的引擎图元 ID）
    void selectionChanged(const QStringList& ids);
    /// 双击/回车激活节点
    void itemActivated(const QString& id);
    /// 可见性切换请求
    void visibilityToggled(const QString& id, bool visible);
    /// 重命名请求
    void renameRequested(const QString& id, const QString& newName);
    /// 批量删除请求（ids 为选中的实体 id）
    void deleteRequested(const QStringList& ids);
    /// 批量显示/隐藏请求
    void batchVisibilityRequested(const QStringList& ids, bool visible);
    /// 批量锁定/解锁请求
    void batchLockRequested(const QStringList& ids, bool locked);

private:
    void onModelSelectionChanged();
    /// 语言切换时刷新缓存的上下文菜单文本（菜单仅在构造时构建一次）
    void retranslateMenu();
    /// 右键上下文菜单：对当前多选实体执行显示/隐藏/锁定/删除等批量操作
    void showContextMenu(const QPoint& pos);
    /// 全选顶层行（用 QItemSelection 区间，O(1)，不逐行枚举）
    void selectAllRows();

private:
    QTreeView* m_view{ nullptr };
    QMenu* m_contextMenu{ nullptr };
    SceneTreeTableModel2D* m_model{ nullptr };
    /// 程序化同步选中期间置位，抑制 selectionChanged 信号回环
    bool m_syncing{ false };
};
