#include "UiSceneTreeDock.h"

#include "UiEntities.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

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

        // UserRole = SceneNode id（即引擎图元 ID 字符串）
        // UserRole+1 = 引擎图元 ID（备用，与 UserRole 相同）
        const QString nodeId = item->data(0, Qt::UserRole).toString();

        selectPathParents(nodeId);
        highlightPathInTree(nodeId);

        // 向外界发送节点 ID，工作台通过 onSceneTreeSelection 同步引擎场景
        if (m_selectionCallback)
            m_selectionCallback(nodeId);
        emit nodeActivated(nodeId);
        });
}

void SceneTreeDockWidget::setSceneDocument(SceneDocument3DAdapter* document)
{
    m_document = document;
    refresh();
}

void SceneTreeDockWidget::setSelectionCallback(std::function<void(const QString&)> callback)
{
    m_selectionCallback = std::move(callback);
}

void SceneTreeDockWidget::setPathCallback(std::function<void(const QStringList&)> callback)
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

    const auto engineScene = m_document->engineScene();
    constexpr Eg::EntityId kNoEntity = static_cast<Eg::EntityId>(-1);

    for (const auto& entity : m_document->entities())
    {
        auto node = std::dynamic_pointer_cast<SceneNode>(entity);
        if (!node)
            continue;

        auto* item = new QTreeWidgetItem(m_tree, { QString::fromStdString(node->name()), tr("Node") });
        item->setData(0, Qt::UserRole, QString::fromStdString(node->id()));
        if (engineScene && node->engineEntityId() != kNoEntity)
        {
            item->setData(0, Qt::UserRole + 1, QString::number(node->engineEntityId()));
            if (auto* mesh = engineScene->findMeshById(node->engineEntityId()))
                item->setText(1, tr("Mesh #%1").arg(mesh->getId()));
        }
        for (const auto& child : node->children())
            addNodeItem(item, child);
    }

    m_tree->expandAll();
}

void SceneTreeDockWidget::addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node)
{
    if (!parent || !node)
        return;

    const auto engineScene = m_document ? m_document->engineScene() : nullptr;
    constexpr Eg::EntityId kNoEntity = static_cast<Eg::EntityId>(-1);

    auto* item = new QTreeWidgetItem(parent, { QString::fromStdString(node->name()), tr("Node") });
    item->setData(0, Qt::UserRole, QString::fromStdString(node->id()));
    if (engineScene && node->engineEntityId() != kNoEntity)
    {
        item->setData(0, Qt::UserRole + 1, QString::number(node->engineEntityId()));
        if (auto* mesh = engineScene->findMeshById(node->engineEntityId()))
            item->setText(1, tr("Mesh #%1").arg(mesh->getId()));
    }
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

        const auto storedId = item->data(0, Qt::UserRole).toString();
        const auto engineId = item->data(0, Qt::UserRole + 1).toString();
        const bool matched = (storedId == nodeId) || (!engineId.isEmpty() && engineId == nodeId);
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
        if (!item)
            continue;
        const auto storedId = item->data(0, Qt::UserRole).toString();
        const auto engineId = item->data(0, Qt::UserRole + 1).toString();
        if (storedId == nodeId || (!engineId.isEmpty() && engineId == nodeId))
            return item;
    }
    return nullptr;
}