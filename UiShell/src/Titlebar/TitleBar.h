#pragma once

#include <QPoint>
#include <QWidget>

#include "UiShell/Contract/TitleBarSpec.h"
#include "UiShell/Titlebar/ITitleBarHost.h"
#include "UiShell/Titlebar/ITitleBarMenuHost.h"
#include "UiShell/Titlebar/ITitleBarView.h"

class QLabel;
class QMouseEvent;
class QPaintEvent;
class QToolButton;

namespace AA
{
    // 标题栏视图：依赖 QtWidgets + TitleBarSpec + ITitleBarHost，
    // 只负责绘制与把意图交给宿主
    class TitleBar : public QWidget, public ITitleBarView, public ITitleBarMenuHost
    {
    public:
        // 构造标题栏：spec 高度/标题/按钮列表在此归一化兜底；host 可为 nullptr
        explicit TitleBar(const TitleBarSpec& spec, ITitleBarHost* host = nullptr, QWidget* parent = nullptr);

    public:
        QWidget* widget() override
        {
            return this;
        }

        // 返回归一化后的描述对象（高度已夹在合法区间内）。
        const TitleBarSpec& spec() const override
        {
            return m_spec;
        }

        // 设置宿主接口并立即按宿主状态刷新最大化按钮
        void setHost(ITitleBarHost* host) override;

        // 归一化后的标题文本与按钮顺序（缺省值在此兜底）。
        QString titleText() const override
        {
            return m_title;
        }

        QStringList buttonIds() const override
        {
            return m_buttonIds;
        }

        // 同步最大化状态：只更新按钮字形与提示，不触发任何窗口操作。
        void setMaximized(bool maximized) override;

        bool isMaximizedShown() const override
        {
            return m_maximized;
        }

        // 把菜单栏嵌入标题栏左侧，所有权转交给标题栏
        void setMenuWidget(QWidget* menu) override;

        void destroy() override
        {
            delete this;
        }

    protected:
        // 左键按下只记录候选拖动，窗口保持原状
        void mousePressEvent(QMouseEvent* event) override;

        // 位移超过拖动阈值后把意图交给宿主，由平台发起系统级移动
        void mouseMoveEvent(QMouseEvent* event) override;

        // 左键释放时结束拖动候选。
        void mouseReleaseEvent(QMouseEvent* event) override;

        // 左键双击且允许双击最大化时转发给宿主切换最大化。
        void mouseDoubleClickEvent(QMouseEvent* event) override;

        // 皮肤 QSS 存在时 autoFillBackground 不保证绘制；显式配置 backgroundColor 时
        // 在 paintEvent 里手动填充，保证颜色真的落在像素上
        void paintEvent(QPaintEvent* event) override;

    private:
        // 搭建图标/标题/按钮的布局与信号连接。
        void buildUi();

        // 应用背景色、文字色与按钮悬停样式。
        void applyAppearance();

        // 按当前最大化状态刷新最大化按钮的字形、提示与无障碍名称。
        void updateMaximizeButton();

        // 把 profile 的文字色同步到内嵌菜单，并标记交互属性。
        void applyMenuAppearance();

    private:
        TitleBarSpec m_spec;
        ITitleBarHost* m_host = nullptr;
        QString m_title;
        QStringList m_buttonIds;
        bool m_maximized = false;
        // 是否配置了合法的 backgroundColor：决定 paintEvent 是否显式填充。
        bool m_hasBackground = false;

        // 拖动候选：只记录按下位置，真正动作由宿主完成。
        bool m_dragPending = false;
        QPoint m_dragPressGlobal;

        QLabel* m_iconLabel = nullptr;
        QLabel* m_titleLabel = nullptr;
        QWidget* m_menuContainer = nullptr;
        QWidget* m_menuWidget = nullptr;
        QWidget* m_buttons = nullptr;
        QToolButton* m_minimizeButton = nullptr;
        QToolButton* m_maximizeButton = nullptr;
        QToolButton* m_closeButton = nullptr;
    };
}  // namespace AA
