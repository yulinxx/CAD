#pragma once

/**
 * @file MacMenuCleanup.h
 * @brief macOS 原生菜单系统项清理
 *
 * macOS 会向名为 "Edit" 的 NSMenu 自动注入系统菜单项
 * （"表情与符号"、"开始听写"等）。本模块在原生菜单栏上
 * 移除这些与应用无关的系统注入项，保留原生菜单栏体验。
 *
 * 非 macOS 平台为空实现，跨平台安全调用。
 */

/**
 * @brief 移除 macOS 原生 Edit 菜单中的系统注入项
 *
 * 遍历 NSApplication 主菜单栏的 Edit 菜单，
 * 移除 action 为 orderFrontCharacterPalette:（表情与符号）
 * 或 startDictation:（开始听写）的 NSMenuItem。
 * 应在菜单创建后、事件循环空闲时调用。
 */
void cleanupMacEditMenuSystemItems();
