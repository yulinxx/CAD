/**
 * @file RenderXDeviceAdapter.cpp
 * @brief IRenderDevice 的 RenderX 实现
 *
 * 所有方法将 RenderAbstraction 业务类型转换为 Render::RT::C ABI 类型，
 * 调用 renderx.h 的 rx* 函数。
 *
 * 注意：本文件是项目中唯一直接 #include "render/renderx.h" 的源文件之一
 * （另一个是 RenderXSceneAdapter.cpp）。所有其他源文件不得直接包含 renderx.h。
 */

#include "RenderBridge/RenderXAdapter.h"
#include "RenderBridge/RenderSessionHost.h"

// 转换声明集中处。包含它（而非直接用）是为了让这个私有头始终处于编译
// 验证之下；当前转换实现仍以本文件内的私有静态成员函数形式存在，
// 接线到 IRenderFactory 路径时再搬到 TypeConversions.cpp。
#include "TypeConversions.h"

#include "render/renderx.h"

#include "Log/SyLogger.h"

#include <cstring>

namespace RenderBridge {

// 以下适配器实现位于 RenderBridge 命名空间，但所有抽象接口（IRenderDevice /
// IRenderSurface / IRenderScene / IRenderFactory）及其参数类型（CommandListHandle /
// SurfaceHandle / TextureHandle 等）都在 RenderAbstraction 命名空间中定义。
// 使用 using namespace 让裸名类型在适配器内部可见。
using namespace RenderAbstraction;
// 同理引入转换函数，适配器内可直接写 toRenderXFormat(...) 这样的裸名。
// 转换实现集中在 TypeConversions.cpp，本文件不再自带一份。
using namespace RenderXConversion;

class RenderXDeviceAdapter : public RenderAbstraction::IRenderDevice {
public:
    explicit RenderXDeviceAdapter(Render::RT::RuntimeHandle runtime)
        : m_runtime(runtime) {}

    ~RenderXDeviceAdapter() override = default;

    const char* backendName() const override {
        return Render::RT::rxBackendName(Render::RT::Backend::OpenGL);
    }

    float maxLineWidth() const override {
        Render::RT::Capabilities caps{};
        if (Render::RT::rxRuntimeGetCapabilities(m_runtime, &caps) == Render::RT::RxResult::Ok) {
            return caps.maxLineWidth;
        }
        return 1.0f;
    }

    int maxTextureSize() const override {
        Render::RT::Capabilities caps{};
        if (Render::RT::rxRuntimeGetCapabilities(m_runtime, &caps) == Render::RT::RxResult::Ok) {
            return static_cast<int>(caps.maxTextureSize);
        }
        return 0;
    }

    RenderAbstraction::BufferHandle createBuffer(uint64_t sizeBytes, bool cpuWritable) override {
        Render::RT::BufferDesc desc{};
        desc.sizeBytes = sizeBytes;
        desc.cpuWritable = cpuWritable ? 1 : 0;
        return { static_cast<uint64_t>(Render::RT::rxBufferCreate(m_runtime, &desc)) };
    }

    void destroyBuffer(RenderAbstraction::BufferHandle buffer) override {
        Render::RT::rxBufferDestroy(m_runtime, static_cast<Render::RT::BufferHandle>(buffer.value));
    }

    void uploadBuffer(RenderAbstraction::BufferHandle buffer, uint64_t offset,
                       uint64_t sizeBytes, const void* data) override {
        Render::RT::rxBufferUpload(m_runtime,
            static_cast<Render::RT::BufferHandle>(buffer.value), offset, sizeBytes, data);
    }

    RenderAbstraction::TextureHandle createTexture(const RenderAbstraction::TextureDesc& desc) override {
        Render::RT::TextureDesc tdesc{};
        tdesc.width = desc.width;
        tdesc.height = desc.height;
        tdesc.rgba = desc.rgba;
        tdesc.rgbaBytes = desc.rgbaBytes;
        return { static_cast<uint64_t>(Render::RT::rxTextureCreate(m_runtime, &tdesc)) };
    }

