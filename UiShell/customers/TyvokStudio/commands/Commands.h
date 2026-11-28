#pragma once

#include "UiShell/Contract/CommandTable.h"

namespace TS
{
    // TyvokStudio 的命令表。示例状态：加工「开始 / 暂停」——
    //   开始：未运行或已暂停时可用，执行后进入运行；
    //   暂停：运行中才可用，是开关类命令（勾选 = 已暂停）。
    // 菜单与 profile 工具栏用的是同一个动作，勾选与置灰在两处同时变化。
    class Commands final : public AA::CommandTable
    {
    public:
        Commands();

    private:
        bool m_running = false;
        bool m_paused = false;
    };
}  // namespace TS
