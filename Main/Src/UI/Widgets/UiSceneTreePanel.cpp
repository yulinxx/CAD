#include "UiSceneTreePanel.h"

#include "SceneTreeModel2D.h"
#include "SceneTreeModel3D.h"
#include "UI/LanguageManager.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QModelIndex>
#include <QStandardItemModel>
#include <QShortcut>
#include <QTreeView>
#include <QVariant>
#include <QVBoxLayout>

#include <functional>

namespace
{
    constexpr int kIdRole = Qt::UserRole;
    constexpr int kNameRole = Qt::UserRole + 1;
    constexpr int kVisRole = Qt::UserRole + 2;
    constexpr int kIsGroupRole = Qt::UserRole + 3;
}

// ============================================================
// 2D 模型：懒加载模型（从 UiSceneTreePanel2D.cpp 提取）
// ============================================================

class SceneTreeTableModel2D : public QAbstractItemModel
{
public:
    SceneTreeTableModel2D(QObject* parent = nullptr)
        : QAbstractItemModel(parent)
    {
    }

    void setTopology(const SceneTreeTopology2D& topology, SceneTreePanel::MetaProvider2D metaProvider,
                     SceneTreePanel::ChildrenProvider2D childrenProvider)
    {
        beginResetModel();
        m_topLevel = topology.topLevel;
        m_topLevelRowById.clear();
        for (int i = 0; i < m_topLevel.size(); ++i)
        {
            m_topLevelRowById[m_topLevel[i].id] = i;
        }
        m_metaProvider = std::move(metaProvider);
        m_childrenProvider = std::move(childrenProvider);
        m_groupChildren.clear();
        m_childParent.clear();
        m_isGroup.clear();
        for (const auto& row : m_topLevel)
        {
            if (row.isGroup)
            {
                m_isGroup.insert(row.id);
            }
        }
        endResetModel();
    }

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
    {
        if (!hasIndex(row, column, parent))
        {
            return {};
        }
        if (!parent.isValid())
        {
            return createIndex(row, column, static_cast<quintptr>(m_topLevel[row].id));
        }
        const qint64 parentId = static_cast<qintptr>(parent.internalId());
        auto it = m_groupChildren.constFind(parentId);
        if (it == m_groupChildren.constEnd() || row >= it->size())
        {
            return {};
        }
        const auto& child = (*it)[row];
        return createIndex(row, column, static_cast<quintptr>(child.id));
    }

    QModelIndex parent(const QModelIndex& index) const override
    {
        if (!index.isValid())
        {
            return {};
        }
        const qint64 id = static_cast<qintptr>(index.internalId());
        auto it = m_childParent.constFind(id);
        if (it == m_childParent.constEnd())
        {
            return {};
        }
        return indexForGroup(it.value());
    }

    int rowCount(const QModelIndex& parent) const override
    {
        if (!parent.isValid())
        {
            return m_topLevel.size();
        }
        const qint64 gid = static_cast<qintptr>(parent.internalId());
        auto it = m_groupChildren.constFind(gid);
        return (it == m_groupChildren.constEnd()) ? 0 : it->size();
    }

    int columnCount(const QModelIndex& /*parent*/) const override
    {
        return 3;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
        {
            return {};
        }
        const qint64 id = static_cast<qintptr>(index.internalId());
        const bool isGroup = m_isGroup.contains(id);

        if (role == kIsGroupRole)
        {
            return isGroup;
        }

        auto meta = m_metaProvider ? m_metaProvider(id, isGroup) : SceneTreeRowMeta2D{};

        if (role == Qt::CheckStateRole && index.column() == 0)
        {
            return meta.visible ? Qt::Checked : Qt::Unchecked;
        }
        if (role == kIdRole)
        {
            return QString::number(id);
        }
        if (role == kNameRole)
        {
            return meta.displayName;
        }
        if (role == kVisRole)
        {
            return meta.visible;
        }
        if (role == Qt::DisplayRole)
        {
            switch (index.column())
            {
            case 1:
                return meta.displayName;
            case 2:
                return meta.typeName;
            default:
                return {};
            }
        }
        return {};
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override
    {
        if (!index.isValid())
        {
            return false;
        }
        const qint64 id = static_cast<qintptr>(index.internalId());

        if (index.column() == 0 && role == Qt::CheckStateRole)
        {
            if (m_visibilityCallback)
            {
                m_visibilityCallback(id, value.toInt() == Qt::Checked);
            }
            return true;
        }
        if (index.column() == 1 && role == Qt::EditRole)
        {
            if (m_renameCallback)
            {
                m_renameCallback(id, value.toString());
            }
            return true;
        }
        return false;
    }

    void setVisibilityCallback(std::function<void(qint64, bool)> callback)
    {
        m_visibilityCallback = std::move(callback);
    }

    void setRenameCallback(std::function<void(qint64, const QString&)> callback)
    {
        m_renameCallback = std::move(callback);
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        if (!index.isValid())
        {
            return Qt::NoItemFlags;
        }
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == 0)
        {
            f |= Qt::ItemIsUserCheckable;
        }
        if (index.column() == 1)
        {
            f |= Qt::ItemIsEditable;
        }
        return f;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        {
            switch (section)
            {
            case 0:
                return QObject::tr("Vis");
            case 1:
                return QObject::tr("Name");
            case 2:
                return QObject::tr("Type");
            default:
                return {};
            }
        }
        return {};
    }

