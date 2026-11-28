#pragma once

#include <QWidget>

class QHBoxLayout;

namespace AA
{
    class IShellHost;
}

namespace MS
{
    class LeftToolBar;
    class LeftToolWidget;

    // 左侧整体复合面板：
    //   LeftToolBar（常驻窄竖条 40px，永不隐藏）
    //     + LeftToolWidget（默认 hidden；按钮点击 → set_content() 弹；再点 → remove_all_content() 收）
    //
    // 依赖 ShellView 中间布局：CenterView stretch=1，本 panel stretch=0。
    // LeftToolWidget show → LeftSidePanel 变宽 → 自动挤中央；
    // LeftToolWidget hide → LeftSidePanel 只有 40px → 中央占满。
    class LeftSidePanel : public QWidget
    {
        Q_OBJECT

    public:
        // host 只用于把工具按钮转成命令调用（非 UI 逻辑不下沉到工具栏控件）。
        explicit LeftSidePanel(AA::IShellHost* host = nullptr, QWidget* parent = nullptr);
        ~LeftSidePanel() override = default;

        LeftToolBar* toolBar() const
        {
            return m_toolBar;
        }
        LeftToolWidget* toolWidget() const
        {
            return m_toolWidget;
        }

    private slots:
        void onToolButtonClicked(const QString& toolId);

    private:
        QWidget* makePlaceholder(const QString& toolId);

    private:
        AA::IShellHost* m_host = nullptr;
        LeftToolBar* m_toolBar = nullptr;
        LeftToolWidget* m_toolWidget = nullptr;
        QHBoxLayout* m_layout = nullptr;
        QString m_curToolId;
    };
}  // namespace MS
