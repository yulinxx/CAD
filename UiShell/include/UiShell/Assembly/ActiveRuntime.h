#pragma once

namespace AA
{
    class IShellRuntime;

    // M6：当前进程中的外壳运行时。客户库（如 April）在命令分发时需要把
    // theme.*/language.*/help.about 交回外壳，但客户模块创建早于运行时创建，
    // 因此通过外壳发布的单一指针获取。纯头契约，不跨 CRT 传对象。
    void setActiveRuntime(IShellRuntime* runtime);
    IShellRuntime* activeRuntime();
}  // namespace AA
