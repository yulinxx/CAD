#pragma once

namespace AA
{
    class IShellRuntime;

    // 客户模块的可选扩展点：宿主在创建外壳运行时之后回绑给客户。
    // 避免客户命令出口为了拿外壳而依赖进程级全局；纯头契约，不跨 CRT 传对象。
    class IRuntimeBinding
    {
    protected:
        virtual ~IRuntimeBinding() = default;

    public:
        // runtime 为空表示宿主正在拆除，客户应清掉缓存指针。
        virtual void bindRuntime(IShellRuntime* runtime) = 0;
    };
}  // namespace AA
