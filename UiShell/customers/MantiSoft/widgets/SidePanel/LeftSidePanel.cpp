#include "LeftSidePanel.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "UiShell/Contract/IShellHost.h"
#include "widgets/ToolBar/LeftToolBar.h"
#include "widgets/ToolWidget/LeftToolWidget.h"

namespace MS
{
    // —— LeftSidePanel 布局原则（对齐源项目 LeftToolBar + LeftToolWidget 流程）——
    //
    //   LeftSidePanel（作为一个整体 panel 挂在 ShellView 左侧，stretch=0）
    //     ├─ LeftToolBar（常驻窄竖条 40px，永不隐藏）
    //     └─ LeftToolWidget（默认 hidden；点击 ToolBar 按钮 → set_content() 弹内容；
    //                       再点同一按钮 → remove_all_content() 收起）
    //
    //   效果（依赖 ShellView 中间布局 CenterView stretch=1）：
    //     收起 → LeftSidePanel 只有 40px → CenterView 占满
    //     展开 → LeftSidePanel 变成 40+340=380px → 自动挤中央
    //
    //   工具与命令的分工：
    //     - Log / Layer 打开左侧辅助面板（当前为占位内容，后续由 Mode 注入真实 widget）；
    //     - 其余绘图/编辑工具统一映射到 Commands 已登记的命令 id，经 IShellHost 调用；
    //       工具栏本身不实现任何业务逻辑。

    LeftSidePanel::LeftSidePanel(RD::IShellHost* host, QWidget* parent)
        : QWidget(parent)
        , m_host(host)
    {
        setObjectName(QStringLiteral("mantisoft.leftSidePanel"));

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
        // 水平方向必须是 Preferred / Expanding，**不能是 Fixed**。
        // Fixed 会让 LeftSidePanel 宽度锁死在工具栏宽度，LeftToolWidget show 后也会被挤扁看不见。
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        m_toolBar = new MS::LeftToolBar(m_host, this);
        m_layout->addWidget(m_toolBar);

        // LeftToolWidget 构造时已 hide()
        m_toolWidget = new MS::LeftToolWidget(this);
        m_layout->addWidget(m_toolWidget);

        // 按钮点击 → 打开面板 / 调用命令
        connect(m_toolBar, &MS::LeftToolBar::sigToolButtonClicked,
            this, &LeftSidePanel::onToolButtonClicked);

        // LeftToolWidget 自己会在 remove_all_content() 时 hide + 发 sigContentChanged(false)
        connect(m_toolWidget, &MS::LeftToolWidget::sigContentChanged,
            this, [this](bool hasContent) {
                if (!hasContent)
                {
                    m_curToolId.clear();
                    m_toolBar->setPanelToolActive(QString());
                }
            });
    }

    void LeftSidePanel::onToolButtonClicked(const QString& toolId)
    {
        // —— Log / Layer：左侧辅助面板的开关 ——
        if (toolId == QLatin1String("log") || toolId == QLatin1String("layer"))
        {
            // Toggle：同一个 toolId 再点 → 收起
            if (!m_curToolId.isEmpty() && toolId == m_curToolId)
            {
                m_toolWidget->remove_all_content();   // 发 sigContentChanged(false) → 同步按钮状态
                return;
            }

            m_toolWidget->remove_all_content();

            auto* placeholder = makePlaceholder(toolId);
            m_toolWidget->set_content(placeholder, 340);

            m_curToolId = toolId;
            m_toolBar->setPanelToolActive(toolId);
            return;
        }

        // —— 其余工具：直通命令总线 ——
        const QString commandId = commandIdForToolId(toolId);
        if (m_host && !commandId.isEmpty())
            m_host->invoke(commandId);
    }

    QWidget* LeftSidePanel::makePlaceholder(const QString& toolId)
    {
        // 从 tooltip/button tooltip 里解析名字（简单做法：toolId 本身就是语义名）
        // 先做个简单标题 + 说明
        auto* root = new QWidget(m_toolWidget);
        auto* lay = new QVBoxLayout(root);
        lay->setContentsMargins(16, 16, 16, 16);
        lay->setSpacing(10);

        auto* title = new QLabel(toolId.toUpper().replace('.', ' '), root);
        title->setStyleSheet(QStringLiteral(
            "font-size: 14px; font-weight: bold; color: #2196F3; padding: 6px 0px;"));
        lay->addWidget(title);

        auto* hint = new QLabel(
            QCoreApplication::translate("MantiSoft",
                "Panel content for '%1' is not wired yet.\n"
                "A Mode will inject the real widget via set_content(w).")
            .arg(toolId), root);
        hint->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
        hint->setWordWrap(true);
        lay->addWidget(hint);

        lay->addStretch(1);
        return root;
    }
}  // namespace MS