/**
 * @file RenderXDeviceAdapter.cpp
 * @brief IRenderDevice 的 RenderX 实现
 *
 * 所有方法将 RenderAbstraction 业务类型转换为 Render::RT::C ABI 类型，
 * 调用 renderx.h 的 rx* 函数。
 *
 * 注意：直接 #include "render/renderx.h" 的源文件只允许出现在 RenderBridge 内部
 * （本文件、TypeConversions.cpp、RenderSessionHost.cpp、PersistentGeometryStore.cpp
 * 等）。UI 层任何源文件都不得直接包含它。
 */

#include "RenderBridge/RenderXAdapter.h"
#include "RenderBridge/RenderSessionHost.h"

// 句柄已由宿主建好时的包装入口；包含它让声明与实现始终处于同一份契约之下
#include "RenderXAdapterInternal.h"

// 转换声明集中处，实现全在 TypeConversions.cpp。包含它让本文件与转换契约
// 始终处于同一份声明之下——适配器内只写「取字段、调 rx*」，不自己拼类型。
#include "TypeConversions.h"

#include "render/renderx.h"

#include "Log/SyLogger.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace RenderBridge {

// 以下适配器实现位于 RenderBridge 命名空间，但所有抽象接口（IRenderDevice /
// IRenderSurface / IRenderScene / IRenderFactory）及其参数类型（DrawListHandle /
// TextureHandle / GeometryStoreHandle 等）都在 RenderAbstraction 命名空间中定义。
// 使用 using namespace 让裸名类型在适配器内部可见。
using namespace RenderAbstraction;
// 同理引入转换函数，适配器内可直接写 toRenderXFormat(...) 这样的裸名。
// 转换实现集中在 TypeConversions.cpp，本文件不再自带一份。
using namespace RenderXConversion;

namespace {

/**
 * @brief 记一条 RxResult 失败日志
 *
 * 抽象层的调用方只拿到 bool，拿不到 RxResult 也认不出它——错误名的知识属于
 * 适配层。收口之前 UI 自己在每个调用点把 rxResultName 拼进日志，收口之后
 * 在这里打，诊断信息不丢，而且少了「忘了打」这种疏漏。
 */
void logRxFailure(const char* what, Render::RT::RxResult result) {
    SY_ERRORF("[RenderXAdapter] %s failed: %s", what ? what : "?", Render::RT::rxResultName(result));
}

/// 只记结果的辅助：成功返回 true，失败按 what 打日志后返回 false
bool checkRx(const char* what, Render::RT::RxResult result) {
    if (result == Render::RT::RxResult::Ok) {
        return true;
    }
    logRxFailure(what, result);
    return false;
}

/**
 * @brief 校验抽象层声明的顶点步长与后端实际步长一致
 *
 * 宿主侧各自用自己的顶点结构（VertexP3C3 / OVertex / GVertex ...），它们的
 * static_assert 全部指向 RenderAbstraction::vertexStride —— 那张表一旦与后端
 * 的实际步长偏离，全套几何都会整体错位，而画面上的表现是「模型扭曲」，
 * 极难反推到步长上。因此这里逐格式对一遍，只在进程内第一次建 Runtime 时跑，
 * 成本可忽略。
 */
struct StridePair {
    RenderAbstraction::VertexFormat abstract;
    Render::RT::VertexFormat raw;
    const char* name;
};

/// 成功返回 true；不一致时打错误日志并返回 false
bool checkStride(const StridePair& pair) {
    const uint32_t declared = RenderAbstraction::vertexStride(pair.abstract);
    const uint32_t actual = Render::RT::rxVertexStride(pair.raw);
    if (declared == actual) {
        return true;
    }
    SY_ERRORF("[RenderXAdapter] vertexStride(%s) 声明 %u 与实际 %u 不一致："
              "所有用该格式的几何都会错位，请同步 IRenderTypes.h",
        pair.name, declared, actual);
    return false;
}

void verifyVertexStrides() {
    const StridePair kPairs[] = {
        { RenderAbstraction::VertexFormat::PositionColor,          Render::RT::VertexFormat::P3C3,   "PositionColor" },
        { RenderAbstraction::VertexFormat::PositionColorAlpha,     Render::RT::VertexFormat::P3C4,   "PositionColorAlpha" },
        { RenderAbstraction::VertexFormat::PositionNormal,         Render::RT::VertexFormat::P3N3,   "PositionNormal" },
        { RenderAbstraction::VertexFormat::PositionUVColor,        Render::RT::VertexFormat::P2T2C4, "PositionUVColor" },
        { RenderAbstraction::VertexFormat::WorldPosUVColor,        Render::RT::VertexFormat::P3T2C4, "WorldPosUVColor" },
        { RenderAbstraction::VertexFormat::WorldAnchorOffsetColor, Render::RT::VertexFormat::P3O2C4, "WorldAnchorOffsetColor" },
    };
    for (const StridePair& pair : kPairs) {
        checkStride(pair);
    }
}

} // namespace

