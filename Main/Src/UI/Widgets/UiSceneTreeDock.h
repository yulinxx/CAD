#pragma once

#include <QString>
#include <QWidget>

#include <memory>

class QTreeWidget;
class QTreeWidgetItem;
class SceneDocument3DAdapter;
class SceneNode;

class SceneTreeDockWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit SceneTreeDockWidget(QWidget* parent = nullptr);

public:
    void setSceneDocument(SceneDocument3DAdapter* document);
    void setSelectionCallback(std::function<void(const QString&)> callback);
    void setPathCallback(std::function<void(const QStringList&)> callback);
    void refresh();
    QString currentNodeId() const;

signals:
    void nodeActivated(const QString& nodeId);

private:
    void rebuildTree();
    void addNodeItem(QTreeWidgetItem* parent, const std::shared_ptr<SceneNode>& node);
    void highlightPathInTree(const QString& nodeId);
    void selectPathParents(const QString& nodeId);
    QTreeWidgetItem* findItemByNodeId(const QString& nodeId) const;

private:
    QTreeWidget* m_tree{ nullptr };
    SceneDocument3DAdapter* m_document{ nullptr };
    std::function<void(const QString&)> m_selectionCallback;
    std::function<void(const QStringList&)> m_pathCallback;
};