    void destroyTexture(RenderAbstraction::TextureHandle texture) override {
        Render::RT::rxTextureDestroy(m_runtime, static_cast<Render::RT::TextureHandle>(texture.value));
    }

    void updateTexture(RenderAbstraction::TextureHandle texture, uint32_t x, uint32_t y,
                        uint32_t width, uint32_t height, const uint8_t* rgba) override {
        // RenderX 的 rxTextureUpdate 语义: 全部更新
        // 如果需要部分更新，需要扩展 ABI
        (void)x; (void)y;
        Render::RT::TextureDesc tdesc{};
        tdesc.width = width;
        tdesc.height = height;
        tdesc.rgba = rgba;
        tdesc.rgbaBytes = static_cast<uint64_t>(width * height * 4);
        Render::RT::rxTextureUpdate(m_runtime,
            static_cast<Render::RT::TextureHandle>(texture.value), &tdesc);
    }

    uint32_t getDefaultPipeline(RenderAbstraction::VertexFormat format,
                                 RenderAbstraction::RenderSpace space,
                                 RenderAbstraction::PrimitiveType topology) override {
        // (格式, 空间, 拓扑) → RenderX 的内建默认管线编号
        return static_cast<uint32_t>(Render::RT::rxPipelineGetDefault(m_runtime,
            toRenderXDefaultPipeline(format, space, topology)));
    }

    uint32_t createPipeline(const RenderAbstraction::PipelineDesc& desc) override {
        Render::RT::PipelineDesc pdesc{};
        pdesc.topology = toRenderXPrimitive(desc.topology);
        pdesc.vertexFormat = toRenderXFormat(desc.format);
        pdesc.depthTest = desc.depthTest;
        pdesc.depthWrite = desc.depthWrite;
        pdesc.blendEnable = desc.blendEnable;
        pdesc.srcBlend = toRenderXBlend(desc.srcBlend);
        pdesc.dstBlend = toRenderXBlend(desc.dstBlend);
        pdesc.depthFunc = toRenderXDepthFunc(desc.depthFunc);
        pdesc.fillMode = toRenderXFillMode(desc.fillMode);
        pdesc.depthBiasConstant = desc.depthBiasConstant;
        pdesc.depthBiasSlope = desc.depthBiasSlope;
        pdesc.shaderName = nullptr;
        // rxPipelineCreate 返回 uint16 索引
        return static_cast<uint32_t>(Render::RT::rxPipelineCreate(m_runtime, &pdesc));
    }

private:
    Render::RT::RuntimeHandle m_runtime;

    friend class RenderXFactory;
};

class RenderXSurfaceAdapter : public RenderAbstraction::IRenderSurface {
public:
    RenderXSurfaceAdapter(Render::RT::RuntimeHandle runtime, Render::RT::SurfaceHandle surface)
        : m_runtime(runtime), m_surface(surface) {}

    void resize(uint32_t width, uint32_t height) override {
        Render::RT::rxSurfaceResize(m_runtime, m_surface, width, height);
    }

    bool isValid() const override {
        return Render::RT::rxValid(m_surface) != 0;
    }

    uint32_t width() const override { return m_width; }
    uint32_t height() const override { return m_height; }

private:
    Render::RT::RuntimeHandle m_runtime;
    Render::RT::SurfaceHandle m_surface;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

class RenderXSceneAdapter : public RenderAbstraction::IRenderScene {
public:
    RenderXSceneAdapter(Render::RT::RuntimeHandle runtime, Render::RT::SessionHandle session)
        : m_runtime(runtime), m_session(session) {}

    ~RenderXSceneAdapter() override = default;

    bool initialize(IRenderDevice& device) override {
        (void)device;
        return true;
    }

