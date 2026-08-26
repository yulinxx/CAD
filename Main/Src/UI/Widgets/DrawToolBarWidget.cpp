#include "DrawToolBarWidget.h"

#include <QAction>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSize>

#include "UI2D/Operation/OperationId.h"
#include "UI/UiMetrics.h"

DrawToolBarWidget::DrawToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DrawToolBarWidget"));
    setMinimumWidth(48);
    setMaximumWidth(56);
}

DrawToolBarWidget::~DrawToolBarWidget() {}

void DrawToolBarWidget::setToolActions(const QVector<QAction*>& actions)
{
    m_toolActions = actions;
    rebuildButtons();
}

void DrawToolBarWidget::rebuildButtons()
{
    // 清除旧按钮，重建布局。按钮只是 QAction 的展示壳，QAction 归中枢所有，此处不销毁它们。
    if (QLayout* old = layout())
    {
        QLayoutItem* item = nullptr;
        while ((item = old->takeAt(0)) != nullptr)
        {
            if (item->widget())
            {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete old;
    }

    const int iconSize = UiMetrics::toolbarIconSizeLarge();

    auto* box = new QVBoxLayout(this);
    box->setSpacing(4);
    box->setContentsMargins(4, 4, 4, 4);

    for (QAction* action : m_toolActions)
    {
        if (!action)
        {
            continue;
        }

        auto* button = new QToolButton(this);
        // 关闭 autoRaise：让 QSS :hover / :pressed 样式正常渲染（autoRaise=true 时 Qt 原生绘制会覆盖样式表）
        button->setAutoRaise(false);
        // 不抢占焦点：让 Esc 等快捷键始终由视口（ViewportInputRouter）处理
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(iconSize, iconSize));
        // 无图标时回退为文字按钮，保证功能可见
        button->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        // 派发来源标记：中枢的 detectOperationSource 读取本属性判定 LeftToolbar，
        // 不依赖宿主（QToolBar / QDockWidget）的 objectName，换承载方式也不会误判。
        button->setProperty("operationSource", static_cast<int>(OperationSource::LeftToolbar));
        // setDefaultAction 后按钮的图标/文案/提示/可勾选/勾选态/启用态全部跟随 QAction，
        // 点击即 trigger 该 QAction —— 与菜单、右键菜单共用同一条派发链。
        // 互斥由中枢的 QActionGroup 保证，无需 setAutoExclusive。
        button->setDefaultAction(action);

        box->addWidget(button);
    }

    box->addStretch();
}
