#pragma once

class QMainWindow;

namespace AA
{
    // M6：宿主在创建客户模块之前把主窗口交给外壳。客户模块据此装配其服务根；
    // 纯头契约，不暴露任何 Main 类型。
    void setHostMainWindow(QMainWindow* window);
    QMainWindow* hostMainWindow();
}  // namespace AA
