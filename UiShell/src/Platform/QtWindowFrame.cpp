#include "Platform/WindowFrame.h"

#include <QWidget>
#include <QWindow>

#include "Platform/ResizeBorderFilter.h"

namespace AA
{
    namespace
    {
        ///////////////////////////////////////////////////////////////////////////

        // Linux/macOS 退化实现：仍使用 Qt 原生无边框 + QWindow 的系统移动/缩放。
        // 只负责无边框标志与边缘命中，不负责标题栏绘制，也不接触任何 Win32 细节。
        class QtWindowFrame final : public IWindowFrame
        {
        public:
            void attach(QWidget* window) override
            {
                m_window = window;
                if (!m_window)
                    return;
                // 只加 FramelessWindowHint，窗口仍是普通 Qt 窗口，系统缩放交给 QWindow 接口。
                m_window->setWindowFlags(m_window->windowFlags() | Qt::FramelessWindowHint);
                m_filter.handler.window = m_window;
                // 边缘按下必须把意图转成系统级缩放：onStartResize 不赋值时
                // ResizeBorderFilter 只会改光标，鼠标拖拽完全无效。
                m_filter.handler.onStartResize = [this](Qt::Edges edges) {
                    return startSystemResize(edges);
                };
            }

            void setResizeBorderWidget(QWidget* widget) override
            {
                // 换绑时先解绑旧控件，避免其继续收到事件过滤器回调。
                if (m_resizeWidget == widget)
                    return;
                if (m_resizeWidget)
                    m_resizeWidget->removeEventFilter(&m_filter);
                m_resizeWidget = widget;
                if (m_resizeWidget)
                {
                    // 必须开启鼠标跟踪，否则未按键移动时收不到 MouseMove，光标无法变化。
                    m_resizeWidget->setMouseTracking(true);
                    m_resizeWidget->installEventFilter(&m_filter);
                    m_filter.handler.resizeWidget = widget;
                }
                else
                {
                    m_filter.handler.resizeWidget = nullptr;
                }
            }

            void setMinimumSize(const QSize& size) override
            {
                m_minimumSize = size;
                if (m_window)
                    m_window->setMinimumSize(size);
            }

            int resizeBorderWidth() const override
            {
                return 4;
            }

            bool startSystemMove() override
            {
                // 非 Windows 上退出自绘标题栏时，拖动同样交给窗口系统接管，
                // 避免自绘坐标换算在多屏/缩放下出现偏差。
                if (!m_window || !m_window->windowHandle())
                    return false;
                return m_window->windowHandle()->startSystemMove();
            }

            bool startSystemResize(Qt::Edges edges) override
            {
                // 与 startSystemMove 同理：交给窗口系统做缩放，保证贴靠/多屏行为原生一致。
                if (!m_window || !m_window->windowHandle())
                    return false;
                return m_window->windowHandle()->startSystemResize(edges);
            }

            QRect nativeFrameGeometry() const override
            {
                // 非 Windows 平台没有原生消息拦截，Qt 缓存即为准。
                return m_window ? m_window->frameGeometry() : QRect();
            }

            void refreshTargetHwnds() override
            {
                // Windows 专用：枚举顶层 HWND 补 WS_CAPTION + WS_THICKFRAME。
                // 非 Windows 平台无需处理，保持空实现。
            }

        private:
            QWidget* m_window = nullptr;
            QWidget* m_resizeWidget = nullptr;
            QSize m_minimumSize;
            ResizeBorderFilter m_filter;
        };
    }  // namespace

    ///////////////////////////////////////////////////////////////////////////

    std::unique_ptr<IWindowFrame> createQtWindowFrame()
    {
        return std::unique_ptr<IWindowFrame>(new QtWindowFrame());
    }
}  // namespace AA
