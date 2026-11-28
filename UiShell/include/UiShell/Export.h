#pragma once

// 导出/导入宏与跨模块（EXE / UiShell.dll / UiCustomerX.dll）规则。
//
// 1. 只导出自由函数（createXxx / xxxBuildFingerprint），不导出类：类的成员与内联实现不进入导出表。
// 2. 跨模块的参数、返回值与结构体成员只用：基本类型、裸指针、纯虚接口、Qt 值类型（QString / QList /
//    QJsonObject …，内存由 Qt6Core.dll 统一分配，各模块共用同一份堆）。
//    不传 std::string / std::vector / std::function / std::unique_ptr 等标准库对象：
//    它们的布局与分配器随编译器、CRT（/MD /MT）和 _ITERATOR_DEBUG_LEVEL 变化，Debug / Release 混装即崩溃。
//    回调用纯虚接口（如 IFeatureFlagObserver、IShellListener），不用 std::function。
//    cmake/CheckLayers.cmake 检查公开头里没有 std:: 类型（只在客户模块内实例化的 Retranslatable.h 除外）。
// 3. 谁创建谁释放：shell 创建的对象继承 IApiObject，经 destroy()（通常由 ApiPtr）在 shell 内释放；
//    这些接口的析构函数是 protected，跨模块直接 delete 编译不过。
//    QWidget 交给 Qt 父对象销毁：虚析构经对象自身模块的 deleting destructor 释放，不会跨堆。
// 4. 异常不跨模块：接口实现（含客户的命令、对话框、面板工厂）不得让异常逃出虚函数调用。
// 5. 三个产物必须同一次构建：构建指纹（BuildFingerprint.h）比较 api / Qt / 编译器 / 位数 / CRT /
//    Debug|Release / 源码版本，任一不同即拒绝启动；接口布局因此不做运行期版本协商。

#if defined(UI_STATIC)

#define UI_DECL_EXPORT
#define UI_DECL_IMPORT
#define UI_SHELL_API
#define UI_CUSTOMER_API

#else

#if defined(_WIN32) || defined(__CYGWIN__)
#define UI_DECL_EXPORT __declspec(dllexport)
#define UI_DECL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define UI_DECL_EXPORT __attribute__((visibility("default")))
#define UI_DECL_IMPORT __attribute__((visibility("default")))
#else
#define UI_DECL_EXPORT
#define UI_DECL_IMPORT
#endif

#if defined(UI_SHELL_EXPORTS)
#define UI_SHELL_API UI_DECL_EXPORT
#else
#define UI_SHELL_API UI_DECL_IMPORT
#endif

// 客户模块由各客户目录自己编译；构建时只链接 UI_CUSTOMER 选中的那个。
#if defined(UI_CUSTOMER_EXPORTS)
#define UI_CUSTOMER_API UI_DECL_EXPORT
#else
#define UI_CUSTOMER_API UI_DECL_IMPORT
#endif

#endif  // UI_STATIC