class RenderXDeviceAdapter : public RenderAbstraction::IRenderDevice {
public:
    /**
     * @param ownsRuntime 句柄是否由本对象创建。工厂路径自己建 Runtime，因此为 true；
     *                    宿主把已有句柄交给 wrapRenderXDevice 包装时为 false——
     *                    那种场景下句柄的生命周期归宿主（见 RenderSessionHost）。
     */
    RenderXDeviceAdapter(Render::RT::RuntimeHandle runtime, bool ownsRuntime)
        : m_runtime(runtime), m_ownsRuntime(ownsRuntime) {
        // 设备名查询一次即定：它是 Runtime 级常量，逐次查询等于每次问一遍同样的答案
        if (Render::RT::rxRuntimeGetCapabilities(m_runtime, &m_caps) != Render::RT::RxResult::Ok) {
            m_caps = Render::RT::Capabilities{};
        }
        m_backendName = Render::RT::rxBackendName(m_caps.backend);
        // 步长一致性只与代码有关，与具体设备无关，因此整个进程只需要校验一次
        static const bool kStridesVerified = [] { verifyVertexStrides(); return true; }();
        (void)kStridesVerified;
    }

    ~RenderXDeviceAdapter() override {
        if (m_ownsRuntime && Render::RT::rxValid(m_runtime)) {
            Render::RT::rxRuntimeDestroy(m_runtime);
        }
    }

    const char* backendName() const override {
        return m_backendName;
    }

