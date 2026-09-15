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

void DrawToolBarWidget::setPanModeToggleCallback(PanModeCallback callback)
{
    m_panModeToggleCallback = std::move(callback);
}

void DrawToolBarWidget::setIsPanModeCallback(IsPanModeCallback callback)
{
    m_isPanModeCallback = std::move(callback);
}

void DrawToolBarWidget::setCurrentToolName(const QString& toolName)
{
    m_currentToolName = toolName;
}

QIcon DrawToolBarWidget::selectIcon()
{
    return QIcon(":/ui/common/Icons/Tools/select.svg");
}

QIcon DrawToolBarWidget::panIcon()
{
    return QIcon(":/ui/common/Icons/Tools/pan.svg");
}

void DrawToolBarWidget::updateSelectButtonIcon()
{
    if (m_selectButton)
    {
        bool isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
        m_selectButton->setIcon(isPanMode ? panIcon() : selectIcon());
        m_selectButton->setToolTip(isPanMode ? tr("Pan (Click to Select)") : tr("Select (Click to Pan)"));
    }
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

        // 特殊处理：点击 Select 工具按钮时，在 Select 和 Pan 之间切换
        QString toolName = action->data().toString();
        if (toolName == QStringLiteral("SelectTool") && m_panModeToggleCallback && m_isPanModeCallback)
        {
            // 保存 Select 按钮指针
            m_selectButton = button;

            // 设置正确的图标
            bool isPanMode = m_isPanModeCallback();
            button->setIcon(isPanMode ? panIcon() : selectIcon());
            button->setToolTip(isPanMode ? tr("Pan (Click to Select)") : tr("Select (Click to Pan)"));

            // 覆盖 clicked 信号来处理 Select/Pan 切换
            connect(button, &QToolButton::clicked, this, [this, action]() {
                bool isPanMode = m_isPanModeCallback();
                if (isPanMode)
                {
                    // 当前是 Pan 模式，点击 Select 按钮时切换到 Select 工具
                    action->trigger();
                    // 切换后更新图标
                    QTimer::singleShot(0, this, &DrawToolBarWidget::updateSelectButtonIcon);
                }
                else
                {
                    // 当前是 Select 工具或普通模式，点击 Select 按钮时切换到 Pan 模式
                    m_panModeToggleCallback();
                    // 切换后更新图标
                    QTimer::singleShot(0, this, &DrawToolBarWidget::updateSelectButtonIcon);
                }
            });
        }
        else
        {
            // 普通工具按钮正常处理
            // setDefaultAction 后按钮的图标/文案/提示/可勾选/勾选态/启用态全部跟随 QAction，
            // 点击即 trigger 该 QAction —— 与菜单、右键菜单共用同一条派发链。
            // 互斥由中枢的 QActionGroup 保证，无需 setAutoExclusive。
            button->setDefaultAction(action);
        }

        box->addWidget(button);
    }

    box->addStretch();
}
