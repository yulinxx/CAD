#pragma once

#include <QWidget>

class QHBoxLayout;

namespace AA
{
    class IShellHost;
}

namespace MS
{
    class RightToolBar;
    class RightToolWidget;

    // 右侧整体复合面板：
    //   RightToolBar（常驻窄竖条 40px） | RightToolWidget（按需展开的硬件/加工面板）
    //
    // 面板切换按钮是独立 Toggle（没有 autoExclusive），所以多个面板可以同时可见——
    // 点击几个按钮就累加显示几个子面板，与源项目 RightToolBar + RightToolWidget 行为一致。
    //
    //   - 整体 RightSidePanel 只有当至少一个子面板可见时才展开（变宽 → 挤中央）；
    //     全部子面板隐藏 → RightSidePanel 只有 40px → 中央占满。
    //   - 次级工具区（Group / Mirror / Alignment / ...）只发 commandId，
    //     经 IShellHost 转命令总线。
    class RightSidePanel : public QWidget
    {
        Q_OBJECT

    public:
        explicit RightSidePanel(AA::IShellHost* host = nullptr, QWidget* parent = nullptr);
        ~RightSidePanel() override = default;

        RightToolBar* toolBar() const
        {
            return m_toolBar;
        }
        RightToolWidget* toolWidget() const
        {
            return m_toolWidget;
        }

    private slots:
        // 顶部面板按钮 clicked：根据按钮当前 checked 状态决定开/关对应面板。
        void onPanelButtonClicked(const QString& panelId);

    private:
        // 整体 RightToolWidget 的显隐（只有当至少一个子面板可见时才展开）。
        void updatePanelVisibility();

    private:
        AA::IShellHost* m_host = nullptr;
        RightToolBar* m_toolBar = nullptr;
        RightToolWidget* m_toolWidget = nullptr;
        QHBoxLayout* m_layout = nullptr;
    };
}  // namespace MS
