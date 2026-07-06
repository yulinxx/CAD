#include "DrawToolBarWidget.h"

#include <QToolButton>
#include <QVBoxLayout>
#include <QIcon>

#include "UiCommandDispatcher.h"

DrawToolBarWidget::DrawToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DrawToolBarWidget"));
    setMinimumWidth(48);
    setMaximumWidth(56);

    createToolButtons();

    updateActiveTool(QStringLiteral("2d.select"));
}

void DrawToolBarWidget::setCommandDispatcher(UiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
    connectToolChanged();
}

void DrawToolBarWidget::connectToolChanged()
{
    if (!m_commandDispatcher)
        return;

    m_commandDispatcher->setToolChangedCallback([this](const QString& toolId) {
        updateActiveTool(toolId);
    });
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

    if (m_commandDispatcher)
        m_commandDispatcher->execute(toolId);
}

void DrawToolBarWidget::createToolButtons()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);

    struct ToolInfo {
        QString id;
        QString displayName;
        QString tooltip;
        QString shortcut;
    };

    QList<ToolInfo> tools = {
        { QStringLiteral("2d.select"),      QStringLiteral("选择"), QStringLiteral("选择工具"),    QStringLiteral("V") },
        { QStringLiteral("2d.draw_line"),   QStringLiteral("直线"), QStringLiteral("绘制直线"),    QStringLiteral("L") },
        { QStringLiteral("2d.draw_circle"), QStringLiteral("圆"),   QStringLiteral("绘制圆"),      QStringLiteral("C") },
        { QStringLiteral("2d.draw_polyline"), QStringLiteral("多段线"), QStringLiteral("绘制多段线"), QStringLiteral("PL") },
        { QStringLiteral("2d.move"),        QStringLiteral("移动"), QStringLiteral("移动对象"),    QStringLiteral("M") },
        { QStringLiteral("2d.copy"),        QStringLiteral("复制"), QStringLiteral("复制对象"),    QStringLiteral("CO") },
        { QStringLiteral("2d.rotate"),      QStringLiteral("旋转"), QStringLiteral("旋转对象"),    QStringLiteral("RO") },
    };

    for (const auto& tool : tools)
    {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(tool.displayName);
        button->setToolTip(QStringLiteral("%1 (%2)").arg(tool.tooltip, tool.shortcut));
        button->setProperty("toolId", tool.id);
        button->setProperty("shortcut", tool.shortcut);

        connect(button, &QToolButton::clicked, this, &DrawToolBarWidget::onToolButtonClicked);

        layout->addWidget(button);
        m_toolButtons[tool.id] = button;
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
