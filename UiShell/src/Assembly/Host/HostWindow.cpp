#include "UiShell/Assembly/HostWindow.h"

#include "UiShell/Assembly/ActiveRuntime.h"

namespace RD
{
    namespace
    {
        QMainWindow* g_hostMainWindow = nullptr;
        IShellRuntime* g_activeRuntime = nullptr;
    }  // namespace

    void setHostMainWindow(QMainWindow* window)
    {
        g_hostMainWindow = window;
    }

    QMainWindow* hostMainWindow()
    {
        return g_hostMainWindow;
    }

    void setActiveRuntime(IShellRuntime* runtime)
    {
        g_activeRuntime = runtime;
    }

    IShellRuntime* activeRuntime()
    {
        return g_activeRuntime;
    }
}  // namespace RD
