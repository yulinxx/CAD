#include "DrawToolBarWidget.h"

#include <QToolButton>
#include <QVBoxLayout>
#include <QIcon>
#include <QSize>

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI/IconHelper.h"
#include "UI/UiMetrics.h"
#include "Log/SyLogger.h"

DrawToolBarWidget::DrawToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DrawToolBarWidget"));
    setMinimumWidth(48);
    setMaximumWidth(56);

    m_activeToolId.clear();
}

DrawToolBarWidget::~DrawToolBarWidget() {}

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
    {
        return;
    }

    QString toolId = button->property("toolId").toString();
    if (toolId.isEmpty())
    {
        return;
    }

    // 按钮存的是 CommandCatalog 的 toolName（如 "LineTool"），
    // 因此必须用 operationForToolName 解析，而不是 operationForCommandId（后者只认识 "2d.draw_line" 这类命令键）。
    const OperationId opId = CommandCatalog::operationForToolName(toolId);
    if (opId == OperationId::None)
    {
        SY_WARNF("[DrawToolBarWidget] no operation for tool: %s", qPrintable(toolId));
        return;
    }

    // 始终保持点击的工具为选中态（配合 setAutoExclusive，重复点击也不会取消高亮）
    const bool alreadyActive = (toolId == m_activeToolId);
    m_activeToolId = toolId;
    setButtonChecked(toolId, true);
    if (alreadyActive)
    {
        return;
    }

    // 统一走 OperationBus：UI 入口 → 操作总线 → 已注册的 Tool_* 操作 → 视口激活对应工具
    SY_INFOF("[DrawToolBarWidget] activate tool=%s op=%s", qPrintable(toolId), Cmd::operationIdToString(opId));
    if (m_operationBus)
    {
        m_operationBus->run(opId, {}, OperationSource::DrawTool);
    }
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
            {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete layout();
    }
    m_toolButtons.clear();

    const int iconSize = UiMetrics::toolbarIconSizeLarge();

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);

    // 使用外部注入的工具定义，保证与 CommandCatalog 命令 ID 一致
    for (const auto& tool : m_toolDefinitions)
    {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        // 互斥选中（单选组语义）：点击已选中的按钮不会取消选中，保证始终有一个工具处于高亮
        button->setAutoExclusive(true);
        // 不抢占焦点：让 Esc 等快捷键始终由视口（ViewportInputRouter）处理
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(iconSize, iconSize));

        QString tooltip = tool.tooltip;
        if (!tool.shortcut.isEmpty())
        {
            tooltip = QStringLiteral("%1 (%2)").arg(tool.tooltip, tool.shortcut);
        }

        if (tool.iconResource.isEmpty())
        {
            // 无图标时回退为文字按钮，保证功能可见
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setText(tool.displayName);
        }
        else
        {
            // 图标按钮，悬停时以文字提示（tooltip）说明用途
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            IconHelper::setThemedIcon(button, tool.iconResource);
            tooltip = tool.displayName + (tooltip.isEmpty() ? QString() : QStringLiteral("\n") + tooltip);
        }
        button->setToolTip(tooltip);
        button->setProperty("toolId", tool.commandId);
        button->setProperty("shortcut", tool.shortcut);

        connect(button, &QToolButton::clicked, this, &DrawToolBarWidget::onToolButtonClicked);

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