    CommandListHandle createCommandList(uint32_t initialCapacity) override {
        (void)initialCapacity;
        // RenderX 的 DrawList 由 PersistentGeometryStore 管理
        // 这里简化：每个 CommandList 对应一个 DrawList
        Render::RT::DrawListDesc desc{};
        desc.initialCapacity = initialCapacity;
        desc.enableMerging = 1;
        desc.enableCulling = 1;
        return { static_cast<uint64_t>(Render::RT::rxDrawListCreate(m_runtime, &desc)) };
    }

    void destroyCommandList(CommandListHandle list) override {
        if (Render::RT::rxValid(static_cast<Render::RT::DrawListHandle>(list.value))) {
            Render::RT::rxDrawListDestroy(m_runtime, static_cast<Render::RT::DrawListHandle>(list.value));
        }
    }

    void clearCommandList(CommandListHandle list) override {
        Render::RT::rxDrawListClear(m_runtime, static_cast<Render::RT::DrawListHandle>(list.value));
    }

    BufferHandle uploadGeometry(const void* vertices, uint64_t sizeBytes, bool persistent) override {
        // 使用 RenderBridge::PersistentGeometryStore 或其他机制
        // 简化版：创建缓冲并上传
        auto buffer = static_cast<RenderXDeviceAdapter*>(nullptr);
        (void)buffer;
        // 实际实现应使用 GeometryStore 或 Buffer
        return {};
    }

    void uploadGeometryBatch(const void* const* vertexArrays, const uint64_t* sizeBytes,
                            int count, bool persistent) override {
        // stub：逐个调用 uploadGeometry，结果暂不返回
        for (int i = 0; i < count; ++i) {
            uploadGeometry(vertexArrays[i], sizeBytes[i], persistent);
        }
    }

    void beginFrame() override {
        Render::RT::rxSessionBeginFrame(m_session);
    }

    void endFrame() override {
        Render::RT::rxSessionEndFrame(m_session);
    }

    void submitToScreen(CommandListHandle list) override {
        // RenderAbstraction 的 CommandListHandle 包装的就是 DrawListHandle
        Render::RT::rxSessionSubmitDrawList(m_session,
            static_cast<Render::RT::DrawListHandle>(list.value),
            nullptr);  // nullptr = 关闭剔除，整表提交
    }

    void submitToSurface(CommandListHandle list, SurfaceHandle surface) override {
        (void)surface; // Session 已绑定 Surface，忽略此参数
        submitToScreen(list);
    }

    TextureHandle createRenderTarget(uint32_t width, uint32_t height) override {
        Render::RT::RenderTargetDesc rtDesc{};
        rtDesc.width = width;
        rtDesc.height = height;
        rtDesc.usage = Render::RT::TextureUsageFlag::ColorAttachment | Render::RT::TextureUsageFlag::TransferSrc;
        rtDesc._pad0 = 0;
        return { static_cast<uint64_t>(Render::RT::rxTextureCreateRenderTarget(m_runtime, &rtDesc)) };
    }

    void readPixels(TextureHandle target, void* outPixels, uint32_t width, uint32_t height) override {
        Render::RT::rxSessionReadPixelsFromTexture(m_session,
            static_cast<Render::RT::TextureHandle>(target.value),
            0, 0, width, height,
            outPixels,
            static_cast<uint64_t>(width) * height * 4);
    }

    void setCamera(const RenderAbstraction::CameraDesc& camera) override {
        float viewMatrix[16];
        toRenderXViewMatrix(camera.viewMatrix, viewMatrix);
        Render::RT::rxSessionSetViewMatrix(m_session, viewMatrix);
    }

    void setLighting(const RenderAbstraction::LightingDesc& lighting) override {
        const Render::RT::Lighting3DDesc ldesc = toRenderXLighting(lighting);
        Render::RT::rxSessionSetLighting3D(m_session, &ldesc);
    }

