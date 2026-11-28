#pragma once

#include <QObject>
#include <QPointer>

#include "UiShell/Titlebar/ITitleBarHost.h"

class QWidget;

namespace AA
{
    class IWindowFrame;
    class ITitleBarView;

    // 装配层对标题栏宿主 适配器
    class TitleBarHostAdapter final : public QObject, public ITitleBarHost
    {
    public:
        explicit TitleBarHostAdapter(QObject* parent = nullptr);

    public:
        // 绑定窗口、平台框架与标题栏视图
        void bind(QWidget* window, IWindowFrame* frame, ITitleBarView* view);

        // 拖动
        void requestDrag() override;
        // 最小化当前窗口
        void requestMinimize() override;
        // 在最大化/全屏与常规之间切换
        void requestToggleMaximize() override;
        // 关闭当前窗口
        void requestClose() override;
        // 当前窗口是否处于最大化（含全屏）
        bool isMaximized() const override;

    protected:
        // 监听窗口状态变化，及时刷新标题栏的最大化图标与提示文字
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        // 把最大化窗口还原到光标所在位置，供系统拖动接管
        void restoreForDrag();

    private:
        QPointer<QWidget> m_window;
        IWindowFrame* m_frame = nullptr;
        // 视图与它的 QWidget 同生共死；用 QPointer 防 rebuild() 后悬空
        QPointer<QWidget> m_titleBarWidget;
        ITitleBarView* m_titleBar = nullptr;
    };
}  // namespace AA
