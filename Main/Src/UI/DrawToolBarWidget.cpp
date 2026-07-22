#include "DrawToolBarWidget.h"

#include <QToolButton>
#include <QVBoxLayout>
#include <QIcon>

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/CommandCatalog.h"

DrawToolBarWidget::DrawToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DrawToolBarWidget"));
    setMinimumWidth(48);
    setMaximumWidth(56);

    m_activeToolId.clear();
}

DrawToolBarWidget::~DrawToolBarWidget()
{
}

void DrawToolBarWidget::setToolDefinitions(const QVector<DrawToolEntry>& tools)
{
    m_toolDefinitions = tools;
    createToolButtons();
}

void DrawToolBarWidget::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void DrawToolBarWidget::updateActiveTool(const QString& toolId)
{
    m_activeToolId = toolId;
    setButtonChecked(toolId, true);
}

QString DrawToolBarWidget::currentActiveTool() const
{
    return m_activeToolId;
}

void DrawToolBarWidget::onToolButtonClicked()
{
    auto* button = qobject_cast<QToolButton*>(sender());
    if (!button)
        return;

    QString toolId = button->property("toolId").toString();
    if (toolId.isEmpty())
        return;

    if (toolId == m_activeToolId)
        return;

    m_activeToolId = toolId;
    setButtonChecked(toolId, true);

    if (m_operationBus)
        m_operationBus->run(CommandCatalog::operationForCommandId(toolId));
}

void DrawToolBarWidget::createToolButtons()
{
    // 清除旧按钮，重建布局
    if (layout())
    {
        QLayoutItem* item;
        while ((item = layout()->takeAt(0)) != nullptr)
        {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        delete layout();
    }
    m_toolButtons.clear();

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);

    // 使用外部注入的工具定义，保证与 CommandCatalog 命令 ID 一致
    for (const auto& tool : m_toolDefinitions)
    {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(tool.displayName);
        button->setToolTip(tr("%1 (%2)").arg(tool.tooltip, tool.shortcut)); // %1=工具提示, %2=快捷键
        button->setProperty("toolId", tool.commandId);
        button->setProperty("shortcut", tool.shortcut);

        connect(button, &QToolButton::clicked,
            this, &DrawToolBarWidget::onToolButtonClicked);

        layout->addWidget(button);
        m_toolButtons[tool.commandId] = button;
    }

    layout->addStretch();
}

void DrawToolBarWidget::setButtonChecked(const QString& toolId, bool checked)
{
    for (auto it = m_toolButtons.begin(); it != m_toolButtons.end(); ++it)
    {
        it.value()->setChecked(it.key() == toolId && checked);
    }
}