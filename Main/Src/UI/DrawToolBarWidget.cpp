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

    m_activeToolId.clear();
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

    struct ToolInfo
    {
        QString id;
        QString displayName;
        QString tooltip;
        QString shortcut;
    };

    QList<ToolInfo> tools = {
        { QStringLiteral("2d.draw_line"),   tr("Line"),       tr("Draw Line"),    QStringLiteral("L") }, // 直线
        { QStringLiteral("2d.draw_polyline"), tr("Polyline"), tr("Draw Polyline"), QStringLiteral("PL") }, // 多段线
        { QStringLiteral("2d.draw_circle"), tr("Circle"),     tr("Draw Circle"),  QStringLiteral("C") }, // 圆
        { QStringLiteral("2d.draw_arc"),    tr("Arc"),        tr("Draw Arc"),     QStringLiteral("A") }, // 圆弧
        { QStringLiteral("2d.draw_polygon"), tr("Polygon"),   tr("Draw Polygon"), QStringLiteral("PG") }, // 多边形
        { QStringLiteral("2d.draw_bezier2"), tr("Bezier2"),   tr("Draw Bezier2"), QStringLiteral("B2") }, // 二阶贝塞尔
        { QStringLiteral("2d.draw_bezier"), tr("Bezier"),     tr("Draw Bezier"),  QStringLiteral("B") }, // 三阶贝塞尔
        { QStringLiteral("2d.draw_nurbs"),  tr("NURBS"),      tr("Draw NURBS"),   QStringLiteral("N") }, // NURBS曲线
        { QStringLiteral("2d.draw_smartline"), tr("SmartLine"), tr("Draw SmartLine"), QStringLiteral("SL") }, // 复合图元
    };

    for (const auto& tool : tools)
    {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(tool.displayName);
        button->setToolTip(tr("%1 (%2)").arg(tool.tooltip, tool.shortcut)); // %1=工具提示, %2=快捷键
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