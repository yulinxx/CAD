#include "UiSceneTreePanel2D.h"

#include "SceneTreeModel2D.h"
#include "UI/LanguageManager.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QModelIndex>
#include <QTreeView>
#include <QVariant>
#include <QVBoxLayout>

// ============================================================
// 内部懒加载模型：QTreeView + QAbstractItemModel
// 顶层行来自紧凑拓扑；群组成员通过 canFetchMore/fetchMore 懒加载。
// Q_OBJECT 定义在 .cpp 内，由 Qt AUTOMOC 处理。
// ============================================================
class SceneTreeTableModel2D : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColName = 0,  // 名称（可双击重命名）
        ColType,      // 类型
        ColLayer,     // 图层
        ColVisible,   // 可见性（复选框）
        ColumnCount
    };

    explicit SceneTreeTableModel2D(QObject* parent = nullptr)
        : QAbstractItemModel(parent)
    {
    }

    void setTopology(const SceneTreeTopology2D& topo,
        SceneTreePanel2D::MetaProvider meta,
        SceneTreePanel2D::ChildrenProvider children)
    {
        beginResetModel();
        m_topLevel = topo.topLevel;
        m_meta = std::move(meta);
        m_children = std::move(children);
        m_groupChildren.clear();
        m_childParent.clear();
        m_loadedGroups.clear();
        m_isGroup.clear();
        m_topLevelRowById.clear();
        for (int r = 0; r < m_topLevel.size(); ++r)
        {
            if (m_topLevel[r].isGroup)
            {
                m_isGroup.insert(m_topLevel[r].id);
            }
            m_topLevelRowById.insert(m_topLevel[r].id, r);
        }
        endResetModel();
    }

    bool isGroupId(qint64 id) const
    {
        return m_isGroup.contains(id);
    }

    QModelIndex indexForId(qint64 id) const
    {
        auto it = m_topLevelRowById.constFind(id);
        if (it != m_topLevelRowById.constEnd())
        {
            return createIndex(it.value(), 0, static_cast<quintptr>(id));
        }
        // 已加载的群组成员
        auto pit = m_childParent.constFind(id);
        if (pit != m_childParent.constEnd())
        {
            const qint64 gid = pit.value();
            const QModelIndex p = indexForGroup(gid);
            auto kids = m_groupChildren.constFind(gid);
            if (p.isValid() && kids != m_groupChildren.constEnd())
            {
                for (int r = 0; r < kids->size(); ++r)
                {
                    if (kids->at(r).id == id)
                    {
                        return createIndex(r, 0, static_cast<quintptr>(id));
                    }
                }
            }
        }
        return {};
    }

    // ---- QAbstractItemModel overrides ----

    QModelIndex index(int row, int column, const QModelIndex& parent) const override
    {
        if (column < 0 || column >= ColumnCount || row < 0)
        {
            return {};
        }
        if (parent.isValid())
        {
            const qint64 gid = static_cast<qint64>(parent.internalId());
            auto it = m_groupChildren.constFind(gid);
            if (it == m_groupChildren.constEnd() || row >= it->size())
            {
                return {};
            }
            return createIndex(row, column, static_cast<quintptr>(it->at(row).id));
        }
        if (row >= m_topLevel.size())
        {
            return {};
        }
        return createIndex(row, column, static_cast<quintptr>(m_topLevel[row].id));
    }

    QModelIndex parent(const QModelIndex& child) const override
    {
        if (!child.isValid())
        {
            return {};
        }
        const qint64 id = static_cast<qint64>(child.internalId());
        auto it = m_childParent.constFind(id);
        if (it == m_childParent.constEnd())
        {
            return {};  // 顶层
        }
        return indexForGroup(it.value());
    }

    int rowCount(const QModelIndex& parent) const override
    {
        if (!parent.isValid())
        {
            return m_topLevel.size();
        }
        const qint64 gid = static_cast<qint64>(parent.internalId());
        auto it = m_groupChildren.constFind(gid);
        return (it == m_groupChildren.constEnd()) ? 0 : it->size();
    }

    int columnCount(const QModelIndex& /*parent*/) const override
    {
        return ColumnCount;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid())
        {
            return {};
        }
        const qint64 id = static_cast<qint64>(index.internalId());
        const bool isGroup = m_isGroup.contains(id);

        if (role == Qt::DisplayRole)
        {
            const SceneTreeRowMeta2D meta = m_meta ? m_meta(id, isGroup) : SceneTreeRowMeta2D();
            switch (index.column())
            {
            case ColName:
                return meta.displayName;
            case ColType:
                return meta.typeName;
            case ColLayer:
                return meta.layerName;
            default:
                return {};
            }
        }
        if (role == Qt::CheckStateRole && index.column() == ColVisible)
        {
            const SceneTreeRowMeta2D meta = m_meta ? m_meta(id, isGroup) : SceneTreeRowMeta2D();
            return meta.visible ? Qt::Checked : Qt::Unchecked;
        }
        if (role == Qt::ToolTipRole && index.column() == ColName)
        {
            const SceneTreeRowMeta2D meta = m_meta ? m_meta(id, isGroup) : SceneTreeRowMeta2D();
            return meta.displayName;
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        {
            return {};
        }
        switch (section)
        {
        case ColName:
            return tr("Name");
        case ColType:
            return tr("Type");
        case ColLayer:
            return tr("Layer");
        case ColVisible:
            return tr("Vis");
        default:
            return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        if (!index.isValid())
        {
            return Qt::NoItemFlags;
        }
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == ColName)
        {
            f |= Qt::ItemIsEditable;
        }
        if (index.column() == ColVisible)
        {
            f |= Qt::ItemIsUserCheckable;
        }
        return f;
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (!index.isValid())
        {
            return false;
        }
        const qint64 id = static_cast<qint64>(index.internalId());
        if (index.column() == ColVisible && role == Qt::CheckStateRole)
        {
            emit sigVisibilityToggled(id, value.toInt() == Qt::Checked);
            return true;
        }
        if (index.column() == ColName && role == Qt::EditRole)
        {
            emit sigRenameRequested(id, value.toString());
            return true;
        }
        return false;
    }

    bool hasChildren(const QModelIndex& parent) const override
    {
        if (!parent.isValid())
        {
            return !m_topLevel.isEmpty();
        }
        const qint64 id = static_cast<qint64>(parent.internalId());
        if (!m_isGroup.contains(id))
        {
            return false;
        }
        auto it = m_groupChildren.constFind(id);
        if (it == m_groupChildren.constEnd())
        {
            return true;  // 群组未加载 → 显示展开箭头，点击时 fetchMore
        }
        return !it->isEmpty();
    }

    bool canFetchMore(const QModelIndex& parent) const override
    {
        if (!parent.isValid())
        {
            return false;
        }
        const qint64 id = static_cast<qint64>(parent.internalId());
        if (!m_isGroup.contains(id))
        {
            return false;
        }
        return !m_loadedGroups.contains(id);
    }

    void fetchMore(const QModelIndex& parent) override
    {
        if (!parent.isValid())
        {
            return;
        }
        const qint64 gid = static_cast<qint64>(parent.internalId());
        if (m_loadedGroups.contains(gid))
        {
            return;
        }
        const QVector<SceneTreeRow2D> kids = m_children ? m_children(gid) : QVector<SceneTreeRow2D>();

        const int count = kids.size();
        if (count == 0)
        {
            m_loadedGroups.insert(gid);
            return;
        }

        beginInsertRows(parent, 0, count - 1);
        m_groupChildren.insert(gid, kids);
        for (int r = 0; r < count; ++r)
        {
            m_childParent.insert(kids[r].id, gid);
            if (kids[r].isGroup)
            {
                m_isGroup.insert(kids[r].id);
            }
        }
        m_loadedGroups.insert(gid);
        endInsertRows();
    }

