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
#include <QWidget>

#include <functional>

struct SceneTreeTopology2D;
struct SceneTreeRow2D;
struct SceneTreeRowMeta2D;
struct SceneTreeModel3D;

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

    /// 仅更新选中高亮（不重建拓扑）
    void setSelectedIds(const QSet<QString>& ids);

    /// 当前选中的实体节点 ID 列表
    QStringList selectedIds() const;

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
    /// 批量删除请求（ids 为选中的实体 id）
    void deleteRequested(const QStringList& ids);
    /// 批量显示/隐藏请求
    void batchVisibilityRequested(const QStringList& ids, bool visible);
    /// 批量锁定/解锁请求
    void batchLockRequested(const QStringList& ids, bool locked);

private:
    void onModelSelectionChanged();
    void retranslateMenu();
    void showContextMenu(const QPoint& pos);
    void selectAllRows();

    void setModel2D(const SceneTreeTopology2D& topology, MetaProvider2D metaProvider, ChildrenProvider2D childrenProvider);
    void setModel3D(const SceneTreeModel3D& model);

private:
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