    const char* deviceName() const override {
        return m_caps.deviceName;
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

    // ---------- 常驻几何仓 ----------

    RenderAbstraction::GeometryStoreHandle createGeometryStore(
        const RenderAbstraction::GeometryStoreDesc& desc) override {
        const Render::RT::GeometryStoreDesc raw = toRenderXGeometryStoreDesc(desc);
        const Render::RT::GeometryStoreHandle store = Render::RT::rxGeometryStoreCreate(m_runtime, &raw);
        if (!Render::RT::rxValid(store)) {
            SY_ERRORF("[RenderXAdapter] rxGeometryStoreCreate failed (initial=%llu max=%llu granularity=%u)",
                static_cast<unsigned long long>(raw.initialBytes),
                static_cast<unsigned long long>(raw.maxBytes),
                raw.granularity);
            return {};
        }
        return { static_cast<uint64_t>(store) };
    }

    void destroyGeometryStore(RenderAbstraction::GeometryStoreHandle store) override {
        if (RenderAbstraction::isValid(store) && Render::RT::rxValid(m_runtime)) {
            Render::RT::rxGeometryStoreDestroy(m_runtime,
                static_cast<Render::RT::GeometryStoreHandle>(store.value));
        }
    }

    RenderAbstraction::GeometryAllocResult allocGeometry(RenderAbstraction::GeometryStoreHandle store,
                                                         uint64_t bytes,
                                                         RenderAbstraction::GeometryBlock& out) override {
        if (!RenderAbstraction::isValid(store)) {
            return RenderAbstraction::GeometryAllocResult::Failed;
        }
        Render::RT::GeometryBlock raw{};
        const Render::RT::RxResult result = Render::RT::rxGeometryAlloc(m_runtime,
            static_cast<Render::RT::GeometryStoreHandle>(store.value), bytes, &raw);
        const RenderAbstraction::GeometryAllocResult converted = fromRenderXGeometryAlloc(result);
        if (converted != RenderAbstraction::GeometryAllocResult::Ok) {
            // StoreFull 是调用方要处理的正常分支（换仓重试），不算错误
            if (converted == RenderAbstraction::GeometryAllocResult::Failed) {
                logRxFailure("rxGeometryAlloc", result);
            }
            return converted;
        }
        out = fromRenderXGeometryBlock(raw);
        return converted;
    }

    bool writeGeometry(RenderAbstraction::GeometryStoreHandle store, uint64_t blockId,
                       uint32_t byteOffset, uint32_t sizeBytes, const void* data) override {
        if (!RenderAbstraction::isValid(store)) {
            return false;
        }
        return checkRx("rxGeometryWrite", Render::RT::rxGeometryWrite(m_runtime,
            static_cast<Render::RT::GeometryStoreHandle>(store.value),
            blockId, byteOffset, sizeBytes, data));
    }

    void freeGeometry(RenderAbstraction::GeometryStoreHandle store, uint64_t blockId) override {
        if (!RenderAbstraction::isValid(store) || blockId == 0) {
            return;
        }
        Render::RT::rxGeometryFree(m_runtime,
            static_cast<Render::RT::GeometryStoreHandle>(store.value), blockId);
    }

    void flushGeometry(RenderAbstraction::GeometryStoreHandle store) override {
        if (!RenderAbstraction::isValid(store)) {
            return;
        }
        Render::RT::rxGeometryFlush(m_runtime,
            static_cast<Render::RT::GeometryStoreHandle>(store.value));
    }

    RenderAbstraction::GeometryStoreStats geometryStoreStats(
        RenderAbstraction::GeometryStoreHandle store) override {
        RenderAbstraction::GeometryStoreStats out{};
        if (!RenderAbstraction::isValid(store)) {
            return out;
        }
        Render::RT::GeometryStoreStats raw{};
        if (Render::RT::rxGeometryStoreGetStats(m_runtime,
                static_cast<Render::RT::GeometryStoreHandle>(store.value), &raw) != Render::RT::RxResult::Ok) {
            return out;
        }
        return fromRenderXGeometryStats(raw);
    }

    uint32_t defaultPipeline(RenderAbstraction::PipelineKind kind) override {
        return static_cast<uint32_t>(Render::RT::rxPipelineGetDefault(
            m_runtime, toRenderXDefaultPipeline(kind)));
    }

    uint32_t createPipeline(const RenderAbstraction::PipelineDesc& desc) override {
        const Render::RT::PipelineDesc raw = toRenderXPipelineDesc(desc);
        // rxPipelineCreate 返回 uint16 索引，0 表示失败
        const uint16_t index = Render::RT::rxPipelineCreate(m_runtime, &raw);
        if (index == 0) {
            SY_ERRORF("[RenderXAdapter] rxPipelineCreate failed (topology=%u format=%u fillMode=%u)",
                static_cast<unsigned>(raw.topology),
                static_cast<unsigned>(raw.vertexFormat),
                static_cast<unsigned>(raw.fillMode));
        }
        return static_cast<uint32_t>(index);
    }

    // ---------- 材质 ----------

    uint32_t createMaterial(const RenderAbstraction::MaterialDesc& desc) override {
        const Render::RT::MaterialDesc raw = toRenderXMaterial(desc);
        return static_cast<uint32_t>(Render::RT::rxMaterialAdd(m_runtime, &raw));
    }

    bool updateMaterial(uint32_t material, const RenderAbstraction::MaterialDesc& desc) override {
        const Render::RT::MaterialDesc raw = toRenderXMaterial(desc);
        return checkRx("rxMaterialUpdate",
            Render::RT::rxMaterialUpdate(m_runtime, static_cast<uint16_t>(material), &raw));
    }

    // ---------- 字体 ----------

    RenderAbstraction::FontHandle createFont(const RenderAbstraction::FontDesc& desc) override {
        const Render::RT::FontDesc raw = toRenderXFontDesc(desc);
        Render::RT::FontHandle font{};
        if (Render::RT::rxFontCreate(m_runtime, &raw, &font) != Render::RT::RxResult::Ok) {
            return {};
        }
        return RenderAbstraction::FontHandle{ static_cast<uint64_t>(font) };
    }

    void destroyFont(RenderAbstraction::FontHandle font) override {
        Render::RT::rxFontDestroy(m_runtime, static_cast<Render::RT::FontHandle>(font.value));
    }

    bool fontMetrics(RenderAbstraction::FontHandle font, RenderAbstraction::FontMetrics& out) override {
        Render::RT::FontMetrics raw{};
        if (Render::RT::rxFontMetrics(
                m_runtime, static_cast<Render::RT::FontHandle>(font.value), &raw) != Render::RT::RxResult::Ok) {
            return false;
        }
        out = fromRenderXFontMetrics(raw);
        return true;
    }

    bool fontGlyph(RenderAbstraction::FontHandle font, uint32_t codepoint,
                   RenderAbstraction::GlyphInfo& out) override {
        Render::RT::GlyphInfo raw{};
        if (Render::RT::rxFontGlyph(
                m_runtime, static_cast<Render::RT::FontHandle>(font.value), codepoint, &raw) != Render::RT::RxResult::Ok) {
            return false;
        }
        out = fromRenderXGlyphInfo(raw);
        return true;
    }

    bool flushFontAtlas(RenderAbstraction::FontHandle font) override {
        return Render::RT::rxFontFlushAtlas(m_runtime, static_cast<Render::RT::FontHandle>(font.value))
            == Render::RT::RxResult::Ok;
    }

    RenderAbstraction::TextureHandle fontAtlas(RenderAbstraction::FontHandle font) override {
        return RenderAbstraction::TextureHandle{
            static_cast<uint64_t>(Render::RT::rxFontAtlas(m_runtime, static_cast<Render::RT::FontHandle>(font.value))) };
    }

private:
    Render::RT::RuntimeHandle m_runtime;
    /// Runtime 级能力，构造时查一次即定。当前只用到 deviceName——
    /// 其余字段（线宽上限、纹理上限）不建议再往抽象层暴露，见 IRenderDevice 的说明
    Render::RT::Capabilities m_caps{};
    /// 后端名，指向 rxBackendName 的静态字符串，生命周期不随本对象
    const char* m_backendName = "";
    bool m_ownsRuntime = false;

    friend class RenderXFactory;
};

/// 表面句柄一律由宿主（RenderSessionHost）创建与销毁，本对象只借用
class RenderXSurfaceAdapter : public RenderAbstraction::IRenderSurface {
public:
    RenderXSurfaceAdapter(Render::RT::RuntimeHandle runtime, Render::RT::SurfaceHandle surface)
        : m_runtime(runtime), m_surface(surface) {}

