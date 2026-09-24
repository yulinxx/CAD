#pragma once

/**
 * @file UiSceneTreePanel.h
 * @brief 统一场景树面板（UI 层，纯 Qt Widgets，面向百万级图元）
 *
 * 本控件使用 **QTreeView + 自写 QAbstractItemModel**，支持 2D 和 3D 数据模型。
 * 2D 模型实现 `canFetchMore/fetchMore` 懒加载，3D 数据直接全量加载。
 *
 * 解耦设计：
 *   - 数据层：SceneTreeTopology2D / SceneTreeModel3D
 *   - 算法层：SceneTreeBuilder2D / SceneTreeBuilder3D
 *   - UI 层：本类，只消费数据 + 回调，不感知引擎细节
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

struct SceneTreeTopology2D;
struct SceneTreeRow2D;
struct SceneTreeRowMeta2D;
struct SceneTreeModel3D;
struct SceneTreeNode3D;

class QModelIndex;
class QMenu;
class QTreeView;
class QAbstractItemModel;

class SceneTreePanel final : public QWidget
{
    Q_OBJECT

public:
    /// 2D 行元数据提供者（UI 不感知引擎，由控制器注入算法层）
    using MetaProvider2D = std::function<SceneTreeRowMeta2D(qint64 id, bool isGroup)>;
    /// 2D 群组成员懒加载提供者（群组展开时调用）
    using ChildrenProvider2D = std::function<QVector<SceneTreeRow2D>(qint64 groupId)>;

    enum class Mode
    {
        Mode2D,
        Mode3D
    };

    explicit SceneTreePanel(QWidget* parent = nullptr);
    ~SceneTreePanel() override;

    /// 设置为 2D 模式
    void setMode2D(const SceneTreeTopology2D& topology, MetaProvider2D metaProvider, ChildrenProvider2D childrenProvider);
    /// 设置为 3D 模式
    void setMode3D(const SceneTreeModel3D& model);

    /// 增量追加顶层行（仅 2D 模式；用于可安全增量表达的纯新增，不做整树 reset）
    void appendTopLevelRows(const QVector<SceneTreeRow2D>& rows);

    /// 增量追加顶层节点（仅 3D 模式；用于可安全增量表达的纯新增，不做整树 reset）
    void appendTopLevelNodes(const QList<SceneTreeNode3D>& nodes);

    /// 仅更新选中高亮（不重建拓扑）
    void setSelectedIds(const QSet<QString>& ids);

    /// 当前选中的图元节点 ID 列表
    QStringList selectedIds() const;

    /// 当前选中的图元节点 ID（整数）。批量显隐/锁定等热路径用它，
    /// 避免 N 个 QString 的构造、传递与再解析（全选百万图元时是主要开销）。
    QVector<qint64> selectedIdNumbers() const;

    /**
     * @brief 对指定 id 的行发 dataChanged（不重建拓扑）
     *
     * 显隐/锁定这类「只有行内容变了」的刷新走这里：逐 id 定位行（O(log N)）
     * 后只对命中的行发 dataChanged，不触发 buildTopology 的 O(N) 全量遍历，
     * 也不 reset 模型（保留展开/滚动状态）。
     */
    void refreshRows(const QVector<qint64>& ids);

    /// 批量改写 3D 复选框态 + refreshRows（右键 Show/Hide；不重建拓扑、不走 setData 回调）
    void setRowsVisible(const QVector<qint64>& ids, bool visible);

    /// 按引擎当前状态改写已有 3D 行的内容（Name/Visible 等）+ refreshRows。
    /// 属性面板编辑后反向同步树用：3D 模型是 QStandardItem 物化缓存，
    /// 仅发 dataChanged 不会从引擎回读，必须先写回行内容再通知视图。
    void updateNodes3D(const QList<SceneTreeNode3D>& nodes);

    /// 获取当前模式
    Mode mode() const { return m_mode; }

    /// 2D 专用：设置命令状态
    bool setCommandState(bool hasSelection, bool anyLocked);

signals:
    /// 用户在树中改变选择（ids 为选中的引擎图元 ID）
    void selectionChanged(const QStringList& ids);
    /// 双击/回车激活节点
    void itemActivated(const QString& id);
    /// 可见性切换请求
    void visibilityToggled(const QString& id, bool visible);
    /// 重命名请求
    void renameRequested(const QString& id, const QString& newName);
    /// 批量删除请求（ids 为选中的图元 id）
    void deleteRequested(const QStringList& ids);
    /// 批量显示/隐藏请求（整数 id，避免字符串转换开销）
    void batchVisibilityRequested(const QVector<qint64>& ids, bool visible);
    /// 批量锁定/解锁请求（整数 id）
    void batchLockRequested(const QVector<qint64>& ids, bool locked);

 private:
    void onModelSelectionChanged();
    void retranslateMenu();
    void showContextMenu(const QPoint& pos);
    void selectAllRows();

    /// 保存/恢复展开状态
    void collectExpandedIds(const QModelIndex& index, QStringList& outIds) const;
    void expandId(const QString& id);
    bool findAndExpandId(const QModelIndex& index, const QString& id);

    QTreeView* m_view{ nullptr };
    QMenu* m_contextMenu{ nullptr };
    QAbstractItemModel* m_model{ nullptr };
    Mode m_mode{ Mode::Mode2D };

    /// 程序化同步选中期间置位，抑制 selectionChanged 信号回环
    bool m_syncing{ false };

    /// 2D 模式专用
    bool m_hasSelection{ false };
    bool m_anyLocked{ false };
    MetaProvider2D m_metaProvider2D;
    ChildrenProvider2D m_childrenProvider2D;
};
