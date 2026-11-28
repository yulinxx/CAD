#pragma once

/**
 * @file AprilShellStartup.h
 * @brief April 产品入口在 UiShell 外壳上的兼容层。
 *
 * UiShell/main/main.cpp 是客户无关的入口；只有产品目标 April 定义
 * APR_PRODUCT_STARTUP 并调用这里的钩子。AprilShell 仍是无业务启动的
 * 外壳回归目标，不链接 AprilCore，因此不会引用本文件里的符号。
 */

/**
 * @brief 在 QApplication 创建之前设置平台与 OpenGL 默认格式。
 */
void aprilShellPreAppInit(int argc, char** argv);

/**
 * @brief 处理 --help/--version/--smoke-exit-ms 等命令行开关。
 * @return >=0 表示应直接作为退出码返回；<0 表示继续启动 GUI。
 */
int aprilShellHandleCliFlags(int argc, char** argv, int* smokeExitMs);

/**
 * @brief QApplication 创建之后、UiShell 窗口创建之前的应用初始化。
 * @return 0 继续启动；非 0 作为退出码返回。
 */
int aprilShellPostAppInit(int argc, char** argv);

/// 组合根装配完成后执行配置自检与退出时保存接线。
void aprilShellAfterBootstrap();

/// 事件循环结束后关闭日志/崩溃捕获/初始化器。
void aprilShellShutdown();
