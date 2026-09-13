#pragma once
/**
 * @file HostCallbacks.h
 * @brief 宿主 ↔ RenderX 的两处固定桥接：日志回调与 GL 符号解析
 *
 * 这两件事每个视口创建 Runtime 时都要做一遍，且做法必须一致，
 * 因此收在这里而不是在 2D/3D 各写一份 —— 曾经就是各写一份，
 * 两边的日志前缀与降级策略开始漂移。
 *
 * 头文件内联而非 .cpp：函数指针要作为 `RuntimeDesc::logCallback` /
 * `glGetProcAddress` 传进 DLL，取地址点在调用方；放 .cpp 需要调用方
 * 依赖 Log 库的实现细节，代价大于收益。
 *
 * 只有视口（2D/3D）会 include 本文件，它们本来就链接 RenderBridge，
 * 也就间接 PUBLIC 依赖 RenderX。
 */

#include "render/renderx.h"

#include "Log/SyLogger.h"

#include <QOpenGLContext>

namespace Render
{
    namespace host
    {
        /// 把 DLL 的日志回调桥接到宿主日志库。DLL 自身不依赖任何日志实现。
        inline void rxLogBridge(Render::RT::LogLevel level, const char* message, void* /*userData*/)
        {
            if (!message)
            {
                return;
            }
            switch (level)
            {
            case Render::RT::LogLevel::Debug:
            case Render::RT::LogLevel::Info:
                SY_INFOF("[Renderx] %s", message);
                break;
            case Render::RT::LogLevel::Warn:
                SY_WARNF("[Renderx] %s", message);
                break;
            case Render::RT::LogLevel::Error:
                SY_ERRORF("[Renderx] %s", message);
                break;
            }
        }

        /**
         * @brief 把 Qt 的符号解析桥接给 DLL 的 GL 后端
         *
         * 不能让 DLL 走平台默认路径（Windows `wglGetProcAddress`、Linux
         * `glXGetProcAddress`、macOS `dlsym`）：Qt 在部分平台上用的是另一套实现
         * （Windows 的 ANGLE / EGL 后端最典型），两套混用会得到「函数指针非空但
         * 行为不对」，没有任何报错，只表现为画面局部异常。
         *
         * 函数指针 → void* 的转换在 C++ 里是实现定义行为，但所有目标平台
         * （Win/macOS/Linux 的 ABI）都保证两者同宽且可往返，GL 的
         * getProcAddress 家族本身就是这么用的。
         */
        inline void* qtGlGetProcAddress(const char* name)
        {
            QOpenGLContext* ctx = QOpenGLContext::currentContext();
            if (!ctx || !name)
            {
                return nullptr;
            }
            return reinterpret_cast<void*>(ctx->getProcAddress(name));
        }
    }  // namespace host
}  // namespace Render
