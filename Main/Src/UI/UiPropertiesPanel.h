#pragma once

#include <QStringList>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class PropertiesPanelWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanelWidget(QWidget* parent = nullptr);

public:
    enum class WorkbenchMode
    {
        Unknown,
        TwoD,
        ThreeD
    };

    struct PropertiesData
    {
        QString stateText;
        QString selectionText;
        QString objectTitle;
        QStringList objectLines;
        WorkbenchMode mode{ WorkbenchMode::Unknown };
        QString documentType;
        QString documentStatus;
        QStringList modeSpecificFields;
    };

public:
    void setPropertiesData(const PropertiesData& data);
    void setWorkbenchMode(WorkbenchMode mode);
    void setStateText(const QString& text);
    void setSelectionText(const QString& text);
    void setObjectDetails(const QString& title, const QStringList& lines);
    void refresh();

private:
    void syncText();

private:
    QTreeWidget* m_tree{ nullptr };
    PropertiesData m_data;
};