    void resize(uint32_t width, uint32_t height) override {
        Render::RT::rxSurfaceResize(m_runtime, m_surface, width, height);
        // 交换链的实际尺寸要到 BeginFrame 才生效，但 width()/height() 报的是
        // 「刚请求的尺寸」——调用方用它算视口，等到 BeginFrame 就已经是新的了
        m_width = width;
        m_height = height;
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

/// 会话句柄一律由宿主（RenderSessionHost）创建与销毁，本对象只借用
class RenderXSceneAdapter : public RenderAbstraction::IRenderScene {
public:
    RenderXSceneAdapter(Render::RT::RuntimeHandle runtime, Render::RT::SessionHandle session)
        : m_runtime(runtime), m_session(session) {}

    DrawListHandle createDrawList(const DrawListDesc& desc) override {
        Render::RT::DrawListDesc raw = toRenderXDrawListDesc(desc);
        const Render::RT::DrawListHandle handle = Render::RT::rxDrawListCreate(m_runtime, &raw);
        if (!Render::RT::rxValid(handle)) {
            return {};
        }
        // RenderX 只认「调到哪个 upsert / submit」这一件事表达的判据，
        // 抽象层的类型标签因此由这里记着，供 upsert / submit 分派
        m_drawListTypes.emplace(static_cast<uint64_t>(handle), desc.type);
        return { static_cast<uint64_t>(handle) };
    }

    void destroyDrawList(DrawListHandle list) override {
        if (!RenderAbstraction::isValid(list)) {
            return;
        }
        m_drawListTypes.erase(list.value);
        Render::RT::rxDrawListDestroy(m_runtime, static_cast<Render::RT::DrawListHandle>(list.value));
    }

    void clearDrawList(DrawListHandle list) override {
        if (RenderAbstraction::isValid(list)) {
            Render::RT::rxDrawListClear(m_runtime, static_cast<Render::RT::DrawListHandle>(list.value));
        }
    }

    bool upsertDrawItem(DrawListHandle list, uint32_t slot, const DrawInstruction& command,
                        const Aabb3* bounds) override {
        if (!RenderAbstraction::isValid(list)) {
            return false;
        }
        const auto rawList = static_cast<Render::RT::DrawListHandle>(list.value);
        const Render::RT::DrawCommand raw = toRenderXDrawCommand(command);
        if (drawListType(list) == ViewVolumeType::Frustum3D) {
            Render::RT::RxAabb3 box{};
            if (bounds != nullptr) {
                box = toRenderXAabb3(*bounds);
            }
            return checkRx("rxDrawListUpsert3D",
                Render::RT::rxDrawListUpsert3D(m_runtime, rawList, slot, &raw,
                    bounds != nullptr ? &box : nullptr));
        }
        float rect[4] = {};
        if (bounds != nullptr) {
            toRenderXViewBounds(*bounds, rect);
        }
        return checkRx("rxDrawListUpsert",
            Render::RT::rxDrawListUpsert(m_runtime, rawList, slot, &raw,
                bounds != nullptr ? rect : nullptr));
    }

    bool removeDrawItem(DrawListHandle list, uint32_t slot) override {
        if (!RenderAbstraction::isValid(list)) {
            return false;
        }
        return checkRx("rxDrawListRemove", Render::RT::rxDrawListRemove(m_runtime,
            static_cast<Render::RT::DrawListHandle>(list.value), slot));
    }

    bool submitDrawList(DrawListHandle list, const ViewVolume* view) override {
        if (!RenderAbstraction::isValid(list)) {
            return false;
        }
        const auto rawList = static_cast<Render::RT::DrawListHandle>(list.value);
        const ViewVolumeType type = drawListType(list);
        // 没给可见范围、或判据与列表不匹配，都退化成整表全画：判据错配会误裁，
        // 多画只是慢，少画是错
        if (view == nullptr || view->type != type) {
            return type == ViewVolumeType::Frustum3D
                ? checkRx("rxSessionSubmitDrawList3D(no cull)",
                      Render::RT::rxSessionSubmitDrawList3D(m_session, rawList, nullptr))
                : checkRx("rxSessionSubmitDrawList(no cull)",
                      Render::RT::rxSessionSubmitDrawList(m_session, rawList, nullptr));
        }
        if (type == ViewVolumeType::Frustum3D) {
            const Render::RT::RxFrustum frustum = toRenderXFrustum(view->frustum);
            return checkRx("rxSessionSubmitDrawList3D",
                Render::RT::rxSessionSubmitDrawList3D(m_session, rawList, &frustum));
        }
        return checkRx("rxSessionSubmitDrawList",
            Render::RT::rxSessionSubmitDrawList(m_session, rawList, view->rect));
    }

    bool beginFrame() override {
        const Render::RT::RxResult result = Render::RT::rxSessionBeginFrame(m_session);
        if (result == Render::RT::RxResult::Ok) {
            return true;
        }
        // ErrorSurfaceOutOfDate 是调用方要处理的重试路径，不算错误，不打日志
        if (result != Render::RT::RxResult::ErrorSurfaceOutOfDate) {
            logRxFailure("rxSessionBeginFrame", result);
        }
        return false;
    }

    void endFrame() override {
        Render::RT::rxSessionEndFrame(m_session);
    }

    TextureHandle createRenderTarget(uint32_t width, uint32_t height) override {
        Render::RT::RenderTargetDesc rtDesc{};
        rtDesc.width = width;
        rtDesc.height = height;
        rtDesc.usage = Render::RT::TextureUsageFlag::ColorAttachment | Render::RT::TextureUsageFlag::TransferSrc;
        rtDesc._pad0 = 0;
        return { static_cast<uint64_t>(Render::RT::rxTextureCreateRenderTarget(m_runtime, &rtDesc)) };
    }

    bool readPixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    void* outPixels, uint64_t capacity) override {
        return checkRx("rxSessionReadPixels", Render::RT::rxSessionReadPixels(
            m_session, x, y, width, height, outPixels, capacity));
    }

    bool setRenderTarget(const RenderAbstraction::RenderTargetBinding& target) override {
        return checkRx("rxSessionSetRenderTarget", Render::RT::rxSessionSetRenderTarget(m_session,
            static_cast<Render::RT::TextureHandle>(target.color.value),
            static_cast<Render::RT::TextureHandle>(target.depth.value),
            target.width,
            target.height));
    }

    bool readPixelsFromTexture(TextureHandle texture, uint32_t x, uint32_t y, uint32_t width,
                               uint32_t height, void* outPixels, uint64_t capacity) override {
        return checkRx("rxSessionReadPixelsFromTexture", Render::RT::rxSessionReadPixelsFromTexture(m_session,
            static_cast<Render::RT::TextureHandle>(texture.value),
            x, y, width, height, outPixels, capacity));
    }

    void setCamera(const RenderAbstraction::CameraDesc& camera) override {
        float viewMatrix[16];
        toRenderXViewMatrix(camera.viewMatrix, viewMatrix);
        Render::RT::rxSessionSetViewMatrix(m_session, viewMatrix);
    }

    bool allocTransient(uint64_t sizeBytes, TransientAlloc& out) override {
        Render::RT::TransientAlloc raw{};
        if (Render::RT::rxSessionAllocTransient(m_session, sizeBytes, &raw) != Render::RT::RxResult::Ok) {
            return false;
        }
        // RenderX 的分配结果 → 抽象层 POD（句柄降为包装结构）
        out.buffer = { static_cast<uint64_t>(raw.buffer) };
        out.cpuPtr = static_cast<uint8_t*>(raw.cpuPtr);
        out.offset = raw.offset;
        out.sizeBytes = raw.sizeBytes;
        return true;
    }

    bool submit(const RenderAbstraction::DrawPacket& packet) override {
        if (packet.commands == nullptr || packet.commandCount == 0) {
            return true;
        }
        // DrawInstruction → DrawCommand 逐条转换。scratch 是成员，跨帧复用容量，
        // 避免每帧多次提交都做一次堆分配。
        m_commandScratch.clear();
        m_commandScratch.reserve(packet.commandCount);
        for (uint32_t i = 0; i < packet.commandCount; ++i) {
            m_commandScratch.push_back(toRenderXDrawCommand(packet.commands[i]));
        }

        Render::RT::DrawPacket raw{};
        raw.commands = m_commandScratch.data();
        raw.commandCount = packet.commandCount;
        raw.enableCulling = packet.enableCulling ? 1 : 0;
        // RenderX 的约定是「viewMatrix 全零 = 沿用 Session 已设矩阵」，
        // 抽象层用 hasViewMatrix 显式表达，这里只在需要覆盖时才拷贝。
        if (packet.hasViewMatrix) {
            std::memcpy(raw.viewMatrix, packet.viewMatrix, sizeof(raw.viewMatrix));
        }
        std::memcpy(raw.viewport, packet.viewport, sizeof(raw.viewport));
        raw.frameId = 0;

        return checkRx("rxSessionSubmit", Render::RT::rxSessionSubmit(m_session, &raw));
    }

    void setModelMatrix(const RenderAbstraction::Matrix4x4* matrix) override {
        Render::RT::rxSessionSetModelMatrix(m_session, matrix != nullptr ? matrix->m[0] : nullptr);
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
    /// 取列表在创建时定下的可见范围类型。句柄不在表里（不该发生）时按 2D 处理
    ViewVolumeType drawListType(DrawListHandle list) const {
        const auto it = m_drawListTypes.find(list.value);
        return it != m_drawListTypes.end() ? it->second : ViewVolumeType::Rect2D;
    }

    Render::RT::RuntimeHandle m_runtime;
    Render::RT::SessionHandle m_session;
    /// submit 的转换暂存（DrawInstruction → DrawCommand），跨帧复用容量
    std::vector<Render::RT::DrawCommand> m_commandScratch;
    /// 绘制列表句柄裸值 → 可见范围类型
    std::unordered_map<uint64_t, ViewVolumeType> m_drawListTypes;
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
        // 工厂建的 Runtime 归设备对象所有：它随设备析构一起销毁
        return std::make_unique<RenderXDeviceAdapter>(runtime, /*ownsRuntime=*/true);
    }

};

std::unique_ptr<RenderAbstraction::IRenderFactory> RenderXAdapter::createFactory() {
    return std::make_unique<RenderXFactory>();
}

// ---------- 句柄已由宿主建好时的包装入口（见 RenderXAdapterInternal.h）----------
//
// 适配器类定义在本文件内，因此这几个入口也放在这里：它们的构造函数只存句柄，
// 不承担任何资源所有权，销毁仍归建句柄的那一方。

std::unique_ptr<RenderAbstraction::IRenderDevice> wrapRenderXDevice(uint64_t runtime) {
    const auto handle = static_cast<Render::RT::RuntimeHandle>(runtime);
    if (!Render::RT::rxValid(handle)) {
        return nullptr;
    }
    return std::make_unique<RenderXDeviceAdapter>(handle, /*ownsRuntime=*/false);
}

std::unique_ptr<RenderAbstraction::IRenderSurface> wrapRenderXSurface(uint64_t runtime, uint64_t surface) {
    const auto rt = static_cast<Render::RT::RuntimeHandle>(runtime);
    const auto sf = static_cast<Render::RT::SurfaceHandle>(surface);
    if (!Render::RT::rxValid(rt) || !Render::RT::rxValid(sf)) {
        return nullptr;
    }
    return std::make_unique<RenderXSurfaceAdapter>(rt, sf);
}

std::unique_ptr<RenderAbstraction::IRenderScene> wrapRenderXScene(uint64_t runtime, uint64_t session) {
    const auto rt = static_cast<Render::RT::RuntimeHandle>(runtime);
    const auto se = static_cast<Render::RT::SessionHandle>(session);
    if (!Render::RT::rxValid(rt) || !Render::RT::rxValid(se)) {
        return nullptr;
    }
    return std::make_unique<RenderXSceneAdapter>(rt, se);
}

} // namespace RenderBridge