signals:
    void sigVisibilityToggled(qint64 id, bool visible);
    void sigRenameRequested(qint64 id, const QString& newName);

private:
    QModelIndex indexForGroup(qint64 gid) const
    {
        auto it = m_topLevelRowById.constFind(gid);
        if (it != m_topLevelRowById.constEnd())
        {
            return createIndex(it.value(), 0, static_cast<quintptr>(gid));
        }
        // 嵌套群组：gid 作为某已加载群组的成员
        auto pit = m_childParent.constFind(gid);
        if (pit != m_childParent.constEnd())
        {
            const qint64 parentGid = pit.value();
            auto kids = m_groupChildren.constFind(parentGid);
            if (kids != m_groupChildren.constEnd())
            {
                for (int r = 0; r < kids->size(); ++r)
                {
                    if (kids->at(r).id == gid)
                    {
                        return createIndex(r, 0, static_cast<quintptr>(gid));
                    }
                }
            }
        }
        return {};
    }

private:
    SceneTreePanel2D::MetaProvider m_meta;
    SceneTreePanel2D::ChildrenProvider m_children;
    QVector<SceneTreeRow2D> m_topLevel;
    QHash<qint64, QVector<SceneTreeRow2D>> m_groupChildren;  // 已加载群组的成员
    QHash<qint64, qint64> m_childParent;                     // 已加载成员 id -> 所属群组 id
    QHash<qint64, int> m_topLevelRowById;                    // 顶层 id -> 行号（O(1) 选中定位）
    QSet<qint64> m_loadedGroups;                             // 已加载成员列表的群组
    QSet<qint64> m_isGroup;                                  // 已知为群组的 id
};

