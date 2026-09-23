/**
 * @file DrawToolBarWidget.cpp
 * @brief 绘图工具栏窗口实现
 *
 * 显示绘图工具按钮（选择、画线、圆等）。
 *
 * 高亮设计：使用两个独立的 QAction（SelectAction 和 PanAction）代表 Select 和 Pan 模式，
 * 通过切换按钮的 defaultAction 来切换。QActionGroup 自动管理互斥，确保永远只有一个工具高亮。
 */
#include "DrawToolBarWidget.h"

#include "Log/SyLogger.h"
#include <QAction>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSize>
#include <QTimer>

#include "UI2D/Operation/OperationId.h"
#include "UI/UiMetrics.h"
#include "UI/IconHelper.h"

namespace
{
    // Select / Pan 按钮图标资源路径（Pan 复用已有 View 面板的平移手势图标）
    const QString kSelectIconPath = QStringLiteral(":/ui/common/Icons/Tools/select.svg");
    const QString kPanIconPath = QStringLiteral(":/ui/common/Icons/View/pan.svg");
}  // namespace

DrawToolBarWidget::DrawToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DrawToolBarWidget"));
    setMinimumWidth(48);
    setMaximumWidth(56);
}

DrawToolBarWidget::~DrawToolBarWidget()
{
    // m_panAction 现在以 this 为父对象，由 Qt 自动管理生命周期
}

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
    // 工具切换时更新高亮状态
    updateHighlight();
}

void DrawToolBarWidget::setPanMode(bool enabled)
{
    m_isPanMode = enabled;
    // 不直接调用 updateHighlight，避免信号顺序问题
    // 让 setCurrentToolName 在工具切换完成后再更新 UI
}

void DrawToolBarWidget::updateSelectButtonAction()
{
    // 委托给 updateHighlight
    updateHighlight();
}

void DrawToolBarWidget::updateHighlight()
{
    // 安全检查：确保所有对象都在同一线程
    if (!m_selectButton || !m_selectAction || !m_panAction)
    {
        return;
    }

    // 检查线程亲和性：确保 action 和 button 在同一线程
    if (m_selectAction->thread() != m_selectButton->thread() ||
        m_panAction->thread() != m_selectButton->thread())
    {
        SY_WARNF("[DrawToolBarWidget] Action and button are in different threads, skipping updateHighlight");
        return;
    }

    // 使用回调获取当前的 Pan 状态
    const bool isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
    const bool isSelectTool = (m_currentToolName == QStringLiteral("SelectTool"));

    // 更新图标
    if (isPanMode)
    {
        m_selectButton->setDefaultAction(m_panAction);
        IconHelper::setThemedIcon(m_selectButton, kPanIconPath);
        m_selectButton->setToolTip(tr("Pan (Click to Select)"));
    }
    else
    {
        m_selectButton->setDefaultAction(m_selectAction);
        IconHelper::setThemedIcon(m_selectButton, kSelectIconPath);
        m_selectButton->setToolTip(tr("Select (Click to Pan)"));
    }

    // 高亮逻辑：
    // 1. SelectTool 且非 Pan 模式 -> 高亮
    // 2. Pan 模式 -> 高亮
    // 3. 其他工具 -> 不高亮
    const bool shouldHighlight = isSelectTool || isPanMode;
    m_selectButton->setChecked(shouldHighlight);
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

        // 特殊处理：Select 按钮在 Select 与 Pan 之间切换
        const QString toolName = action->property("toolName").toString();
        if (toolName == QStringLiteral("SelectTool") && m_panModeToggleCallback && m_isPanModeCallback)
        {
            // 保存 Select 按钮指针
            m_selectButton = button;
            // 保存 Select 模式的 QAction
            m_selectAction = action;

            // 创建 Pan 模式的虚拟 QAction（用于切换）
            // 使用 this 作为父对象，由 DrawToolBarWidget 统一管理生命周期
            m_panAction = new QAction(this);
            m_panAction->setCheckable(true);
            // Pan 图标和提示在 updateHighlight 中设置

            // 初始状态更新
            updateHighlight();

            // 点击时切换 Select/Pan
            connect(button, &QToolButton::clicked, this, [this]() {
                const bool isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
                const bool isSelectTool = (m_currentToolName == QStringLiteral("SelectTool"));

                if (isPanMode)
                {
                    // 当前是 Pan：切回 Select
                    if (m_selectAction)
                    {
                        m_selectAction->trigger();
                    }
                }
                else if (isSelectTool)
                {
                    // 当前是 Select：进入 Pan 模式
                    m_panModeToggleCallback();
                }
                else
                {
                    // 当前是其他工具：切回 Select（不进入 Pan）
                    if (m_selectAction)
                    {
                        m_selectAction->trigger();
                    }
                }
                // 延迟更新按钮状态（等待状态变化）
                QTimer::singleShot(0, this, &DrawToolBarWidget::updateHighlight);
            });
        }
        else
        {
            // 普通工具按钮正常处理
            // setDefaultAction 后按钮的图标/文案/提示/可勾选/勾选态/启用态全部跟随 QAction，
            // 点击即 trigger 该 QAction —— 与菜单、右键菜单共用同一条派发链。
            // 互斥由中枢的 QActionGroup 保证，无需 setAutoExclusive。
            // 安全检查：确保 action 和 button 在同一线程
            if (action && button && action->thread() == button->thread())
            {
                button->setDefaultAction(action);
            }
        }

        box->addWidget(button);
    }

    box->addStretch();
}
