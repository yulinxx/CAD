#pragma once

#include <QWidget>
#include <QMap>
#include <QString>

class QToolButton;
class UiCommandDispatcher;

class DrawToolBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DrawToolBarWidget(QWidget* parent = nullptr);

    void setCommandDispatcher(UiCommandDispatcher* dispatcher);

    void connectToolChanged();

    void updateActiveTool(const QString& toolId);

    QString currentActiveTool() const;

private slots:
    void onToolButtonClicked();

private:
    void createToolButtons();
    void setButtonChecked(const QString& toolId, bool checked);

    QMap<QString, QToolButton*> m_toolButtons;
    QString m_activeToolId;
    UiCommandDispatcher* m_commandDispatcher{ nullptr };
};