#include "UiSceneTreePanel2D.moc"

// ============================================================
// 面板实现
// ============================================================
SceneTreePanel2D::SceneTreePanel2D(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_model = new SceneTreeTableModel2D(this);
    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setAllColumnsShowFocus(true);
    m_view->setUniformRowHeights(true);
    // 所有列 Interactive：允许用户拖拽表头调整各列宽度
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(SceneTreeTableModel2D::ColName, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(SceneTreeTableModel2D::ColType, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(SceneTreeTableModel2D::ColLayer, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(SceneTreeTableModel2D::ColVisible, QHeaderView::Interactive);
    // 绝不 expandAll；群组仅在用户点击展开箭头时通过 canFetchMore/fetchMore 懒加载
    m_view->setExpandsOnDoubleClick(true);
    layout->addWidget(m_view);

    connect(m_model, &SceneTreeTableModel2D::sigVisibilityToggled, this, [this](qint64 id, bool visible) {
        emit visibilityToggled(QString::number(id), visible);
    });
    connect(m_model, &SceneTreeTableModel2D::sigRenameRequested, this, [this](qint64 id, const QString& newName) {
        emit renameRequested(QString::number(id), newName);
    });
    connect(m_view->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        this,
        &SceneTreePanel2D::onModelSelectionChanged);
    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid())
        {
            emit itemActivated(QString::number(static_cast<qint64>(index.internalId())));
        }
    });

    // 右键批量操作菜单（显示/隐藏/锁定/解锁/删除/全选/清空）
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTreeView::customContextMenuRequested, this, &SceneTreePanel2D::showContextMenu);
    m_contextMenu = new QMenu(this);
    auto* actShow = m_contextMenu->addAction(tr("Show"));
    actShow->setObjectName(QStringLiteral("ctxShow"));
    auto* actHide = m_contextMenu->addAction(tr("Hide"));
    actHide->setObjectName(QStringLiteral("ctxHide"));
    m_contextMenu->addSeparator();
    auto* actLock = m_contextMenu->addAction(tr("Lock"));
    actLock->setObjectName(QStringLiteral("ctxLock"));
    auto* actUnlock = m_contextMenu->addAction(tr("Unlock"));
    actUnlock->setObjectName(QStringLiteral("ctxUnlock"));
    m_contextMenu->addSeparator();
    auto* actDelete = m_contextMenu->addAction(tr("Delete"));
    actDelete->setObjectName(QStringLiteral("ctxDelete"));
    m_contextMenu->addSeparator();
    auto* actSelectAll = m_contextMenu->addAction(tr("Select All"));
    actSelectAll->setObjectName(QStringLiteral("ctxSelectAll"));
    auto* actClear = m_contextMenu->addAction(tr("Clear Selection"));
    actClear->setObjectName(QStringLiteral("ctxClear"));

    connect(actShow, &QAction::triggered, this, [this]() {
        emit batchVisibilityRequested(selectedIds(), true);
    });
    connect(actHide, &QAction::triggered, this, [this]() {
        emit batchVisibilityRequested(selectedIds(), false);
    });
    connect(actLock, &QAction::triggered, this, [this]() {
        emit batchLockRequested(selectedIds(), true);
    });
    connect(actUnlock, &QAction::triggered, this, [this]() {
        emit batchLockRequested(selectedIds(), false);
    });
    connect(actDelete, &QAction::triggered, this, [this]() {
        emit deleteRequested(selectedIds());
    });
    connect(actSelectAll, &QAction::triggered, this, &SceneTreePanel2D::selectAllRows);
    connect(actClear, &QAction::triggered, this, [this]() {
        if (m_view && m_view->selectionModel())
        {
            m_syncing = true;
            m_view->selectionModel()->clearSelection();
            m_syncing = false;
        }
    });

    // 上下文菜单只在构造时构建一次，需监听语言切换以刷新其缓存的文本
    if (auto* lm = LanguageManager::instance())
    {
        connect(lm, &LanguageManager::languageChanged, this, &SceneTreePanel2D::retranslateMenu);
    }
    retranslateMenu();
}