    bool canFetchMore(const QModelIndex& parent) const override
    {
        if (!parent.isValid())
        {
            return false;
        }
        const qint64 gid = static_cast<qintptr>(parent.internalId());
        return m_isGroup.contains(gid) && !m_groupChildren.contains(gid);
    }

    void fetchMore(const QModelIndex& parent) override
    {
        if (!parent.isValid())
        {
            return;
        }
        const qint64 gid = static_cast<qintptr>(parent.internalId());
        if (!m_isGroup.contains(gid) || m_groupChildren.contains(gid))
        {
            return;
        }
        auto children = m_childrenProvider ? m_childrenProvider(gid) : QVector<SceneTreeRow2D>{};
        m_groupChildren[gid] = children;
        for (const auto& child : children)
        {
            m_childParent[child.id] = gid;
            if (child.isGroup)
            {
                m_isGroup.insert(child.id);
            }
        }
    }

    QModelIndex indexForId(qint64 id) const
    {
        auto it = m_topLevelRowById.constFind(id);
        if (it != m_topLevelRowById.constEnd())
        {
            return createIndex(it.value(), 0, static_cast<quintptr>(id));
        }
        for (auto it = m_groupChildren.constBegin(); it != m_groupChildren.constEnd(); ++it)
        {
            const auto& children = it.value();
            for (int i = 0; i < children.size(); ++i)
            {
                if (children[i].id == id)
                {
                    return createIndex(i, 0, static_cast<quintptr>(id));
                }
            }
        }
        return {};
    }

private:
    QModelIndex indexForGroup(qint64 gid) const
    {
        auto it = m_topLevelRowById.constFind(gid);
        if (it != m_topLevelRowById.constEnd())
        {
            return createIndex(it.value(), 0, static_cast<quintptr>(gid));
        }
        auto pit = m_childParent.constFind(gid);
        if (pit != m_childParent.constEnd())
        {
            const qint64 parentGid = pit.value();
            auto kids = m_groupChildren.constFind(parentGid);
            if (kids != m_groupChildren.constEnd())
            {
                for (int r = 0; r < kids->size(); ++r)
                {
                    if ((*kids)[r].id == gid)
                    {
                        return createIndex(r, 0, static_cast<quintptr>(gid));
                    }
                }
            }
        }
        return {};
    }

    QVector<SceneTreeRow2D> m_topLevel;
    QMap<qint64, int> m_topLevelRowById;
    QMap<qint64, QVector<SceneTreeRow2D>> m_groupChildren;
    QMap<qint64, qint64> m_childParent;
    QSet<qint64> m_isGroup;
    SceneTreePanel::MetaProvider2D m_metaProvider;
    SceneTreePanel::ChildrenProvider2D m_childrenProvider;
    std::function<void(qint64, bool)> m_visibilityCallback;
    std::function<void(qint64, const QString&)> m_renameCallback;

    friend class SceneTreePanel;
};

// ============================================================
// 3D 模型：使用 QStandardItemModel
// ============================================================

class SceneTreeTableModel3D : public QStandardItemModel
{
public:
    SceneTreeTableModel3D()
        : QStandardItemModel(0, 3)
    {
    }

