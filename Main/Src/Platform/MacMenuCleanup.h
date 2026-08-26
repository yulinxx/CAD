#pragma once

// Q_OS_MACOS 的唯一来源。缺了它，下面的平台分支会静默走到空实现分支：
// 编译链接全过，功能为零，而且没有任何编译期提示。
#include <QtGlobal>

/**
 * @file MacMenuCleanup.h
 * @brief macOS 原生菜单系统项清理
 *
 * macOS 会向名为 "Edit" 的 NSMenu 自动注入系统菜单项
 * （"表情与符号"、"开始听写"等）。本模块在原生菜单栏上
 * 移除这些与应用无关的系统注入项，保留原生菜单栏体验。
 *
 * 非 macOS 平台为内联空实现，调用点无需条件编译。
 */

#ifdef Q_OS_MACOS

/**
 * @brief 移除 macOS 原生 Edit 菜单中的系统注入项
 *
 * 遍历 NSApplication 主菜单栏的 Edit 菜单，
 * 移除 action 为 orderFrontCharacterPalette:（表情与符号）
 * 或 startDictation:（开始听写）的 NSMenuItem。
 *
 * 必须在菜单创建后、事件循环空闲时调用：Qt 的原生菜单栏是延迟同步到 NSMenu 的，
 * 紧跟菜单构建同步调用时 NSMenu 还没成形，清理会扫到空菜单。
 * 另外菜单每次重建（切工作台 / 切语言 / 重载客户配置）都会重新生成整棵 NSMenu，
 * 因此本函数需要在每次重建后调用，而不是启动时调一次。
 *
 * 实现见 MacMenuCleanup.mm（仅在 APPLE 平台参与编译，见 Main/CMakeLists.txt）。
 */
void cleanupMacEditMenuSystemItems();

#else

/// 非 macOS 平台无系统注入项，内联空实现使调用点保持无条件。
inline void cleanupMacEditMenuSystemItems()
{
}

#endif
