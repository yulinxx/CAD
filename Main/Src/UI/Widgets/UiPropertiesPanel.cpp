#include "UiPropertiesPanel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

PropertiesPanelWidget::PropertiesPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Field"), tr("Value") });
    layout->addWidget(m_tree);
}

void PropertiesPanelWidget::setPropertiesData(const PropertiesData& data)
{
    m_data = data;
    refresh();
}

void PropertiesPanelWidget::setWorkbenchMode(WorkbenchMode mode)
{
    m_data.mode = mode;
    refresh();
}

void PropertiesPanelWidget::setStateText(const QString& text)
{
    m_data.stateText = text;
    refresh();
}

void PropertiesPanelWidget::setSelectionText(const QString& text)
{
    m_data.selectionText = text;
    refresh();
}

void PropertiesPanelWidget::setObjectDetails(const QString& title, const QStringList& lines)
{
    m_data.objectTitle = title;
    m_data.objectLines = lines;
    refresh();
}

void PropertiesPanelWidget::refresh()
{
    if (m_tree)
    {
        m_tree->clear();
    }
    syncText();
}

void PropertiesPanelWidget::syncText()
{
    if (!m_tree)
    {
        return;
    }

    new QTreeWidgetItem(m_tree, { tr("State"), m_data.stateText });
    new QTreeWidgetItem(m_tree, { tr("Selection"), m_data.selectionText });
    new QTreeWidgetItem(m_tree, { tr("Object"), m_data.objectTitle });

    if (!m_data.documentType.isEmpty())
    {
        new QTreeWidgetItem(m_tree, { tr("Document"), m_data.documentType });
    }
    if (!m_data.documentStatus.isEmpty())
    {
        new QTreeWidgetItem(m_tree, { tr("Status"), m_data.documentStatus });
    }

    for (const QString& field : m_data.modeSpecificFields)
    {
        const int colonIndex = field.indexOf(QStringLiteral(":"));
        if (colonIndex > 0)
        {
            new QTreeWidgetItem(m_tree, { field.left(colonIndex).trimmed(), field.mid(colonIndex + 1).trimmed() });
        }
        else
        {
            new QTreeWidgetItem(m_tree, { tr("Detail"), field });
        }
    }

    for (const QString& line : m_data.objectLines)
    {
        new QTreeWidgetItem(m_tree, { tr("Detail"), line });
    }
}