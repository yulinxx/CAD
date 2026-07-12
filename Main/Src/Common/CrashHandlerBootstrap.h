#pragma once

#include <string>

/**
 * @file CrashHandlerBootstrap.h
 * @brief SanYiCAD 崩溃捕获引导器（封装 CrashHandler C API）
 */
class CrashHandlerBootstrap
{
public:
    static bool initialize(const std::string& appName, const std::string& appVersion);
    static void shutdown();
    static bool isInitialized();

    /// 启动后记录历史 dump 信息（需在日志系统初始化之后调用）
    static void logPendingDumps();

private:
    CrashHandlerBootstrap() = delete;
};
