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

struct SurfaceConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    bool enableDepth = true;
    NativeWindowHandle nativeWindow;
};

class IRenderFactory {
public:
    virtual ~IRenderFactory() = default;

    virtual std::unique_ptr<IRenderDevice> createDevice(const DeviceConfig& config) = 0;
    virtual std::unique_ptr<IRenderSurface> createSurface(IRenderDevice& device, const SurfaceConfig& config) = 0;
    virtual std::unique_ptr<IRenderScene> createScene(IRenderDevice& device) = 0;
};

} // namespace RenderAbstraction
