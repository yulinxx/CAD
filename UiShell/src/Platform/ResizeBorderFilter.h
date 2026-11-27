#pragma once

#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QSize>
#include <QWidget>

#include <functional>

namespace RD
{
    // 把鼠标位置映射成边缘集合；band 为像素宽度的可缩放带。
    inline Qt::Edges resizeEdgesAt(const QPoint& pos, const QSize& size, int band)
    {
        Qt::Edges edges;
        if (pos.x() < band)
            edges |= Qt::LeftEdge;
        if (pos.x() >= size.width() - band)
            edges |= Qt::RightEdge;
        if (pos.y() < band)
            edges |= Qt::TopEdge;
        if (pos.y() >= size.height() - band)
            edges |= Qt::BottomEdge;
        return edges;
    }

    // 由边缘组合推导光标形状，四角优先于四边。
    inline Qt::CursorShape resizeCursorForEdges(Qt::Edges edges)
    {
        if ((edges & Qt::LeftEdge && edges & Qt::TopEdge) || (edges & Qt::RightEdge && edges & Qt::BottomEdge))
            return Qt::SizeFDiagCursor;
        if ((edges & Qt::RightEdge && edges & Qt::TopEdge) || (edges & Qt::LeftEdge && edges & Qt::BottomEdge))
            return Qt::SizeBDiagCursor;
        if (edges & (Qt::LeftEdge | Qt::RightEdge))
            return Qt::SizeHorCursor;
        if (edges & (Qt::TopEdge | Qt::BottomEdge))
            return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    // 最大化（含全屏）时不提供边缘缩放，与 Windows 实现保持一致的行为。
    inline bool isWindowMaximized(QWidget* window)
    {
        return window && (window->isMaximized() || window->isFullScreen());
    }

    // 事件过滤器代理：QtWindowFrame 不继承 QObject，
    // 用这个小型 QObject 子类承接 installEventFilter/removeEventFilter。
    class ResizeBorderFilter final : public QObject
    {
    public:
        using QObject::QObject;

        struct Handler
        {
            QWidget* resizeWidget = nullptr;
            QWidget* window = nullptr;
            // 边缘按下时由窗口框架注入；为空表示只改光标、不发起系统缩放。
            std::function<bool(Qt::Edges)> onStartResize;
        };

        Handler handler;

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (watched != handler.resizeWidget || !handler.window)
                return false;

            // 移动时只更新光标形状，事件继续向下传递，不影响控件自身逻辑。
            if (event->type() == QEvent::MouseMove)
            {
                auto* mouse = static_cast<QMouseEvent*>(event);
                handler.resizeWidget->setCursor(
                    resizeCursorForEdges(resizeEdgesAt(mouse->pos(), handler.resizeWidget->size(), 4)));
                return false;
            }

            // 左键落在 4px 边缘带内即发起系统缩放；最大化时不响应，避免误触。
            if (event->type() == QEvent::MouseButtonPress &&
                static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && !isWindowMaximized(handler.window))
            {
                auto* mouse = static_cast<QMouseEvent*>(event);
                const Qt::Edges edges = resizeEdgesAt(mouse->pos(), handler.resizeWidget->size(), 4);
                if (edges != Qt::Edges() && handler.onStartResize)
                    return handler.onStartResize(edges);
            }

            return false;
        }
    };
}  // namespace RD
