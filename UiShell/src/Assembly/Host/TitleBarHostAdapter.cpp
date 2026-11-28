#include "Assembly/Host/TitleBarHostAdapter.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <algorithm>

#include "Platform/WindowFrame.h"
#include "UiShell/Titlebar/ITitleBarView.h"

namespace AA
{
    TitleBarHostAdapter::TitleBarHostAdapter(QObject* parent)
        : QObject(parent)
    {
    }

    void TitleBarHostAdapter::bind(QWidget* window, IWindowFrame* frame, ITitleBarView* view)
    {
        // 先解除旧视图的宿主引用
        if (m_titleBar && m_titleBarWidget)
            m_titleBar->setHost(nullptr);

        if (m_window)
            m_window->removeEventFilter(this);

        m_window = window;
        m_frame = frame;
        m_titleBar = view;
        m_titleBarWidget = view ? view->widget() : nullptr;

        if (!m_window)
            return;

        m_window->installEventFilter(this);
        if (m_titleBar)
        {
            m_titleBar->setHost(this);
            m_titleBar->setMaximized(isMaximized());
        }
    }

    void TitleBarHostAdapter::requestDrag()
    {
        if (!m_window)
            return;

        // 最大化时先还原，否则系统移动循环会把最大化窗口拖出屏幕。
        if (isMaximized())
            restoreForDrag();

        if (m_frame && m_frame->startSystemMove())
            return;

        // 没有平台框架时的兜底：交给 Qt 窗口系统，仍然保持系统级拖动语义。
        if (QWindow* handle = m_window->windowHandle())
            handle->startSystemMove();
    }

    void TitleBarHostAdapter::requestMinimize()
    {
        if (m_window)
            m_window->showMinimized();
    }

    void TitleBarHostAdapter::requestToggleMaximize()
    {
        if (!m_window)
            return;

        if (m_window->isMaximized() || m_window->isFullScreen())
            m_window->showNormal();
        else
            m_window->showMaximized();
    }

    void TitleBarHostAdapter::requestClose()
    {
        if (m_window)
            m_window->close();
    }

    bool TitleBarHostAdapter::isMaximized() const
    {
        return m_window && (m_window->isMaximized() || m_window->isFullScreen());
    }

    bool TitleBarHostAdapter::eventFilter(QObject* watched, QEvent* event)
    {
        // 窗口可能被系统快捷键等外部路径改变状态，这里作为唯一的同步点。
        if (watched == m_window && event->type() == QEvent::WindowStateChange && m_titleBar && m_titleBarWidget)
            m_titleBar->setMaximized(isMaximized());

        return QObject::eventFilter(watched, event);
    }

    void TitleBarHostAdapter::restoreForDrag()
    {
        QWidget* window = m_window;
        if (!window)
            return;

        const QPoint cursor = QCursor::pos();
        const QRect maximizedGeometry = window->frameGeometry();
        window->showNormal();
        if (window->isMaximized() || window->isFullScreen())
            return;

        QRect target;
        if (m_frame)
            target = m_frame->nativeFrameGeometry();
        if (target.isNull())
            target = window->frameGeometry();

        // 保持光标在还原后窗口中的横向比例，手感与系统标题栏拖动一致。
        if (maximizedGeometry.width() > 0)
        {
            const qreal ratio = std::min(std::max(
                qreal(cursor.x() - maximizedGeometry.left()) / maximizedGeometry.width(), 0.0), 1.0);
            target.moveLeft(cursor.x() - qRound(target.width() * ratio));
        }

        // 光标通常位于标题栏内：让还原后的标题栏仍停在指针下方。
        target.moveTop(cursor.y() - (std::min)(16, target.height() / 3));

        // 至少保证窗口主体落在光标所在屏幕内，避免还原到看不见的位置。
        QScreen* screen = QGuiApplication::screenAt(cursor);
        if (!screen)
            screen = window->screen();
        if (screen)
        {
            const QRect available = screen->availableGeometry();
            if (target.width() <= available.width())
                target.moveLeft(std::min(std::max(target.left(), available.left()), available.right() - target.width() + 1));
            if (target.height() <= available.height())
                target.moveTop(std::min(std::max(target.top(), available.top()), available.bottom() - target.height() + 1));
        }

        window->setGeometry(target);
    }
}  // namespace AA