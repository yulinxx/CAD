#pragma once

/**
 * @file RenderCoreApi.h
 * @brief 统一渲染抽象层导出宏
 *
 * RenderCore 作为静态库链接到 Main 可执行文件，使用空宏即可。
 * 若未来改为独立 DLL，只需修改此文件即可切换导出模式。
 */

// 静态库模式：不需要 dllimport/dllexport
#define RENDER_CORE_API