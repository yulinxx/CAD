#pragma once

namespace AA
{
    class ITitleBarHost
    {
    public:
        virtual ~ITitleBarHost() = default;

    public:
        // 请求拖动窗口；鼠标左键处于按下状态，由宿主转交平台层发起系统拖动。
        virtual void requestDrag() = 0;

        virtual void requestMinimize() = 0;

        // 请求在最大化与还原之间切换。
        virtual void requestToggleMaximize() = 0;

        virtual void requestClose() = 0;

        // 查询窗口当前是否最大化。
        virtual bool isMaximized() const = 0;
    };
}  // namespace AA
