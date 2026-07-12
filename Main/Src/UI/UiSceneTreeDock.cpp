#include "UiSceneTreeDock.h"

#include "UiEntities.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

SceneTreeDockWidget::SceneTreeDockWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Name"), tr("Type") });
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item)
            return;
        const QString nodeId = item->data(0, Qt::UserRole).toString();
        selectPathParents(nodeId);
        highlightPathInTree(nodeId);
        if (m_selectionCallback)
            m_selectionCallback(nodeId);
        emit nodeActivated(nodeId);
        });
}

void SceneTreeDockWidget::setSceneDocument(SceneDocument3D* document)
{
    m_document = document;
    refresh();
}

void SceneTreeDockWidget::setSelectionCallback(std::function<void(const QString&)>&& callback)
{
    m_selectionCallback = std::move(callback);
}

void SceneTreeDockWidget::setPathCallback(std::function<void(const QStringList&)>&& callback)
{
    m_pathCallback = std::move(callback);
}

void SceneTreeDockWidget::refresh()
{
    rebuildTree();
    if (!m_tree)
        return;

    const auto currentId = currentNodeId();
    if (!currentId.isEmpty())
    {
        selectPathParents(currentId);
        highlightPathInTree(currentId);
    }
}

QString SceneTreeDockWidget::currentNodeId() const
{
    if (!m_tree || !m_tree->currentItem())
        return {};
    return m_tree->currentItem()->data(0, Qt::UserRole).toString();
}

void SceneTreeDockWidget::rebuildTree()
{
    if (!m_tree)
        return;

    m_tree->clear();
    if (!m_document)
        return;

    for (const auto& entity : m_document->entities())
    {
        auto node = std::dynamic_pointer_cast<SceneNode>(entity);
        if (!node)
            continue;
        auto* item = new QTreeWidgetItem(m_tree, { QString::fromStdString(node->name()), tr("Node") });
        item->setData(0, Qt::UserRole, QString::fromStdString(node->id()));
        for (const auto& child : node->children())
            addNodeItem(item, child);
    }

    m_tree->expandAll();
}

void SceneTreeDockWidget::addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node)
{
    if (!parent || !node)
        return;

    auto* item = new QTreeWidgetItem(parent, { QString::fromStdString(node->name()), tr("Node") });
    item->setData(0, Qt::UserRole, QString::fromStdString(node->id()));
    for (const auto& child : node->children())
        addNodeItem(item, child);
}

void SceneTreeDockWidget::highlightPathInTree(const QString& nodeId)
{
    if (!m_tree)
        return;

    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
    for (auto* item : items)
    {
        if (!item)
            continue;

        const bool matched = item->data(0, Qt::UserRole).toString() == nodeId;
        item->setSelected(matched);
        item->setBackground(0, matched ? QColor(80, 180, 255, 120) : QColor());
    }
}

void SceneTreeDockWidget::selectPathParents(const QString& nodeId)
{
    if (!m_tree || !m_document)
        return;

    if (auto* item = findItemByNodeId(nodeId))
    {
        item->setExpanded(true);
        auto* parent = item->parent();
        while (parent)
        {
            parent->setExpanded(true);
            parent = parent->parent();
        }
        m_tree->setCurrentItem(item);
    }
}

QTreeWidgetItem* SceneTreeDockWidget::findItemByNodeId(const QString& nodeId) const
{
    if (!m_tree)
        return nullptr;

    const auto items = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
    for (auto* item : items)
    {
        if (item && item->data(0, Qt::UserRole).toString() == nodeId)
            return item;
    }
    return nullptr;
}