    void setData(const SceneTreeModel3D& model)
    {
        beginResetModel();
        clear();
        m_nodeMap.clear();

        for (const auto& node : model.nodes)
        {
            addNode(nullptr, node);
        }
        endResetModel();
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override
    {
        if (!index.isValid())
        {
            return false;
        }

        if (index.column() == 0 && role == Qt::CheckStateRole)
        {
            const QString id = index.sibling(index.row(), 1).data(kIdRole).toString();
            if (m_visibilityCallback)
            {
                m_visibilityCallback(id, value.toInt() == Qt::Checked);
            }
            // Call parent to actually update the check state
            return QStandardItemModel::setData(index, value, role);
        }
        if (index.column() == 1 && role == Qt::EditRole)
        {
            const QString id = index.data(kIdRole).toString();
            if (m_renameCallback)
            {
                m_renameCallback(id, value.toString());
            }
            return QStandardItemModel::setData(index, value, role);
        }
        return QStandardItemModel::setData(index, value, role);
    }

    void setVisibilityCallback(std::function<void(const QString&, bool)> callback)
    {
        m_visibilityCallback = std::move(callback);
    }

    void setRenameCallback(std::function<void(const QString&, const QString&)> callback)
    {
        m_renameCallback = std::move(callback);
    }

private:
    void addNode(QStandardItem* parent, const SceneTreeNode3D& node)
    {
        auto* visibleItem = new QStandardItem();
        visibleItem->setCheckable(true);
        visibleItem->setCheckState(node.visible ? Qt::Checked : Qt::Unchecked);
        visibleItem->setData(node.id, kIdRole);
        visibleItem->setData(node.visible, kVisRole);

        auto* nameItem = new QStandardItem(node.displayName);
        nameItem->setData(node.id, kIdRole);
        nameItem->setData(node.displayName, kNameRole);

        auto* typeItem = new QStandardItem(node.typeName);

        QList<QStandardItem*> row = { visibleItem, nameItem, typeItem };

        if (parent)
        {
            parent->appendRow(row);
        }
        else
        {
            this->appendRow(row);
        }

        for (const auto& child : node.children)
        {
            addNode(visibleItem, child);
        }

        m_nodeMap[node.id] = visibleItem;
    }

    QMap<QString, QStandardItem*> m_nodeMap;
    std::function<void(const QString&, bool)> m_visibilityCallback;
    std::function<void(const QString&, const QString&)> m_renameCallback;

    friend class SceneTreePanel;
};

// ============================================================
// SceneTreePanel 实现
// ============================================================

SceneTreePanel::SceneTreePanel(QWidget* parent)
    : QWidget(parent), m_model(nullptr)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new QTreeView(this);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_view->setColumnWidth(0, 50);
    m_view->setColumnWidth(1, 70);
    m_view->setColumnWidth(2, 60);
    m_view->setAlternatingRowColors(true);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(1, Qt::AscendingOrder);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_view);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SceneTreePanel::onModelSelectionChanged);
    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid())
        {
            const QString id = index.sibling(index.row(), 1).data(kIdRole).toString();
            emit itemActivated(id);
        }
    });

    // 右键菜单
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTreeView::customContextMenuRequested, this, &SceneTreePanel::showContextMenu);
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
    connect(actSelectAll, &QAction::triggered, this, &SceneTreePanel::selectAllRows);
    connect(actClear, &QAction::triggered, this, [this]() {
        if (m_view && m_view->selectionModel())
        {
            m_syncing = true;
            m_view->selectionModel()->clearSelection();
            m_syncing = false;
        }
    });

    // Ctrl+A 快捷键
    auto* shortcut = new QShortcut(QKeySequence(tr("Ctrl+A")), this);
    shortcut->setContext(Qt::WidgetShortcut);
    connect(shortcut, &QShortcut::activated, this, &SceneTreePanel::selectAllRows);

    if (auto* lm = LanguageManager::instance())
    {
        connect(lm, &LanguageManager::languageChanged, this, &SceneTreePanel::retranslateMenu);
    }

    retranslateMenu();
}

SceneTreePanel::~SceneTreePanel()
{
    delete m_model;
}