void SceneTreePanel2D::retranslateMenu()
{
    if (!m_contextMenu)
    {
        return;
    }
    for (QAction* act : m_contextMenu->actions())
    {
        const QString on = act->objectName();
        if (on == QStringLiteral("ctxShow"))
        {
            act->setText(tr("Show"));
        }
        else if (on == QStringLiteral("ctxHide"))
        {
            act->setText(tr("Hide"));
        }
        else if (on == QStringLiteral("ctxLock"))
        {
            act->setText(tr("Lock"));
        }
        else if (on == QStringLiteral("ctxUnlock"))
        {
            act->setText(tr("Unlock"));
        }
        else if (on == QStringLiteral("ctxDelete"))
        {
            act->setText(tr("Delete"));
        }
        else if (on == QStringLiteral("ctxSelectAll"))
        {
            act->setText(tr("Select All"));
        }
        else if (on == QStringLiteral("ctxClear"))
        {
            act->setText(tr("Clear Selection"));
        }
    }
}

SceneTreePanel2D::~SceneTreePanel2D() = default;

void SceneTreePanel2D::setTopology(
    const SceneTreeTopology2D& topology, MetaProvider metaProvider, ChildrenProvider childrenProvider)
{
    m_model->setTopology(topology, std::move(metaProvider), std::move(childrenProvider));
}

void SceneTreePanel2D::setSelectedIds(const QSet<QString>& ids)
{
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    QSet<qint64> idSet;
    for (const QString& s : ids)
    {
        bool ok = false;
        const qint64 v = s.toLongLong(&ok);
        if (ok)
        {
            idSet.insert(v);
        }
    }

    m_syncing = true;
    m_view->selectionModel()->clearSelection();
    QModelIndex firstIndex;
    for (qint64 id : idSet)
    {
        const QModelIndex idx = m_model->indexForId(id);
        if (!idx.isValid())
        {
            continue;
        }
        m_view->selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!firstIndex.isValid())
        {
            firstIndex = idx;
        }
    }

    // 定位到第一个选中项：展开其祖先并滚动到可见（若选中项位于未加载的群组内，
    // 无法定位，保持现状，仅对已物化的行生效）
    if (firstIndex.isValid())
    {
        QModelIndex parent = firstIndex.parent();
        while (parent.isValid())
        {
            m_view->expand(parent);
            parent = parent.parent();
        }
        m_view->scrollTo(firstIndex, QAbstractItemView::EnsureVisible);
    }
    m_syncing = false;
}

QStringList SceneTreePanel2D::selectedIds() const
{
    QStringList ids;
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return ids;
    }
    const auto rows = m_view->selectionModel()->selectedRows(0);
    for (const QModelIndex& index : rows)
    {
        if (!index.isValid())
        {
            continue;
        }
        const qint64 id = static_cast<qint64>(index.internalId());
        if (!m_model->isGroupId(id))
        {
            ids << QString::number(id);
        }
    }
    return ids;
}

void SceneTreePanel2D::onModelSelectionChanged()
{
    if (m_syncing)
    {
        return;
    }
    emit selectionChanged(selectedIds());
}

void SceneTreePanel2D::showContextMenu(const QPoint& pos)
{
    if (!m_contextMenu || !m_view)
    {
        return;
    }
    const bool hasSelection = !selectedIds().isEmpty();
    for (QAction* action : m_contextMenu->actions())
    {
        const QString name = action->objectName();
        if (name == QStringLiteral("ctxSelectAll") || name == QStringLiteral("ctxClear"))
        {
            action->setEnabled(true);
        }
        else
        {
            action->setEnabled(hasSelection);
        }
    }
    m_contextMenu->exec(m_view->viewport()->mapToGlobal(pos));
}

void SceneTreePanel2D::selectAllRows()
{
    if (!m_view || !m_model)
    {
        return;
    }
    const int rows = m_model->rowCount(QModelIndex());
    if (rows <= 0)
    {
        return;
    }
    // 用 QItemSelection 区间全选顶层行（O(1) 记录范围，不逐行枚举，百万级不卡）
    const QModelIndex first = m_model->index(0, 0, QModelIndex());
    const QModelIndex last = m_model->index(rows - 1, m_model->columnCount(QModelIndex()) - 1, QModelIndex());
    if (!first.isValid() || !last.isValid())
    {
        return;
    }
    m_syncing = true;
    m_view->selectionModel()->select(
        QItemSelection(first, last), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    m_syncing = false;
}