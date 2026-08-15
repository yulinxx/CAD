#include "UiSceneTreePanel2D.h"

#include "SceneTreeModel2D.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace
{
    constexpr int kIdRole = Qt::UserRole;        // 节点 ID（引擎图元/群组 ID 字符串）
    constexpr int kNameRole = Qt::UserRole + 1;  // 原始名称（用于重命名比对）
    constexpr int kVisRole = Qt::UserRole + 2;   // 原始可见性（用于复选框比对）
    constexpr int kGroupRole = Qt::UserRole + 3; // 是否为群组节点
}  // namespace

SceneTreePanel2D::SceneTreePanel2D(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Name"), tr("Type"), tr("Layer"), tr("Vis") });
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(SceneTreePanel2D::ColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(SceneTreePanel2D::ColType, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(SceneTreePanel2D::ColLayer, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(SceneTreePanel2D::ColVisible, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemChanged, this, &SceneTreePanel2D::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SceneTreePanel2D::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemActivated, this, &SceneTreePanel2D::onItemActivated);
}

void SceneTreePanel2D::setModel(const SceneTreeModel2D& model)
{
    rebuildTree(model);
    if (m_tree)
    {
        m_tree->expandAll();
    }
}

void SceneTreePanel2D::setSelectedIds(const QSet<QString>& ids)
{
    if (!m_tree)
    {
        return;
    }

    m_updating = true;

    QTreeWidgetItem* first = nullptr;
    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, ColName);
    for (auto* item : items)
    {
        if (!item)
        {
            continue;
        }
        const bool selected = ids.contains(item->data(ColName, kIdRole).toString());
        item->setSelected(selected);
        if (selected && !first)
        {
            first = item;
        }
    }

    // 展开选中节点的祖先，便于定位
    if (first)
    {
        auto* parent = first->parent();
        while (parent)
        {
            parent->setExpanded(true);
            parent = parent->parent();
        }
    }

    m_updating = false;
}

QStringList SceneTreePanel2D::selectedIds() const
{
    QStringList ids;
    collectSelectedIds(ids);
    return ids;
}

void SceneTreePanel2D::rebuildTree(const SceneTreeModel2D& model)
{
    if (!m_tree)
    {
        return;
    }

    m_updating = true;
    m_tree->clear();
    for (const auto& node : model.nodes)
    {
        addNodeItem(nullptr, node);
    }
    m_updating = false;
}

void SceneTreePanel2D::addNodeItem(QTreeWidgetItem* parentItem, const SceneTreeNode2D& node)
{
    QTreeWidgetItem* item = nullptr;
    if (parentItem)
    {
        item = new QTreeWidgetItem(parentItem);
    }
    else
    {
        item = new QTreeWidgetItem(m_tree);
    }

    item->setText(ColName, node.displayName);
    item->setText(ColType, node.typeName);
    item->setText(ColLayer, node.layerName);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(ColVisible, node.visible ? Qt::Checked : Qt::Unchecked);
    item->setSelected(node.selected);

    item->setData(ColName, kIdRole, node.id);
    item->setData(ColName, kNameRole, node.displayName);
    item->setData(ColName, kVisRole, node.visible);
    item->setData(ColName, kGroupRole, node.isGroup);

    for (const auto& child : node.children)
    {
        addNodeItem(item, child);
    }
}

QTreeWidgetItem* SceneTreePanel2D::findItemById(const QString& id) const
{
    if (!m_tree || id.isEmpty())
    {
        return nullptr;
    }
    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, ColName);
    for (auto* item : items)
    {
        if (item && item->data(ColName, kIdRole).toString() == id)
        {
            return item;
        }
    }
    return nullptr;
}

void SceneTreePanel2D::collectSelectedIds(QStringList& ids) const
{
    if (!m_tree)
    {
        return;
    }
    const auto items = m_tree->selectedItems();
    for (auto* item : items)
    {
        if (!item)
        {
            continue;
        }
        const QString id = item->data(ColName, kIdRole).toString();
        const bool isGroup = item->data(ColName, kGroupRole).toBool();
        // 群组节点在引擎中不是可单独选择的图元，仅收集实体节点
        if (!id.isEmpty() && !isGroup)
        {
            ids << id;
        }
    }
}

void SceneTreePanel2D::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (!item || m_updating)
    {
        return;
    }

    const QString id = item->data(ColName, kIdRole).toString();
    if (id.isEmpty())
    {
        return;
    }

    if (column == ColName)
    {
        const QString newName = item->text(ColName);
        const QString oldName = item->data(ColName, kNameRole).toString();
        if (!newName.isEmpty() && newName != oldName)
        {
            emit renameRequested(id, newName);
            // 引擎更新后由控制器刷新模型；此处立即回写占位名，避免歧义
            item->setData(ColName, kNameRole, newName);
        }
    }
    else if (column == ColVisible)
    {
        const bool newVisible = item->checkState(ColVisible) == Qt::Checked;
        const bool oldVisible = item->data(ColName, kVisRole).toBool();
        if (newVisible != oldVisible)
        {
            item->setData(ColName, kVisRole, newVisible);
            emit visibilityToggled(id, newVisible);
        }
    }
}

void SceneTreePanel2D::onSelectionChanged()
{
    if (m_updating)
    {
        return;
    }
    QStringList ids;
    collectSelectedIds(ids);
    emit selectionChanged(ids);
}

void SceneTreePanel2D::onItemActivated(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || m_updating)
    {
        return;
    }
    const QString id = item->data(ColName, kIdRole).toString();
    if (!id.isEmpty())
    {
        emit itemActivated(id);
    }
}
