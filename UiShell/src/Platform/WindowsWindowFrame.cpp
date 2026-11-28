#ifndef RD_SRC_PLATFORM_WINDOWSWINDOWFRAME_CPP
#define RD_SRC_PLATFORM_WINDOWSWINDOWFRAME_CPP
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Platform/WindowFrame.h"
#include "HitTest.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QMap>
#include <QSet>
#include <QtGlobal>
#include <QWidget>

#include <algorithm>

#include <windows.h>
#include <windowsx.h>

namespace AA
{
    namespace
    {
        // SC_MOVE | SC_DRAGMOVE 让系统立即进入拖动循环
        constexpr WPARAM kScDragMove = 0x0002;

        // 取窗口所在显示器 DPI；失败按 96 兜底
        int dpiForWindow(HWND hwnd)
        {
            const UINT dpi = GetDpiForWindow(hwnd);
            return dpi > 0 ? static_cast<int>(dpi) : 96;
        }

        // 判断 rgrc[0] 是否为最大化溢出矩形（四边严格包住工作区）
        bool isMaximizeSpillover(const RECT& proposed, const RECT& work)
        {
            return proposed.left <= work.left && proposed.top <= work.top &&
                proposed.right >= work.right && proposed.bottom >= work.bottom;
        }
    }  // namespace

    // Windows 原生窗口框架：
    //   WS_CAPTION + WS_THICKFRAME 保留让系统发 WM_NCHITTEST；
    //   WM_NCCALCSIZE 返回 0 覆盖系统边框；
    //   startSystemMove 通过 PostMessage WM_SYSCOMMAND SC_MOVE|SC_DRAGMOVE
    //   让系统接管移动循环，靠边分屏 / 跨屏 DPI 保持原生行为；
    //   WM_NCHITTEST 只做边缘检测，客户区统一 HTCLIENT 交给 Qt。
    //
    // Qt + QML 混合会创建多个顶层 HWND，attach() 里 EnumWindows
    // 枚举全部补齐样式 + 子类化 WNDPROC；主窗口走 nativeEventFilter。

    class WindowsWindowFrame final : public IWindowFrame, public QAbstractNativeEventFilter
    {
    public:
        WindowsWindowFrame() = default;

        ~WindowsWindowFrame() override
        {
            detach();
        }

        // 非主窗口子类化 WNDPROC，主窗口走 Qt nativeEventFilter（已覆盖 SendMessage 同步调用）
        void subclassWindow(HWND hwnd)
        {
            if (!hwnd || hwnd == m_wnd)
                return;
            if (m_subclassed.contains(hwnd))
                return;
            WNDPROC oldProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&WindowsWindowFrame::subclassProc);
            m_subclassed.insert(hwnd, oldProc);
        }

        // 子类化 WNDPROC：拦截非客户区消息，其余转交旧过程
        static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto* self = (WindowsWindowFrame*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!self)
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            auto it = self->m_subclassed.constFind(hwnd);
            WNDPROC oldProc = (it != self->m_subclassed.constEnd()) ? it.value() : nullptr;

            switch (msg)
            {
            case WM_NCCALCSIZE:
            {
                if (wParam == TRUE && lParam != 0)
                {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    self->adjustNcCalcSizeCached(hwnd, params);
                }
                return 0;  // 覆盖系统边框
            }

            case WM_GETMINMAXINFO:
                self->applyMinMaxInfo(hwnd, (MINMAXINFO*)lParam);
                return 0;

            case WM_NCHITTEST:
            {
                MSG fakeMsg = {};
                fakeMsg.hwnd = hwnd;
                fakeMsg.message = WM_NCHITTEST;
                fakeMsg.wParam = wParam;
                fakeMsg.lParam = lParam;
                return self->hitTest(&fakeMsg);
            }

            default:
                break;
            }
            return oldProc ? CallWindowProcW(oldProc, hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
        }

