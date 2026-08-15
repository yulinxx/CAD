#include "UiSceneTreePanel3D.h"

#include "SceneTreeModel3D.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace
{
    constexpr int kIdRole = Qt::UserRole;        // 节点 ID（引擎图元 ID 字符串）
    constexpr int kNameRole = Qt::UserRole + 1;  // 原始名称（用于重命名比对）
    constexpr int kVisRole = Qt::UserRole + 2;   // 原始可见性（用于复选框比对）
}  // namespace

SceneTreePanel3D::SceneTreePanel3D(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Name"), tr("Type"), tr("Info") });
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(SceneTreePanel3D::ColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(SceneTreePanel3D::ColType, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(SceneTreePanel3D::ColInfo, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemChanged, this, &SceneTreePanel3D::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SceneTreePanel3D::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemActivated, this, &SceneTreePanel3D::onItemActivated);
}

void SceneTreePanel3D::setModel(const SceneTreeModel3D& model)
{
    rebuildTree(model);
    if (m_tree)
    {
        m_tree->expandAll();
    }
}

void SceneTreePanel3D::setSelectedIds(const QSet<QString>& ids)
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

QStringList SceneTreePanel3D::selectedIds() const
{
    QStringList ids;
    collectSelectedIds(ids);
    return ids;
}

void SceneTreePanel3D::rebuildTree(const SceneTreeModel3D& model)
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

void SceneTreePanel3D::addNodeItem(QTreeWidgetItem* parentItem, const SceneTreeNode3D& node)
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
    item->setText(ColInfo, node.info);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(ColName, node.visible ? Qt::Checked : Qt::Unchecked);
    item->setSelected(node.selected);

    item->setData(ColName, kIdRole, node.id);
    item->setData(ColName, kNameRole, node.displayName);
    item->setData(ColName, kVisRole, node.visible);

    for (const auto& child : node.children)
    {
        addNodeItem(item, child);
    }
}

QTreeWidgetItem* SceneTreePanel3D::findItemById(const QString& id) const
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

void SceneTreePanel3D::collectSelectedIds(QStringList& ids) const
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
        if (!id.isEmpty())
        {
            ids << id;
        }
    }
}

void SceneTreePanel3D::onItemChanged(QTreeWidgetItem* item, int column)
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
}

void SceneTreePanel3D::onSelectionChanged()
{
    if (m_updating)
    {
        return;
    }
    QStringList ids;
    collectSelectedIds(ids);
    emit selectionChanged(ids);
}

void SceneTreePanel3D::onItemActivated(QTreeWidgetItem* item, int /*column*/)
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