void SceneTreePanel::setMode2D(const SceneTreeTopology2D& topology, MetaProvider2D metaProvider, ChildrenProvider2D childrenProvider)
{
    m_mode = Mode::Mode2D;
    m_metaProvider2D = std::move(metaProvider);
    m_childrenProvider2D = std::move(childrenProvider);

    // 先清除旧模型
    delete m_model;
    m_model = nullptr;

    auto* model = new SceneTreeTableModel2D(this);
    model->setTopology(topology, m_metaProvider2D, m_childrenProvider2D);
    m_model = model;
    m_view->setModel(m_model);

    // 使用回调而非信号
    model->setVisibilityCallback([this](qint64 id, bool visible) {
        emit visibilityToggled(QString::number(id), visible);
    });
    model->setRenameCallback([this](qint64 id, const QString& newName) {
        emit renameRequested(QString::number(id), newName);
    });
}

void SceneTreePanel::setMode3D(const SceneTreeModel3D& model)
{
    m_mode = Mode::Mode3D;

    // 先清除旧模型
    delete m_model;
    m_model = nullptr;

    auto* model3d = new SceneTreeTableModel3D();
    model3d->setData(model);
    m_model = model3d;
    m_view->setModel(m_model);

    // 使用回调而非信号
    model3d->setVisibilityCallback([this](const QString& id, bool visible) {
        emit visibilityToggled(id, visible);
    });
    model3d->setRenameCallback([this](const QString& id, const QString& newName) {
        emit renameRequested(id, newName);
    });

    m_view->expandAll();
}

void SceneTreePanel::setSelectedIds(const QSet<QString>& ids)
{
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    m_syncing = true;
    m_view->selectionModel()->clearSelection();

    if (m_mode == Mode::Mode2D)
    {
        auto* model2d = dynamic_cast<SceneTreeTableModel2D*>(m_model);
        if (model2d)
        {
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

            QModelIndex firstIndex;
            for (qint64 id : idSet)
            {
                const QModelIndex idx = model2d->indexForId(id);
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

            if (firstIndex.isValid())
            {
                QModelIndex parent = firstIndex.parent();
                while (parent.isValid())
                {
                    m_view->expand(parent);
                    parent = parent.parent();
                }
                m_view->scrollTo(firstIndex);
            }
        }
    }
    else  // Mode3D
    {
        auto* model3d = dynamic_cast<SceneTreeTableModel3D*>(m_model);
        if (model3d)
        {
            for (const QString& id : ids)
            {
                // 遍历查找匹配的项
                for (int row = 0; row < m_model->rowCount(); ++row)
                {
                    const QModelIndex idx = m_model->index(row, 1, QModelIndex());
                    if (idx.data(kIdRole).toString() == id)
                    {
                        m_view->selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                        break;
                    }
                }
            }
        }
    }

    m_syncing = false;
}

QStringList SceneTreePanel::selectedIds() const
{
    QStringList ids;
    if (!m_view || !m_model)
    {
        return ids;
    }

    const auto selectedIndexes = m_view->selectionModel()->selectedRows(1);
    for (const QModelIndex& index : selectedIndexes)
    {
        if (index.isValid())
        {
            QString id = index.data(kIdRole).toString();
            if (id.isEmpty() && m_mode == Mode::Mode2D)
            {
                // 2D mode: get ID from first column
                id = index.sibling(index.row(), 0).data(kIdRole).toString();
            }
            if (!id.isEmpty())
            {
                ids.append(id);
            }
        }
    }
    return ids;
}

bool SceneTreePanel::setCommandState(bool hasSelection, bool anyLocked)
{
    const bool lockChanged = (m_anyLocked != anyLocked);
    m_hasSelection = hasSelection;
    m_anyLocked = anyLocked;
    return lockChanged;
}

void SceneTreePanel::onModelSelectionChanged()
{
    if (m_syncing)
    {
        return;
    }
    emit selectionChanged(selectedIds());
}

void SceneTreePanel::retranslateMenu()
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

void SceneTreePanel::showContextMenu(const QPoint& pos)
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
        else if (name == QStringLiteral("ctxLock") || name == QStringLiteral("ctxUnlock"))
        {
            action->setEnabled(hasSelection);
        }
        else
        {
            action->setEnabled(hasSelection && !m_anyLocked);
        }
    }
    m_contextMenu->exec(m_view->viewport()->mapToGlobal(pos));
}

void SceneTreePanel::selectAllRows()
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