    public:
        void attach(QWidget* window) override
        {
            if (!window || m_wnd)
                return;

            m_window = window;
            window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
            m_wnd = reinterpret_cast<HWND>(window->winId());

            // 保留 WS_CAPTION | WS_THICKFRAME，否则系统不发 WM_NCHITTEST
            LONG_PTR style = GetWindowLongPtrW(m_wnd, GWL_STYLE);
            style &= ~static_cast<LONG_PTR>(WS_BORDER | WS_DLGFRAME);
            style |= WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
            SetWindowLongPtrW(m_wnd, GWL_STYLE, style);

            // 触发系统按新样式重算窗口
            SetWindowPos(
                m_wnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            // Qt + QML 混合会创建多个顶层 HWND，需枚举全部补齐样式
            refreshTargetHwnds();

            if (QApplication::instance())
                QApplication::instance()->installNativeEventFilter(this);
        }

        void refreshTargetHwnds() override
        {
            QSet<HWND> newTargets;
            if (!m_wnd)
            {
                m_targetHwnds.clear();
                return;
            }
            newTargets.insert(m_wnd);

            struct EnumContext
            {
                DWORD pid;
                HWND mainHwnd;
                QSet<HWND>* targets;
                WindowsWindowFrame* self;
            };

            EnumContext enumCtx{ GetCurrentProcessId(), m_wnd, &newTargets, this };
            auto collectExtraWindows = [](HWND hWnd, LPARAM lParam) -> BOOL {
                auto* ctx = reinterpret_cast<EnumContext*>(lParam);
                DWORD ownerPid = 0;
                GetWindowThreadProcessId(hWnd, &ownerPid);
                if (ownerPid != ctx->pid)
                    return TRUE;
                ctx->targets->insert(hWnd);
                if (hWnd == ctx->mainHwnd)
                    return TRUE;
                LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
                style |= WS_CAPTION | WS_THICKFRAME;
                SetWindowLongPtrW(hWnd, GWL_STYLE, style);
                if (ctx->self)
                {
                    ctx->self->subclassWindow(hWnd);
                    // 触发系统重算非客户区 → subclassProc 同步拦截 WM_NCCALCSIZE
                    SetWindowPos(hWnd,
                        nullptr,
                        0,
                        0,
                        0,
                        0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                }
                return TRUE;
                };
            EnumWindows(collectExtraWindows, reinterpret_cast<LPARAM>(&enumCtx));
            m_targetHwnds = newTargets;
        }

        // Windows 由 WM_NCHITTEST + WS_THICKFRAME 处理边缘缩放
        void setResizeBorderWidget(QWidget*) override
        {
        }

        void setMinimumSize(const QSize& size) override
        {
            m_minimumSize = size;
            if (m_window)
                m_window->setMinimumSize(size);
        }

        int resizeBorderWidth() const override
        {
            return 0;
        }

        // 拖动：PostMessage WM_SYSCOMMAND SC_MOVE|SC_DRAGMOVE 让系统接管移动循环。
        // 必须用 PostMessage——Qt WNDPROC 会拦截 SendMessage 的 WM_SYSCOMMAND。
        bool startSystemMove() override
        {
            if (!m_window)
                return false;
            HWND hwnd = (HWND)m_window->winId();
            if (!hwnd)
                return false;
            ReleaseCapture();
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE | kScDragMove, 0);
            return true;
        }

        // Windows 由 WM_NCHITTEST + WS_THICKFRAME 直接命中边缘，不需要手动触发系统缩放。
        // 接口保留是为了 QtWindowFrame（Linux/macOS 自绘窗口）用同一个入口。
        bool startSystemResize(Qt::Edges) override
        {
            return false;
        }

        // 直查原生窗口框体几何（像素）。showNormal 等状态切换后 QWidget 几何异步刷新，
        // 立即读取可能仍是旧状态，GetWindowRect 才保证返回已生效的几何。失败时退回 Qt 缓存。
        QRect nativeFrameGeometry() const override
        {
            if (!m_window)
                return QRect();
            const HWND hwnd = (HWND)m_window->winId();
            RECT rc = {};
            if (!hwnd || !GetWindowRect(hwnd, &rc))
                return m_window->frameGeometry();
            const qreal dpr = m_window->devicePixelRatioF();
            if (dpr <= 0)
                return m_window->frameGeometry();
            return QRect(QPoint(qRound(rc.left / dpr), qRound(rc.top / dpr)),
                QSize(qRound((rc.right - rc.left) / dpr), qRound((rc.bottom - rc.top) / dpr)));
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
#else
        bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override
#endif
        {
            if (!m_wnd || !m_window)
                return false;
            if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
                eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
                return false;

            MSG* msg = static_cast<MSG*>(message);
            if (!msg)
                return false;

            if (msg->hwnd != m_wnd)
                return false;

            switch (msg->message)
            {
            case WM_NCCALCSIZE:
            {
                if (msg->wParam == TRUE && msg->lParam != 0)
                {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                    adjustNcCalcSizeCached(msg->hwnd, params);
                }
                *result = 0;
                return true;
            }

            case WM_GETMINMAXINFO:
                applyMinMaxInfo(msg->hwnd, reinterpret_cast<MINMAXINFO*>(msg->lParam));
                *result = 0;
                return true;

            case WM_NCHITTEST:
                *result = hitTest(msg);
                return true;

            default:
                break;
            }
            return false;
        }

    private:
        void detach()
        {
            if (QApplication::instance())
                QApplication::instance()->removeNativeEventFilter(this);
            for (auto it = m_subclassed.begin(); it != m_subclassed.end(); ++it)
            {
                SetWindowLongPtrW(it.key(), GWLP_WNDPROC, (LONG_PTR)it.value());
                SetWindowLongPtrW(it.key(), GWLP_USERDATA, 0);
            }
            m_subclassed.clear();
            m_wnd = nullptr;
            m_window = nullptr;
        }

        // 只做边缘检测，客户区全部 HTCLIENT 交给 Qt 分发，规避跨窗口 DPI 坐标偏移
        LRESULT hitTest(MSG* msg) const
        {
            const bool maxed = IsZoomed(msg->hwnd) != FALSE;

            RECT client = {};
            GetClientRect(msg->hwnd, &client);
            POINT pt = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
            ScreenToClient(msg->hwnd, &pt);

            return toWin32Hit(hitTestZone(
                pt.x, pt.y, client.right, client.bottom, maxed, edgeBandWidth(dpiForWindow(msg->hwnd))));
        }

        // 平台无关命中语义 → Win32 HT* 常量
        static LRESULT toWin32Hit(HitTestZone zone)
        {
            switch (zone)
            {
            case HitTestZone::Left:
                return HTLEFT;
            case HitTestZone::Right:
                return HTRIGHT;
            case HitTestZone::Top:
                return HTTOP;
            case HitTestZone::Bottom:
                return HTBOTTOM;
            case HitTestZone::TopLeft:
                return HTTOPLEFT;
            case HitTestZone::TopRight:
                return HTTOPRIGHT;
            case HitTestZone::BottomLeft:
                return HTBOTTOMLEFT;
            case HitTestZone::BottomRight:
                return HTBOTTOMRIGHT;
            case HitTestZone::Client:
            default:
                return HTCLIENT;
            }
        }

        // 取窗口中心点所在显示器的工作区矩形
        bool monitorWorkRectFromCenter(HWND hwnd, RECT* outWork, RECT* outMonitor)
        {
            if (!hwnd || !outWork)
                return false;
            RECT rc = {};
            if (!GetWindowRect(hwnd, &rc))
                return false;
            const int cx = rc.left + (rc.right - rc.left) / 2;
            const int cy = rc.top + (rc.bottom - rc.top) / 2;
            HMONITOR hMon = MonitorFromPoint({ cx, cy }, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(hMon, &mi))
                return false;
            *outWork = mi.rcWork;
            if (outMonitor)
                *outMonitor = mi.rcMonitor;
            return true;
        }

        void applyMinMaxInfo(HWND hwnd, MINMAXINFO* info)
        {
            if (!hwnd || !info)
                return;

            RECT work = {}, monitor = {};
            if (monitorWorkRectFromCenter(hwnd, &work, &monitor))
            {
                // 严格等于所在显示器工作区，避免遮挡任务栏或跨屏错位
                info->ptMaxPosition.x = work.left - monitor.left;
                info->ptMaxPosition.y = work.top - monitor.top;
                info->ptMaxSize.x = work.right - work.left;
                info->ptMaxSize.y = work.bottom - work.top;
            }

            if (!m_minimumSize.isEmpty() && m_window)
            {
                const qreal dpr = m_window->devicePixelRatioF();
                info->ptMinTrackSize.x = qRound(m_minimumSize.width() * dpr);
                info->ptMinTrackSize.y = qRound(m_minimumSize.height() * dpr);
            }
        }

        // 修正 WM_NCCALCSIZE 的溢出矩形。
        // 最大化时 Windows 对无边框窗口多给约 7px 溢出边框，用工作区精确覆盖。
        void adjustNcCalcSizeCached(HWND hwnd, NCCALCSIZE_PARAMS* params)
        {
            if (!params)
                return;

            RECT work = {}, monitor = {};
            // 必须检查 rgrc[0]（新位置），还原时 rgrc[1] 是旧最大化矩形会误判
            if (monitorWorkRectFromCenter(hwnd, &work, &monitor) && isMaximizeSpillover(params->rgrc[0], work))
            {
                params->rgrc[0] = work;
            }
        }

        QWidget* m_window = nullptr;
        HWND m_wnd = nullptr;
        QSet<HWND> m_targetHwnds;
        QMap<HWND, WNDPROC> m_subclassed;
        QSize m_minimumSize;
    };

    std::unique_ptr<IWindowFrame> createWindowsWindowFrame()
    {
        return std::unique_ptr<IWindowFrame>(new WindowsWindowFrame());
    }
}  // namespace AA