#pragma once
#include "IRenderDevice.h"
#include "IRenderSurface.h"
#include "IRenderScene.h"
#include <memory>
#include <functional>

namespace RenderAbstraction {

using LogCallback = void (*)(LogLevel level, const char* message, void* userData);

struct DeviceConfig {
    RenderBackend backend = RenderBackend::Auto;
    bool enableValidation = false;
    uint64_t transientBufferBytes = 64ull * 1024 * 1024;
    LogCallback logCallback = nullptr;
    void* logUserData = nullptr;
    const char* applicationName = "CAD";
};

/**
 * @brief 渲染后端工厂
 *
 * **只负责建设备**。表面与场景的创建不在这里，因为它们的成败取决于宿主侧的事实，
 * 本层配置表达不出来：
 *
 *  - GL 的上下文由 Qt 拥有，DLL 需要宿主提供的符号解析器（glGetProcAddress）；
 *  - Metal 的交换链必须挂在宿主自建的 NSView 上（句柄只能在宿主侧取得）；
 *  - Metal 的 Runtime 是**进程级共享**的，销毁时机要与「最后一个视口释放」对齐。
 *
 * 这些都在 `RenderBridge::RenderSessionHost` 里 —— 它才是产品实际走的入口，本工厂
 * 用于独立设备（契约测试、离屏工具等不需要交换链的场景）。
 */
class IRenderFactory {
public:
    virtual ~IRenderFactory() = default;

    virtual std::unique_ptr<IRenderDevice> createDevice(const DeviceConfig& config) = 0;
};

} // namespace RenderAbstraction
