/**
 * @file DrawToolBarWidget.cpp
 * @brief 绘图工具栏窗口实现
 *
 * 显示绘图工具按钮（选择、画线、圆等）。
 *
 * Select/Pan 按钮高亮逻辑：
 * - 启动时默认 Select 高亮
 * - 点击切换为 Pan 时，Pan 高亮（按钮保持高亮，只是图标变化）
 * - 切换到其他绘图工具时，其他工具高亮，Select/Pan 按钮取消高亮
 * - 按 ESC 回到 Select/Pan，取消其他工具高亮，将 Select/Pan 高亮
 *
 * 总结：Select/Pan 作为一对互斥状态，和其他工具也是互斥的！
 */
#include "DrawToolBarWidget.h"

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
    , m_currentToolName(QStringLiteral("SelectTool"))  // 默认激活 SelectTool
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
    if (m_selectButton)
    {
        // 高亮状态：当前工具是 SelectTool 或处于 Pan 模式时高亮
        const bool isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
        const bool shouldHighlight = (toolName == QStringLiteral("SelectTool")) || isPanMode;
        m_selectButton->setChecked(shouldHighlight);
    }
}

void DrawToolBarWidget::updateSelectButtonHighlight()
{
    if (m_selectButton)
    {
        // 更新 Pan 模式状态
        m_isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
        // 高亮状态：当前工具是 SelectTool 或处于 Pan 模式时高亮
        const bool shouldHighlight = (m_currentToolName == QStringLiteral("SelectTool")) || m_isPanMode;
        m_selectButton->setChecked(shouldHighlight);
    }
}

void DrawToolBarWidget::updateSelectButtonIcon()
{
    if (m_selectButton)
    {
        // 更新 Pan 模式状态（用于解决信号顺序问题）
        m_isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
        // 走皮肤着色并记录当前图标路径，主题切换时可被 refreshAllThemedIcons 一并刷新
        IconHelper::setThemedIcon(m_selectButton, m_isPanMode ? kPanIconPath : kSelectIconPath);
        m_selectButton->setToolTip(m_isPanMode ? tr("Pan (Click to Select)") : tr("Select (Click to Pan)"));
        // 高亮状态由 setCurrentToolName 控制，此处不更新 checked 避免信号顺序问题
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

        // 特殊处理：Select 按钮在 Select 与 Pan 之间切换
        // 靠 toolName 属性识别：action->data() 存的是 menuId(int)，不能当作工具名使用
        const QString toolName = action->property("toolName").toString();
        if (toolName == QStringLiteral("SelectTool") && m_panModeToggleCallback && m_isPanModeCallback)
        {
            // 保存 Select 按钮指针
            m_selectButton = button;
            // 必须设置 checkable 才能支持 checked（高亮）状态
            button->setCheckable(true);

            // 初始图标/提示按当前 Pan 状态设置
            updateSelectButtonIcon();
            // 初始高亮状态（默认 Select 高亮）
            updateSelectButtonHighlight();

            // 覆盖 clicked 信号来处理 Select/Pan 切换
            connect(button, &QToolButton::clicked, this, [this, action, button]() {
                const bool isPanMode = m_isPanModeCallback ? m_isPanModeCallback() : false;
                if (isPanMode)
                {
                    // 当前是 Pan：切回 Select（setActiveTool 会顺带关闭 Pan）
                    action->trigger();
                    button->setChecked(true);  // 同步高亮状态
                }
                else if (action->isChecked())
                {
                    // 已是 Select：进入 Pan 模式
                    m_panModeToggleCallback();
                    button->setChecked(false);  // 同步高亮状态
                }
                else
                {
                    // 其他工具激活时：先切到 Select，不进入 Pan
                    action->trigger();
                    button->setChecked(true);  // 同步高亮状态
                }
                // 切换后更新图标
                QTimer::singleShot(0, this, &DrawToolBarWidget::updateSelectButtonIcon);
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