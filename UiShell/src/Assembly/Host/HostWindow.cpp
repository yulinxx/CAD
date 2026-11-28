#include "UiShell/Assembly/HostWindow.h"

namespace AA
{
    namespace
    {
        QMainWindow* g_hostMainWindow = nullptr;
    }  // namespace

    void setHostMainWindow(QMainWindow* window)
    {
        g_hostMainWindow = window;
    }

    QMainWindow* hostMainWindow()
    {
        return g_hostMainWindow;
    }
}  // namespace AA
