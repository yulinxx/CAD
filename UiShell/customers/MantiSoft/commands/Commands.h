#pragma once

#include "UiShell/Contract/CommandTable.h"

namespace MS
{
    // 客户 MantiSoft 的命令：目录 + 执行 + 状态都在一张命令表里（CommandTable）。
    // 菜单、工具栏、面板按钮与快捷键只引用命令 id，执行的都是这里登记的处理函数。
    // 示例状态：「保存」只有在「新建 / 打开」之后才可用，所有入口一起置灰 / 恢复。
    class Commands final : public AA::CommandTable
    {
    public:
        Commands();

    private:
        bool m_hasDocument = false;

        // 当前工具（模式命令）id：菜单、工具栏、左侧栏的高亮都从这一个状态推导。
        QString m_activeTool = QStringLiteral("draw.select");
    };
}  // namespace MS
