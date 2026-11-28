#pragma once

class QWidget;

namespace AA
{
    // 标题栏菜单插槽接口；实现者才支持内嵌菜单。
    class ITitleBarMenuHost
    {
    public:
        virtual ~ITitleBarMenuHost() = default;

    public:
        // 挂接/移除菜单（传 nullptr 移除并销毁旧菜单）；所有权转移给标题栏。
        virtual void setMenuWidget(QWidget* menu) = 0;
    };
}  // namespace AA