    FrameStatistics getFrameStatistics() const override {
        Render::RT::FrameStats stats{};
        if (Render::RT::rxSessionGetStats(m_session, &stats) == Render::RT::RxResult::Ok) {
            return fromRenderXStats(stats);
        }
        return {};
    }

    bool isValid() const override {
        return Render::RT::rxValid(m_session) != 0;
    }

private:
    Render::RT::RuntimeHandle m_runtime;
    Render::RT::SessionHandle m_session;
};

class RenderXFactory : public RenderAbstraction::IRenderFactory {
public:
    std::unique_ptr<RenderAbstraction::IRenderDevice> createDevice(const RenderAbstraction::DeviceConfig& config) override {
        Render::RT::RuntimeDesc rd{};
        // RENDERX_ABI_VERSION 是宏（不是 namespace 成员），直接引用即可
        rd.abiVersion = RENDERX_ABI_VERSION;
        rd.backend = static_cast<Render::RT::Backend>(config.backend);
        rd.enableValidation = config.enableValidation ? 1 : 0;
        rd.transientBufferBytes = config.transientBufferBytes;
        // 函数指针类型不同（LogLevel 枚举不同），用 reinterpret_cast 适配。
        // 两者底层都是 int32_t + const char* + void*，ABI 完全兼容。
        rd.logCallback = reinterpret_cast<Render::RT::rxLogCallback>(config.logCallback);
        rd.logUserData = config.logUserData;
        rd.applicationName = config.applicationName;
        rd.glGetProcAddress = nullptr;

        auto runtime = Render::RT::rxRuntimeCreate(&rd);
        if (!Render::RT::rxValid(runtime)) {
            return nullptr;
        }
        return std::make_unique<RenderXDeviceAdapter>(runtime);
    }

    std::unique_ptr<RenderAbstraction::IRenderSurface> createSurface(
        RenderAbstraction::IRenderDevice& device, const RenderAbstraction::SurfaceConfig& config) override {
        auto* dxDevice = static_cast<RenderXDeviceAdapter*>(&device);
        Render::RT::SurfaceDesc sd{};
        sd.presentMode = Render::RT::PresentMode::Fifo;
        sd.width = config.width;
        sd.height = config.height;
        sd.enableDepth = config.enableDepth ? 1 : 0;
        sd.windowKind = Render::RT::NativeWindowKind::ForeignGlContext;
        sd.handleA = config.nativeWindow.handleA;
        sd.handleB = config.nativeWindow.handleB;
        sd._pad0[0] = 0;
        sd._pad1 = 0;

        auto surface = Render::RT::rxSurfaceCreate(dxDevice->m_runtime, &sd);
        if (!Render::RT::rxValid(surface)) {
            return nullptr;
        }
        return std::make_unique<RenderXSurfaceAdapter>(dxDevice->m_runtime, surface);
    }

    std::unique_ptr<RenderAbstraction::IRenderScene> createScene(
        RenderAbstraction::IRenderDevice& device) override {
        auto* dxDevice = static_cast<RenderXDeviceAdapter*>(&device);
        Render::RT::SessionDesc ses{};
        ses.runtime = dxDevice->m_runtime;
        ses.surface = Render::RT::SurfaceHandle::Invalid; // 需要先从 createSurface 获取
        ses.clearColor[0] = 0.94f;
        ses.clearColor[1] = 0.94f;
        ses.clearColor[2] = 0.94f;
        ses.clearColor[3] = 1.0f;

        auto session = Render::RT::rxSessionCreate(&ses);
        if (!Render::RT::rxValid(session)) {
            return nullptr;
        }
        return std::make_unique<RenderXSceneAdapter>(dxDevice->m_runtime, session);
    }
};

std::unique_ptr<RenderBridge::RenderXFactory> RenderXAdapter::createFactory() {
    return std::make_unique<RenderXFactory>();
}

} // namespace RenderBridge
