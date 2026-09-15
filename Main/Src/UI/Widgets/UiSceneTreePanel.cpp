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
}  // namespace

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

    void setTopology(const SceneTreeTopology2D& topology,
        SceneTreePanel::MetaProvider2D metaProvider,
        SceneTreePanel::ChildrenProvider2D childrenProvider)
    {
        // 行集合完全没变时只更新元数据回调 + 发 dataChanged：
        // 可见性/锁定/改名这类"只有行内容变了"的刷新不必 reset 模型 —— reset 会重建
        // 顶层行索引（万级行是 O(N log N) 的 QMap 插入）并让视图丢掉展开状态与滚动位置。
        // 约束：只在没有已展开的群组时走这条快径（m_groupChildren/m_childParent 都是空的），
        // 否则群组成员可能已变、缓存会被落成陈旧行。
        if (m_groupChildren.isEmpty() && m_childParent.isEmpty() && rowsEqual(topology.topLevel))
        {
            m_metaProvider = std::move(metaProvider);
            m_childrenProvider = std::move(childrenProvider);
            if (!m_topLevel.isEmpty())
            {
                emit dataChanged(index(0, 0), index(m_topLevel.size() - 1, columnCount(QModelIndex()) - 1));
            }
            return;
        }

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
        // 首先检查 row 是否有效
        if (row < 0)
        {
            return {};
        }
        // 检查顶层索引时，row 不能超过 m_topLevel 的大小
        if (!parent.isValid())
        {
            if (row >= m_topLevel.size())
            {
                return {};
            }
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
        // 检查行索引有效性，防止越界访问
        if (index.row() < 0 || index.row() >= rowCount(index.parent()))
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
        // 复选框左对齐（文本对齐）
        if (role == Qt::TextAlignmentRole && index.column() == 0)
        {
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
        // 双击重命名时，编辑框以当前名称为初值（在原有名字基础上编辑）
        if (role == Qt::EditRole && index.column() == 1)
        {
            return meta.displayName;
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

        // 检查行列索引有效性，防止越界访问
        if (index.row() < 0 || index.row() >= rowCount(index.parent()) || index.column() < 0 ||
            index.column() >= columnCount(index.parent()))
        {
            return false;
        }

        // 检查 internalId 是否有效（防止无效指针访问）
        if (index.internalId() == 0)
        {
            return false;
        }

        const qint64 id = static_cast<qintptr>(index.internalId());

        if (index.column() == 0 && role == Qt::CheckStateRole)
        {
            // 先保存必要的值，因为回调可能导致模型被重置
            const QModelIndex idx = index;
            if (m_visibilityCallback)
            {
                m_visibilityCallback(id, value.toInt() == Qt::Checked);
            }
            // 检查索引是否仍然有效
            if (idx.isValid())
            {
                emit dataChanged(idx, idx, { Qt::CheckStateRole });
            }
            return true;
        }
        if (index.column() == 1 && role == Qt::EditRole)
        {
            // 先保存必要的值，因为回调可能导致模型被重置
            const QModelIndex idx = index;
            if (m_renameCallback)
            {
                m_renameCallback(id, value.toString());
            }
            // 检查索引是否仍然有效
            if (idx.isValid())
            {
                emit dataChanged(idx, idx, { Qt::DisplayRole, Qt::EditRole });
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
    /// 顶层行集合是否与当前一致（同 id、同分组标志、同顺序）
    bool rowsEqual(const QVector<SceneTreeRow2D>& rows) const
    {
        if (rows.size() != m_topLevel.size())
        {
            return false;
        }
        for (int i = 0; i < rows.size(); ++i)
        {
            if (rows[i].id != m_topLevel[i].id || rows[i].isGroup != m_topLevel[i].isGroup)
            {
                return false;
            }
        }
        return true;
    }

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
        resetColumns();
    }

    void setData(const SceneTreeModel3D& model)
    {
        // 优化：使用 clear() 一次性清空模型，避免循环 removeRow(0) 触发多次更新事件
        clear();
        m_nodeMap.clear();
        // clear() 把根节点换成了全新的 QStandardItem（columns==0），列结构与表头文字一并丢失：
        // 3D 场景为空时不会再有行来补回列数，模型零列 → QTreeView 表头零节 →
        // setMode3D 里的 setSectionResizeMode() 会命中 QHeaderView 的 Q_ASSERT(visual != -1)。
        resetColumns();

        for (const auto& node : model.nodes)
        {
            addNode(nullptr, node);
        }
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override
    {
        if (!index.isValid())
        {
            return false;
        }

        if (index.row() < 0 || index.row() >= rowCount(index.parent()) || index.column() < 0 ||
            index.column() >= columnCount(index.parent()))
        {
            return false;
        }

        if (index.column() == 0 && role == Qt::CheckStateRole)
        {
            const QString id = index.sibling(index.row(), 1).data(kIdRole).toString();
            // 先更新本地复选框状态，再异步通知回调，避免重建整棵树导致闪烁/重排
            bool ok = QStandardItemModel::setData(index, value, role);
            if (m_visibilityCallback)
            {
                m_visibilityCallback(id, value.toInt() == Qt::Checked);
            }
            return ok;
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
    /// 设定列结构（列数 + 表头文字）。clear() 后必须重做一次，否则模型会退化成零列
    void resetColumns()
    {
        setColumnCount(3);
        setHorizontalHeaderLabels({ QObject::tr("Vis"), QObject::tr("Name"), QObject::tr("Type") });
    }

    void addNode(QStandardItem* parent, const SceneTreeNode3D& node)
    {
        auto* visibleItem = new QStandardItem();
        visibleItem->setCheckable(true);
        visibleItem->setCheckState(node.visible ? Qt::Checked : Qt::Unchecked);
        visibleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        visibleItem->setData(node.id, kIdRole);
        visibleItem->setData(node.visible, kVisRole);

        auto* nameItem = new QStandardItem(node.displayName);
        nameItem->setData(node.id, kIdRole);
        nameItem->setData(node.displayName, kNameRole);
        // 双击重命名时编辑框以当前名称为初值
        nameItem->setData(node.displayName, Qt::EditRole);

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
    : QWidget(parent)
    , m_model(nullptr)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new QTreeView(this);
    m_view->header()->setStretchLastSection(false);
    m_view->setAlternatingRowColors(true);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(1, Qt::AscendingOrder);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_view);

    // 注意：QItemSelectionModel 的 connect 延迟到 setMode2D/setMode3D 中执行，
    // 因为构造时 QTreeView 尚未 setModel()，selectionModel() 返回 nullptr。
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

void SceneTreePanel::setMode2D(
    const SceneTreeTopology2D& topology, MetaProvider2D metaProvider, ChildrenProvider2D childrenProvider)
{
    m_mode = Mode::Mode2D;
    m_metaProvider2D = std::move(metaProvider);
    m_childrenProvider2D = std::move(childrenProvider);

    // 先清除旧模型（可能是 3D 模式下的 SceneTreeTableModel3D）。
    // 必须延迟销毁：本方法会在树行回调（勾选可见性 / 改名）中被同步调用，此时视图
    // 正在派发该模型的事件，立即 delete 会让回调返回后的 setData 与 Qt 委托代码访问
    // 已释放对象（勾选可见性即崩溃，崩在 emit dataChanged）。
    if (m_model)
    {
        m_model->deleteLater();
        m_model = nullptr;
    }

    auto* model = new SceneTreeTableModel2D(this);
    model->setTopology(topology, m_metaProvider2D, m_childrenProvider2D);
    m_model = model;
    m_view->setModel(m_model);
    connect(
        m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SceneTreePanel::onModelSelectionChanged);

    // 所有列都设置为 Interactive 模式，允许用户拖动调整列宽
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    // 设置合理的默认列宽，复选框列宽一些以便完整显示
    m_view->setColumnWidth(0, 70);
    m_view->setColumnWidth(1, 100);
    m_view->setColumnWidth(2, 80);

    // 使用回调而非信号
    model->setVisibilityCallback([this](qint64 id, bool visible) {
        emit visibilityToggled(QString::number(id), visible);
    });
    static_cast<SceneTreeTableModel2D*>(m_model)->setRenameCallback([this](qint64 id, const QString& newName) {
        emit renameRequested(QString::number(id), newName);
    });
}

void SceneTreePanel::setMode3D(const SceneTreeModel3D& model)
{
    m_mode = Mode::Mode3D;

    // 先清除旧模型（可能是 2D 模式下的 SceneTreeTableModel2D）。
    // 必须延迟销毁：勾选可见性 / 改名会经回调同步走到这里，此时视图正在派发该模型的事件，
    // 立即 delete 会让回调返回后的 setData（3D 侧还要再调一次 QStandardItemModel::setData）
    // 与 Qt 委托代码访问已释放对象。
    if (m_model)
    {
        m_model->deleteLater();
        m_model = nullptr;
    }

    auto* model3d = new SceneTreeTableModel3D();
    model3d->setData(model);
    m_model = model3d;
    m_view->setModel(m_model);

    // 检查 selectionModel 是否有效
    if (!m_view->selectionModel())
    {
        return;
    }

    connect(
        m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SceneTreePanel::onModelSelectionChanged);

    // 检查 header 是否有效
    QHeaderView* header = m_view->header();
    if (!header)
    {
        return;
    }

    // 所有列都设置为 Interactive 模式，允许用户拖动调整列宽
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    m_view->setColumnWidth(0, 90);
    m_view->setColumnWidth(1, 100);
    m_view->setColumnWidth(2, 80);

    // 首次加载时展开所有节点
    m_view->expandAll();

    // 使用回调而非信号
    model3d->setVisibilityCallback([this](const QString& id, bool visible) {
        emit visibilityToggled(id, visible);
    });
    model3d->setRenameCallback([this](const QString& id, const QString& newName) {
        emit renameRequested(id, newName);
    });
}

void SceneTreePanel::collectExpandedIds(const QModelIndex& index, QStringList& outIds) const
{
    if (!index.isValid() || !m_model)
    {
        return;
    }
    const QString id = index.data(kIdRole).toString();
    if (!id.isEmpty() && m_view->isExpanded(index))
    {
        outIds.append(id);
    }
    for (int r = 0; r < m_model->rowCount(index); ++r)
    {
        collectExpandedIds(m_model->index(r, 0, index), outIds);
    }
}

void SceneTreePanel::expandId(const QString& id)
{
    if (!m_model || !m_view)
    {
        return;
    }
    findAndExpandId(m_model->index(0, 0), id);
}

bool SceneTreePanel::findAndExpandId(const QModelIndex& index, const QString& id)
{
    if (!index.isValid())
    {
        return false;
    }
    if (index.data(kIdRole).toString() == id)
    {
        m_view->setExpanded(index, true);
        return true;
    }
    for (int r = 0; r < m_model->rowCount(index); ++r)
    {
        if (findAndExpandId(m_model->index(r, 0, index), id))
        {
            return true;
        }
    }
    return false;
}

void SceneTreePanel::setSelectedIds(const QSet<QString>& ids)
{
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    // 2D 模式的 id 只需转数值一次：既用于下面「选择集未变」的短路，也用于批量选择。
    QSet<qint64> idSet2D;
    if (m_mode == Mode::Mode2D)
    {
        idSet2D.reserve(ids.size());
        for (const QString& s : ids)
        {
            bool ok = false;
            const qint64 v = s.toLongLong(&ok);
            if (ok)
            {
                idSet2D.insert(v);
            }
        }

        // 要选的就是当前这一批时直接返回：拖动 / 重复通知会以同一份选择高频打到这里，
        // 而下面那次 clearSelection + select 走的是 QItemSelectionModel 的 range 插入合并，
        // 选中项多时是超线性的，重做一遍纯属浪费，展开与滚动同样不必重做。
        // 行的 internalId 就是图元 id，直接比对即可，避免走 data() 触发 metaProvider 的引擎查询。
        const auto selectedRows = m_view->selectionModel()->selectedRows(1);
        if (selectedRows.size() == idSet2D.size())
        {
            bool sameSelection = true;
            for (const QModelIndex& idx : selectedRows)
            {
                if (!idSet2D.contains(static_cast<qint64>(idx.internalId())))
                {
                    sameSelection = false;
                    break;
                }
            }
            if (sameSelection)
            {
                return;
            }
        }
    }

    m_syncing = true;

    // 优化1: 阻止信号以减少触发次数，批量更新选择状态
    m_view->blockSignals(true);
    m_view->selectionModel()->blockSignals(true);
    m_view->selectionModel()->clearSelection();

    if (m_mode == Mode::Mode2D)
    {
        auto* model2d = dynamic_cast<SceneTreeTableModel2D*>(m_model);
        if (model2d)
        {
            // 优化2: 批量选择 - 使用 QItemSelection 一次性选择多个项目
            QItemSelection selection;
            QModelIndex firstIndex;
            for (qint64 id : idSet2D)
            {
                const QModelIndex idx = model2d->indexForId(id);
                if (!idx.isValid())
                {
                    continue;
                }
                selection.select(idx, idx);
                if (!firstIndex.isValid())
                {
                    firstIndex = idx;
                }
            }

            // 一次性应用选择
            m_view->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);

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
            // 3D模式也使用批量选择
            QItemSelection selection;
            for (const QString& id : ids)
            {
                // 遍历查找匹配的项
                for (int row = 0; row < m_model->rowCount(); ++row)
                {
                    const QModelIndex idx = m_model->index(row, 1, QModelIndex());
                    if (idx.data(kIdRole).toString() == id)
                    {
                        selection.select(idx, idx);
                        break;
                    }
                }
            }
            m_view->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    // 上面屏蔽了 selectionModel 的信号，QTreeView 收不到选择变化、也就不会安排重绘；
    // 这里显式刷一次视口，让高亮立刻跟手，而不是等下一次无关重绘才出现。
    m_view->viewport()->update();

    // 恢复信号
    m_view->selectionModel()->blockSignals(false);
    m_view->blockSignals(false);
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
