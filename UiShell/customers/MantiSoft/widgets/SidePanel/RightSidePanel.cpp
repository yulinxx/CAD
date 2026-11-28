#include "RightSidePanel.h"

#include <QHBoxLayout>
#include <QToolButton>

#include "UiShell/Contract/IShellHost.h"
#include "widgets/ToolBar/RightToolBar.h"
#include "widgets/ToolWidget/RightToolWidget.h"

namespace MS
{
    // —— RightSidePanel 布局（和 LeftSidePanel 对齐）——
    //
    //   RightSidePanel（挂在 ShellView 右侧，stretch=0）
    //     ├─ RightToolBar（常驻窄竖条 40px，永不隐藏；四个面板按钮独立 Toggle）
    //     └─ RightToolWidget（按需展开的硬件面板区；内部 property/coordinate/manual/process
    //                         四个子面板构造期全部建好，各自独立 show/hide，可同时可见）
    //
    // 整体显隐原则（与源项目一致）：
    //   - 至少一个子面板 visible → RightToolWidget show → 整体变宽 → 挤中央；
    //   - 全部子面板隐藏       → RightToolWidget hide → 只有 40px 工具栏 → 中央占满。

    RightSidePanel::RightSidePanel(AA::IShellHost* host, QWidget* parent)
        : QWidget(parent)
        , m_host(host)
    {
        setObjectName(QStringLiteral("RightSidePanel"));

        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);
        setContentsMargins(0, 0, 0, 0);
        // 水平方向必须是 Preferred / Expanding，**不能是 Fixed**。
        // Fixed 会告诉布局"我的宽度锁死"，RightToolWidget show 后 RightSidePanel 不会跟着变宽，
        // 子面板就被挤在 40px 工具栏宽度里完全看不见 —— 这就是点了按钮没出面板的根因。
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        m_toolBar = new MS::RightToolBar(this);
        m_layout->addWidget(m_toolBar);

        // 初始 hidden；RightToolBar 面板按钮点击后按需展开。
        // 构造期 RightToolWidget 内部四个子面板默认也全 hide，
        // 所以初始状态：按钮 unchecked + 面板全隐藏 + RightToolWidget 整体 hidden。
        m_toolWidget = new MS::RightToolWidget(this);
        m_toolWidget->hide();
        m_layout->addWidget(m_toolWidget);

        // —— 顶部面板按钮 → 子面板 show/hide ——
        // 注意：Qt checkable 按钮发出 clicked 信号时，checked 状态已经自动翻转完毕，
        // 所以在 slot 里直接读 btn->isChecked() 就是目标状态。
        connect(m_toolBar, &MS::RightToolBar::sigPanelButtonClicked,
            this, &RightSidePanel::onPanelButtonClicked);

        // —— 从面板外部（如 RightToolWidget 自己 clearAllPanels）显隐变化时，
        //     反向同步工具栏按钮的 checked 状态，保证 UI 一致性 ——
        connect(m_toolWidget, &MS::RightToolWidget::sigPanelShown,
            this, [this](const QString& panelId) {
                m_toolBar->setPanelActive(panelId, true);
                updatePanelVisibility();
            });

        connect(m_toolWidget, &MS::RightToolWidget::sigPanelHidden,
            this, [this](const QString& panelId) {
                m_toolBar->setPanelActive(panelId, false);
                updatePanelVisibility();
            });

        // —— 次级工具区：菜单项 / Measurement → 命令总线 ——
        connect(m_toolBar, &MS::RightToolBar::sigCommandTriggered,
            this, [this](const QString& commandId) {
                if (m_host) m_host->invoke(commandId);
            });
    }

    void RightSidePanel::onPanelButtonClicked(const QString& panelId)
    {
        // 直接 toggle 目标子面板的显隐。
        // 按钮 clicked 信号本身已经由 Qt checkable 机制翻转了按钮的 checked 状态，
        // togglePanel 也会把面板的显隐翻一遍 —— 两边目标状态天然一致。
        m_toolWidget->togglePanel(panelId);

        // 整体 RightToolWidget 显隐：至少一个子面板开 → show；全部关 → hide。
        updatePanelVisibility();
    }

    void RightSidePanel::updatePanelVisibility()
    {
        if (m_toolWidget->hasAnyVisible())
            m_toolWidget->show();
        else
            m_toolWidget->hide();
    }
}  // namespace MS