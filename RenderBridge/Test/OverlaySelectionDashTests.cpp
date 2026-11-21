/**
 * @file OverlaySelectionDashTests.cpp
 * @brief 选中流水虚线的覆盖层行为测试（视口裁剪 / 高对比色 / 图层抑制）
 *
 * 覆盖层每帧把轮廓折线切成虚线再写进瞬态环。这里用一个记录型
 * IRenderScene 直接观察 submit 产物，验证与视口、颜色、抑制相关的三条
 * 用户可见行为；不依赖 GPU，也不需要 Qt 控件。
 */

#include <gtest/gtest.h>

#include "RenderBridge/OverlayScene.h"
#include "RenderBridge/RenderSessionHost.h"
#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    namespace RA = RenderAbstraction;

    /// 记录型假场景：所有瞬态分配都指向同一块缓冲，足够观察一次轮廓提交
    class OverlayDashRecordingScene final : public RA::IRenderScene
    {
    public:
        // 放堆上：1MB 的栈对象在 Debug 下会直接触发栈溢出
        std::vector<uint8_t> buffer = std::vector<uint8_t>(1 << 20, 0);

        bool allocTransient(uint64_t sizeBytes, RA::TransientAlloc& out) override
        {
            if (sizeBytes > buffer.size())
            {
                return false;
            }
            out.cpuPtr = buffer.data();
            out.buffer.value = 1;
            out.offset = 0;
            return true;
        }
        bool submit(const RA::DrawPacket&) override
        {
            return false;
        }
        void setModelMatrix(const RA::Matrix4x4*) override {}
        RA::DrawListHandle createDrawList(const RA::DrawListDesc&) override
        {
            RA::DrawListHandle handle;
            handle.value = 1;
            return handle;
        }
        void destroyDrawList(RA::DrawListHandle) override {}
        void clearDrawList(RA::DrawListHandle) override {}
        bool upsertDrawItem(RA::DrawListHandle, uint32_t, const RA::DrawInstruction&, const RA::Aabb3*) override
        {
            return true;
        }
        bool removeDrawItem(RA::DrawListHandle, uint32_t) override
        {
            return true;
        }
        bool submitDrawList(RA::DrawListHandle, const RA::ViewVolume*) override
        {
            return false;
        }
        bool beginFrame() override
        {
            return true;
        }
        void endFrame() override {}
        RA::TextureHandle createRenderTarget(uint32_t, uint32_t) override
        {
            return {};
        }
        bool readPixels(uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }
        bool setRenderTarget(const RA::RenderTargetBinding&) override
        {
            return false;
        }
        bool readPixelsFromTexture(RA::TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }
        void setCamera(const RA::CameraDesc&) override {}
        void setLighting(const RA::LightingDesc&) override {}
        RA::FrameStatistics getFrameStatistics() const override
        {
            return {};
        }
        bool isValid() const override
        {
            return true;
        }
    };

    /// P3C4 顶点在测试侧展开成逐 float 读取（位置 xyz + rgba）
    constexpr size_t kVertexFloats = 7;

    RenderBridge::SelectionOutlinePath makeVerticalPath(float x, float y0, float y1, uint64_t entityId = 7)
    {
        RenderBridge::SelectionOutlinePath path;
        path.color = Ut::Color(0.0f, 0.0f, 0.0f, 1.0f);
        path.entityId = entityId;
        RenderBridge::SelectionOutlineVertex a{};
        a.position = Ut::Vec2f(x, y0);
        a.arcLength = 0.0f;
        RenderBridge::SelectionOutlineVertex b{};
        b.position = Ut::Vec2f(x, y1);
        b.arcLength = y1 - y0;
        path.vertices.push_back(a);
        path.vertices.push_back(b);
        path.bounds = Ut::BBox2d(x, y0, x, y1);
        return path;
    }

    RenderBridge::SelectionDashStyle makeDashStyle()
    {
        RenderBridge::SelectionDashStyle style;
        style.enabled = true;
        style.animated = true;
        style.dashLengthPx = 10.0f;
        style.gapLengthPx = 8.0f;
        style.speedPxPerSec = 48.0f;
        return style;
    }
}  // namespace

// 视口外的路径必须在 AABB 阶段就被跳过，不进弧长切分
TEST(OverlaySelectionDashTest, PathsOutsideViewportAreCulled)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    paths.push_back(makeVerticalPath(1000.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 12.0f;
    params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 20.0, 20.0);
    params.originalVisible = true;
    overlay.setFrameParams(params);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    const float* vertices = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < out[0].vertexCount; ++i)
    {
        EXPECT_NEAR(vertices[i * kVertexFloats + 0], 0.0f, 1e-4f);
    }
}

// 视口只覆盖折线中段时必须按弧长裁剪：输出点不允许跑到视口外
TEST(OverlaySelectionDashTest, ArcRangeClippingKeepsVerticesInsideViewport)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, -50.0f, 50.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d(-10.0, -10.0, 10.0, 10.0);
    params.originalVisible = true;
    overlay.setFrameParams(params);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    const float* vertices = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < out[0].vertexCount; ++i)
    {
        EXPECT_GE(vertices[i * kVertexFloats + 1], -10.5f);
        EXPECT_LE(vertices[i * kVertexFloats + 1], 10.5f);
    }
}

// 本体可见时虚线必须换成高对比色：黑色图元应得到近白虚线
TEST(OverlaySelectionDashTest, DashUsesHighContrastColorWhenOriginalIsVisible)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 20.0, 20.0);
    params.originalVisible = true;
    overlay.setFrameParams(params);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    ASSERT_GT(out[0].vertexCount, 0u);
    const float* vertices = reinterpret_cast<const float*>(scene.buffer.data());
    EXPECT_GT(vertices[3], 0.85f);
    EXPECT_GT(vertices[4], 0.85f);
    EXPECT_GT(vertices[5], 0.85f);
}

// 跨图层选择：只抑制隐藏图层的图元，可见图层的虚线继续画
TEST(OverlaySelectionDashTest, SuppressionIsPerEntity)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f, 7));
    paths.push_back(makeVerticalPath(50.0f, 0.0f, 40.0f, 8));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 80.0, 80.0);
    params.originalVisible = true;
    overlay.setFrameParams(params);

    overlay.setSelectionOutlinesSuppressedEntities({ 7 });
    EXPECT_TRUE(overlay.hasAnimatedSelectionOutlines());

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    const float* vertices = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < out[0].vertexCount; ++i)
    {
        // 被隐藏图元的 x=0 路径不应出现在输出里
        EXPECT_NEAR(vertices[i * kVertexFloats + 0], 50.0f, 1e-4f);
    }
}

// 图层隐藏时抑制只停绘制与动画，路径保留，恢复后立即可见
TEST(OverlaySelectionDashTest, SuppressionStopsDrawingButKeepsPaths)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 20.0, 20.0);
    params.originalVisible = true;
    overlay.setFrameParams(params);

    ASSERT_TRUE(overlay.hasAnimatedSelectionOutlines());

    overlay.setSelectionOutlinesSuppressedEntities({ 7 });
    EXPECT_FALSE(overlay.hasAnimatedSelectionOutlines());
    EXPECT_FALSE(overlay.hasLayer(RenderBridge::OverlayLayerId::SelectionOutlines));
    std::vector<RA::DrawInstruction> suppressed;
    overlay.submit(scene, suppressed);
    EXPECT_TRUE(suppressed.empty());

    overlay.setSelectionOutlinesSuppressedEntities({});
    EXPECT_TRUE(overlay.hasAnimatedSelectionOutlines());
    std::vector<RA::DrawInstruction> resumed;
    overlay.submit(scene, resumed);
    EXPECT_EQ(resumed.size(), 1u);
}

// ==================== B2：虚线交给 GPU ====================
//
// 对拍口径：同一条路径、相同的 dash/gap/相位下，
//   CPU 路径 = appendSelectionDash 输出的线段区间并集；
//   GPU 路径 = 片元公式 mod(arc - phase, period) < dash 的覆盖集合。
// 两者必须是同一个集合。GPU 只丢弃片元、不做几何切段，因此这里用 UV.x
// （全局弧长）做数学比对，不需要真正的光栅化。

namespace
{
    /// P3T2C4 顶点在测试侧按 float 平铺：位置 xyz + uv + rgba
    constexpr size_t kGpuVertexFloats = 9;

    /// 片元公式：rel = mod(arc - phase, period)，落在 [0, dash) 即为实线
    bool dashOnAt(double arc, double phase, double dash, double gap)
    {
        const double period = dash + gap;
        if (period <= 1e-9)
        {
            return true;
        }
        double rel = std::fmod(arc - phase, period);
        if (rel < 0.0)
        {
            rel += period;
        }
        return rel < dash;
    }

    /// 采样点离 dash/gap 边界是否过近：边界两侧的浮点判断允许有差异，
    /// 逐点比对时跳过这些点，避免把实现细节差异误判成集合不一致
    bool nearDashBoundary(double arc, double phase, double dash, double gap)
    {
        const double period = dash + gap;
        if (period <= 1e-9)
        {
            return false;
        }
        double rel = std::fmod(arc - phase, period);
        if (rel < 0.0)
        {
            rel += period;
        }
        const double toCycleStart = std::min(rel, period - rel);
        const double toDashEnd = std::fabs(rel - dash);
        return std::min(toCycleStart, toDashEnd) < 0.08;
    }

    /// CPU 路径的线段顶点是纯位置（P3C4），按两两配对取出纵坐标区间；
    /// 测试路径是竖直线，纵坐标即弧长
    std::vector<std::pair<double, double>> collectCpuArcIntervals(
        const OverlayDashRecordingScene& scene, const RA::DrawInstruction& cmd)
    {
        const float* f = reinterpret_cast<const float*>(scene.buffer.data());
        std::vector<std::pair<double, double>> segs;
        for (uint32_t i = 0; i + 1 < cmd.vertexCount; i += 2)
        {
            segs.emplace_back(f[i * kVertexFloats + 1], f[(i + 1) * kVertexFloats + 1]);
        }
        return segs;
    }

    /// B2 用例共用：初始化 Null 后端（只要 device，场景用记录型假场景）
    bool setupNullDevice(RenderBridge::RenderSessionHost& host, const char* name)
    {
        RenderBridge::RenderSessionHost::Config cfg;
        cfg.applicationName = name;
        cfg.width = 320;
        cfg.height = 240;
        cfg.backend = RA::RenderBackend::Null;
        cfg.transientBufferBytes = 8ull * 1024 * 1024;
        return host.initialize(cfg);
    }

    /// 不提供 WorldDash 管线的设备桩：只用于验证能力探测为零时的回落分支。
    /// 其余接口都不会被虚线路径调用，返回空实现即可。
    class NoDashSupportDevice final : public RA::IRenderDevice
    {
    public:
        const char* backendName() const override { return "Stub"; }
        const char* deviceName() const override { return "NoDash"; }

        RA::TextureHandle createTexture(const RA::TextureDesc&) override { return {}; }
        void destroyTexture(RA::TextureHandle) override {}
        void updateTexture(RA::TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*) override {}

        RA::GeometryStoreHandle createGeometryStore(const RA::GeometryStoreDesc&) override { return {}; }
        void destroyGeometryStore(RA::GeometryStoreHandle) override {}
        RA::GeometryAllocResult allocGeometry(RA::GeometryStoreHandle, uint64_t, RA::GeometryBlock&) override
        {
            return {};
        }
        bool writeGeometry(RA::GeometryStoreHandle, uint64_t, uint32_t, uint32_t, const void*) override
        {
            return false;
        }
        void freeGeometry(RA::GeometryStoreHandle, uint64_t) override {}
        void flushGeometry(RA::GeometryStoreHandle) override {}
        RA::GeometryStoreStats geometryStoreStats(RA::GeometryStoreHandle) override { return {}; }

        /// 关键：声明本后端没有 WorldDash
        uint32_t defaultPipeline(RA::PipelineKind kind) override
        {
            return kind == RA::PipelineKind::WorldDash ? 0u : 1u;
        }
        uint32_t createPipeline(const RA::PipelineDesc&) override { return 0; }

        uint32_t createMaterial(const RA::MaterialDesc&) override { return 0; }
        bool updateMaterial(uint32_t, const RA::MaterialDesc&) override { return false; }
        bool destroyMaterial(uint32_t) override { return false; }

        RA::FontHandle createFont(const RA::FontDesc&) override { return {}; }
        void destroyFont(RA::FontHandle) override {}
        bool fontMetrics(RA::FontHandle, RA::FontMetrics&) override { return false; }
        bool fontGlyph(RA::FontHandle, uint32_t, RA::GlyphInfo&) override { return false; }
        bool flushFontAtlas(RA::FontHandle) override { return false; }
        RA::TextureHandle fontAtlas(RA::FontHandle) override { return {}; }
    };
}  // namespace

// 对拍：CPU 覆盖区间并集与 GPU 片元公式在同一相位下完全一致
TEST(OverlaySelectionDashTest, CpuIntervalsMatchGpuFragmentFormula)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());  // dash 10 / gap 8

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    // 相位 12 大于 gap 8：路径开头 [0, 4) 仍是实线，不能因相位跳过
    params.dashOffsetPx = 12.0f;
    params.visibleWorld = Ut::BBox2d{};  // 无效盒 = 不裁剪，全弧长参与
    params.originalVisible = false;
    overlay.setFrameParams(params);
    overlay.setSelectionDashGpuEnabled(false);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].topology, RA::PrimitiveType::Lines);
    EXPECT_FALSE(overlay.selectionDashGpuActive());

    constexpr double kDash = 10.0;
    constexpr double kGap = 8.0;
    constexpr double kPhase = 12.0;
    constexpr double kTotal = 40.0;

    const auto segs = collectCpuArcIntervals(scene, out[0]);
    ASSERT_FALSE(segs.empty());
    for (const auto& seg : segs)
    {
        EXPECT_GT(seg.second, seg.first);
        // 每段必须整段落在实线区间内
        EXPECT_TRUE(dashOnAt((seg.first + seg.second) * 0.5, kPhase, kDash, kGap));
    }
    // 相位大于 gap 时路径开头不得丢失：第一段必须从弧长 0 起
    EXPECT_LE(segs.front().first, 1e-4);
    EXPECT_NEAR(segs.front().second, 4.0, 1e-3);

    // 逐点比对覆盖集合
    int coveredSamples = 0;
    for (double arc = 0.0; arc <= kTotal; arc += 0.02)
    {
        if (nearDashBoundary(arc, kPhase, kDash, kGap))
        {
            continue;
        }
        const bool expected = dashOnAt(arc, kPhase, kDash, kGap);
        bool actual = false;
        for (const auto& seg : segs)
        {
            if (arc >= seg.first - 1e-4 && arc <= seg.second + 1e-4)
            {
                actual = true;
                break;
            }
        }
        ASSERT_EQ(expected, actual) << "arc=" << arc;
        if (expected)
        {
            ++coveredSamples;
        }
    }
    EXPECT_GT(coveredSamples, 100);
}

// GPU 路径：裁剪后的弧长必须沿用全局值，否则视口边缘会重新起相位
TEST(OverlaySelectionDashTest, GpuClipKeepsGlobalArcLength)
{
    RenderBridge::RenderSessionHost host;
    ASSERT_TRUE(setupNullDevice(host, "OverlayDashGpuClip"));
    RA::IRenderDevice* device = host.device();
    ASSERT_NE(device, nullptr);
    ASSERT_NE(device->defaultPipeline(RA::PipelineKind::WorldDash), 0u);

    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    // 弧长参数化：y=-50 对应 arc=0，y=+50 对应 arc=100
    paths.push_back(makeVerticalPath(0.0f, -50.0f, 50.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 12.0f;
    params.visibleWorld = Ut::BBox2d(-10.0, -10.0, 10.0, 10.0);
    params.originalVisible = false;
    overlay.setFrameParams(params);

    std::vector<RA::DrawInstruction> out;
    ASSERT_TRUE(scene.beginFrame());
    overlay.submit(scene, *device, out);
    scene.endFrame();

    ASSERT_TRUE(overlay.selectionDashGpuActive());
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].topology, RA::PrimitiveType::Triangles);
    EXPECT_EQ(out[0].format, RA::VertexFormat::WorldPosUVColor);
    EXPECT_EQ(out[0].space, RA::RenderSpace::World);
    EXPECT_NE(out[0].pipelineIndex, 0u);
    EXPECT_NE(out[0].materialIndex, 0u);
    // 单段折线被裁剪成一个四边形（无索引缓冲 → 6 个顶点）
    ASSERT_EQ(out[0].vertexCount, 6u);

    const float* v = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < out[0].vertexCount; ++i)
    {
        const float* p = v + i * kGpuVertexFloats;
        for (size_t k = 0; k < kGpuVertexFloats; ++k)
        {
            ASSERT_TRUE(std::isfinite(p[k]));
        }
        // 竖直线宽 1 逻辑像素、DPR=1、p2w=1 → 半宽 0.5 世界单位
        EXPECT_NEAR(std::fabs(p[0]), 0.5f, 1e-3f);
        EXPECT_TRUE(std::fabs(p[1] + 10.0f) < 1e-3f || std::fabs(p[1] - 10.0f) < 1e-3f);
        // 关键：UV.x 是全局弧长（40 / 60），绝不允许被重基成 0 / 20
        EXPECT_TRUE(std::fabs(p[3] - 40.0f) < 1e-3f || std::fabs(p[3] - 60.0f) < 1e-3f);
        EXPECT_TRUE(std::fabs(std::fabs(p[4]) - 1.0f) < 1e-3f);
    }
}

// GPU 路径必须滤掉重复点/零长段：法线不能出现 NaN
TEST(OverlaySelectionDashTest, GpuSkipsZeroLengthSegments)
{
    RenderBridge::RenderSessionHost host;
    ASSERT_TRUE(setupNullDevice(host, "OverlayDashGpuZeroLen"));
    RA::IRenderDevice* device = host.device();
    ASSERT_NE(device, nullptr);

    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    RenderBridge::SelectionOutlinePath path;
    path.color = Ut::Color(0.0f, 0.0f, 0.0f, 1.0f);
    path.entityId = 9;
    RenderBridge::SelectionOutlineVertex a{};
    a.position = Ut::Vec2f(0.0f, 0.0f);
    a.arcLength = 0.0f;
    RenderBridge::SelectionOutlineVertex duplicate{};
    duplicate.position = Ut::Vec2f(0.0f, 0.0f);  // 与 a 重复
    duplicate.arcLength = 0.0f;
    RenderBridge::SelectionOutlineVertex b{};
    b.position = Ut::Vec2f(0.0f, 20.0f);
    b.arcLength = 20.0f;
    path.vertices.push_back(a);
    path.vertices.push_back(duplicate);
    path.vertices.push_back(b);
    path.bounds = Ut::BBox2d(0.0, 0.0, 0.0, 20.0);

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(path);
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d{};  // 不裁剪
    params.originalVisible = false;
    overlay.setFrameParams(params);

    std::vector<RA::DrawInstruction> out;
    ASSERT_TRUE(scene.beginFrame());
    overlay.submit(scene, *device, out);
    scene.endFrame();

    ASSERT_TRUE(overlay.selectionDashGpuActive());
    ASSERT_EQ(out.size(), 1u);
    // 零长段被跳过，只剩 20 单位那一段 → 一个四边形
    ASSERT_EQ(out[0].vertexCount, 6u);
    const float* v = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < out[0].vertexCount * kGpuVertexFloats; ++i)
    {
        ASSERT_TRUE(std::isfinite(v[i]));
    }
}

// 材质必须独占：两个实例各自持有不同的材质编号，逐帧 updateMaterial 都被接受
TEST(OverlaySelectionDashTest, GpuMaterialsAreExclusivePerScene)
{
    RenderBridge::RenderSessionHost host;
    ASSERT_TRUE(setupNullDevice(host, "OverlayDashGpuMaterial"));
    RA::IRenderDevice* device = host.device();
    ASSERT_NE(device, nullptr);

    OverlayDashRecordingScene sceneA;
    OverlayDashRecordingScene sceneB;
    RenderBridge::OverlayScene overlayA;
    RenderBridge::OverlayScene overlayB;

    for (RenderBridge::OverlayScene* overlay : { &overlayA, &overlayB })
    {
        overlay->setDevicePixelRatio(1.0f);
        overlay->setSelectionDashStyle(makeDashStyle());
        std::vector<RenderBridge::SelectionOutlinePath> paths;
        paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
        overlay->setSelectionOutlines(std::move(paths));
        RenderBridge::OverlayFrameParams params;
        params.pixelToWorld = 1.0f;
        params.dashOffsetPx = 0.0f;
        params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 20.0, 20.0);
        params.originalVisible = false;
        overlay->setFrameParams(params);
    }

    std::vector<RA::DrawInstruction> outA;
    std::vector<RA::DrawInstruction> outB;
    // 连跑两帧：第二次必须仍能 updateMaterial（独占材质）
    for (int frame = 0; frame < 2; ++frame)
    {
        outA.clear();
        outB.clear();
        ASSERT_TRUE(sceneA.beginFrame());
        overlayA.submit(sceneA, *device, outA);
        sceneA.endFrame();
        ASSERT_TRUE(sceneB.beginFrame());
        overlayB.submit(sceneB, *device, outB);
        sceneB.endFrame();

        ASSERT_TRUE(overlayA.selectionDashGpuActive());
        ASSERT_TRUE(overlayB.selectionDashGpuActive());
        ASSERT_EQ(outA.size(), 1u);
        ASSERT_EQ(outB.size(), 1u);
        EXPECT_NE(outA[0].materialIndex, 0u);
        EXPECT_NE(outB[0].materialIndex, 0u);
        // 唯一 flags 让两份材质不会被全字段去重合并
        EXPECT_NE(outA[0].materialIndex, outB[0].materialIndex);
    }

    overlayA.releaseGpuResources(*device);
    overlayB.releaseGpuResources(*device);
}

// 强制回落开关：关闭 GPU 后必须回到 CPU 切段，且共用同一份过滤结果
TEST(OverlaySelectionDashTest, GpuSwitchFallsBackToCpuSegments)
{
    RenderBridge::RenderSessionHost host;
    ASSERT_TRUE(setupNullDevice(host, "OverlayDashGpuFallback"));
    RA::IRenderDevice* device = host.device();
    ASSERT_NE(device, nullptr);

    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f, 7));
    paths.push_back(makeVerticalPath(50.0f, 0.0f, 40.0f, 8));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d(-20.0, -20.0, 80.0, 80.0);
    params.originalVisible = false;
    overlay.setFrameParams(params);
    overlay.setSelectionOutlinesSuppressedEntities({ 7 });

    std::vector<RA::DrawInstruction> gpuOut;
    ASSERT_TRUE(scene.beginFrame());
    overlay.submit(scene, *device, gpuOut);
    scene.endFrame();
    ASSERT_TRUE(overlay.selectionDashGpuActive());
    ASSERT_EQ(gpuOut.size(), 1u);
    // 抑制的图元在 GPU 路径里同样不参与几何
    const float* gv = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < gpuOut[0].vertexCount; ++i)
    {
        // 带子有 ±半宽的偏移，中心线在 x=50
        EXPECT_NEAR(gv[i * kGpuVertexFloats + 0], 50.0f, 0.6f);
    }

    overlay.setSelectionDashGpuEnabled(false);
    std::vector<RA::DrawInstruction> cpuOut;
    overlay.submit(scene, cpuOut);
    EXPECT_FALSE(overlay.selectionDashGpuActive());
    ASSERT_EQ(cpuOut.size(), 1u);
    EXPECT_EQ(cpuOut[0].topology, RA::PrimitiveType::Lines);
    // 与第一段的抑制行为一致：只输出可见图元的虚线
    const float* cv = reinterpret_cast<const float*>(scene.buffer.data());
    for (uint32_t i = 0; i < cpuOut[0].vertexCount; ++i)
    {
        EXPECT_NEAR(cv[i * kVertexFloats + 0], 50.0f, 1e-3f);
    }
}

// 能力探测为零：后端没有 WorldDash 管线时必须自动回落 CPU 切段
TEST(OverlaySelectionDashTest, MissingWorldDashPipelineFallsBackToCpu)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    params.pixelToWorld = 1.0f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d{};
    params.originalVisible = false;
    overlay.setFrameParams(params);

    NoDashSupportDevice device;
    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, device, out);

    EXPECT_FALSE(overlay.selectionDashGpuActive());
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].topology, RA::PrimitiveType::Lines);
    // 回落路径不指名管线/材质，交给后端按 (格式, 空间, 拓扑) 解析
    EXPECT_EQ(out[0].pipelineIndex, 0u);
    EXPECT_EQ(out[0].materialIndex, 0u);
    EXPECT_GT(out[0].vertexCount, 0u);
}

// ==================== 第 4 项：CPU 回落总点数额度 ====================
//
// 口径：额度充足时整个选中集的虚线必须完整；额度被人为调小时靠前路径完整、
// 靠后路径被削平，但**不允许**出现 allocTransient 失败导致整批丢失。
// 测试全部走 CPU 路径（关闭 GPU 开关），且不裁剪视口，便于按 x 坐标分辨路径。

namespace
{
    /// 每条竖线的虚线点数：弧长 40、dash 10 / gap 8、相位 0 →
    /// 实线区间 [0,10) [18,28) [36,40)，每段两端各一点，共 6 点
    constexpr uint32_t kPointsPerPath = 6;

    /// z 层忽略，按 x 统计某条路径实际写出了多少个点
    uint32_t countVerticesAtX(const OverlayDashRecordingScene& scene, const RA::DrawInstruction& cmd, float x)
    {
        const float* f = reinterpret_cast<const float*>(scene.buffer.data());
        uint32_t n = 0;
        for (uint32_t i = 0; i < cmd.vertexCount; ++i)
        {
            if (std::fabs(f[i * kVertexFloats + 0] - x) < 1e-4f)
            {
                ++n;
            }
        }
        return n;
    }

    /// 20 条等间距竖线，每条弧长 40：整批需要 20 × 6 = 120 点
    std::vector<RenderBridge::SelectionOutlinePath> makeManyPaths(size_t count)
    {
        std::vector<RenderBridge::SelectionOutlinePath> paths;
        paths.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            paths.push_back(makeVerticalPath(static_cast<float>(i) * 25.0f, 0.0f, 40.0f, i + 1));
        }
        return paths;
    }

    void setupCpuDash(RenderBridge::OverlayScene& overlay, size_t pathCount)
    {
        overlay.setDevicePixelRatio(1.0f);
        overlay.setSelectionDashStyle(makeDashStyle());
        overlay.setSelectionOutlines(makeManyPaths(pathCount));
        RenderBridge::OverlayFrameParams params;
        params.pixelToWorld = 1.0f;
        params.dashOffsetPx = 0.0f;
        params.visibleWorld = Ut::BBox2d{};  // 不裁剪：全部路径参与
        params.originalVisible = false;
        overlay.setFrameParams(params);
        overlay.setSelectionDashGpuEnabled(false);  // 强制 CPU 回落路径
    }
}  // namespace

// 额度充足：所有路径都完整，统计口径与输出点数一致
TEST(OverlaySelectionDashTest, CpuQuotaKeepsAllPathsWhenBudgetSufficient)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    setupCpuDash(overlay, 20);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_FALSE(overlay.selectionDashGpuActive());
    EXPECT_EQ(out[0].vertexCount, 20u * kPointsPerPath);

    const auto stats = overlay.selectionDashCpuStats();
    EXPECT_EQ(stats.quota, RenderBridge::kSelectionDashCpuPointQuota);
    EXPECT_EQ(stats.usedPoints, out[0].vertexCount);
    EXPECT_EQ(stats.truncatedPaths, 0u);
    // 预留上界不超过额度，也不少于实际用掉的点数
    EXPECT_LE(stats.reservedPoints, stats.quota);
    EXPECT_GE(stats.reservedPoints, stats.usedPoints);

    // 20 条路径一条不少
    for (size_t i = 0; i < 20; ++i)
    {
        EXPECT_EQ(countVerticesAtX(scene, out[0], static_cast<float>(i) * 25.0f), kPointsPerPath)
            << "path " << i;
    }
}

// 额度调小：靠前路径完整、中间一条被削平、靠后整条跳过，整批仍然提交
TEST(OverlaySelectionDashTest, CpuQuotaTruncatesLaterPathsWithoutDroppingBatch)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    setupCpuDash(overlay, 20);
    // 28 点 = 4 条完整（24）+ 第 5 条只画前两个实线区间（4）
    overlay.setSelectionDashCpuPointQuota(28);

    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    // 关键：不是「一条都不画」，批次照常提交
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].topology, RA::PrimitiveType::Lines);
    EXPECT_EQ(out[0].vertexCount, 28u);

    const auto stats = overlay.selectionDashCpuStats();
    EXPECT_EQ(stats.quota, 28u);
    EXPECT_EQ(stats.usedPoints, 28u);
    EXPECT_LE(stats.usedPoints, stats.quota);
    EXPECT_LE(stats.reservedPoints, stats.quota);
    // 4 条完整 + 第 5 条被削平 + 其后 15 条整条跳过 = 16 条被截断
    EXPECT_EQ(stats.truncatedPaths, 16u);

    for (size_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(countVerticesAtX(scene, out[0], static_cast<float>(i) * 25.0f), kPointsPerPath)
            << "path " << i << " should be complete";
    }
    // 第 5 条只剩 [0,10) [18,28) 两段
    EXPECT_EQ(countVerticesAtX(scene, out[0], 100.0f), 4u);
    // 第 6 条及之后完全不再画
    for (size_t i = 5; i < 20; ++i)
    {
        EXPECT_EQ(countVerticesAtX(scene, out[0], static_cast<float>(i) * 25.0f), 0u)
            << "path " << i << " should be blocked by quota";
    }
}

// v.reserve 的上界必须被额度收口：极端节距下估算值远超额度，也不得按估算值预留
TEST(OverlaySelectionDashTest, CpuQuotaBoundsReserve)
{
    OverlayDashRecordingScene scene;
    RenderBridge::OverlayScene overlay;
    overlay.setDevicePixelRatio(1.0f);
    overlay.setSelectionDashStyle(makeDashStyle());

    std::vector<RenderBridge::SelectionOutlinePath> paths;
    paths.push_back(makeVerticalPath(0.0f, 0.0f, 40.0f));
    overlay.setSelectionOutlines(std::move(paths));

    RenderBridge::OverlayFrameParams params;
    // 深放大：1 世界单位 = 100000 物理像素 → dash 10px 只有 1e-4 世界单位，
    // 展开点数估算约 44 万，远超下面设置的额度
    params.pixelToWorld = 1e-5f;
    params.dashOffsetPx = 0.0f;
    params.visibleWorld = Ut::BBox2d{};
    params.originalVisible = false;
    overlay.setFrameParams(params);
    overlay.setSelectionDashGpuEnabled(false);

    overlay.setSelectionDashCpuPointQuota(64);
    std::vector<RA::DrawInstruction> out;
    overlay.submit(scene, out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].vertexCount, 64u);
    const auto stats = overlay.selectionDashCpuStats();
    EXPECT_EQ(stats.quota, 64u);
    // 上界被额度收口：不再按估算值（44 万点）预留
    EXPECT_EQ(stats.reservedPoints, 64u);
    EXPECT_EQ(stats.usedPoints, 64u);
    EXPECT_EQ(stats.truncatedPaths, 1u);
}

// ==================== B1 场景缓存工作量对比 ====================
//
// 口径：同一场景（20000 条线段图元，全部在视口内）、同一缩放（正交单位矩阵），
// 比较两种「每帧提交形状」：
//   基线：每帧提交环境顶点 + submitDrawList（后端逐条解析 20000 条做剔除/合批）
//   缓存：每 30 帧重建一次（离屏帧 = 环境 + 绘制列表），其余 29 帧只提交
//         一张 6 顶点的全屏贴图四边形
//
// Null 后端不做光栅化，因此这里测到的是 **CPU 侧每帧提交耗时**，它正是
// RenderWidget 每帧在主线程上花掉的那部分；GPU 侧省掉的光栅化工作量用
// FrameStatistics 的 lineCount 对照（后端不填该字段时只报耗时）。
TEST(OverlaySelectionDashTest, SceneCacheWorkloadComparison)
{
    RenderBridge::RenderSessionHost host;
    RenderBridge::RenderSessionHost::Config cfg;
    cfg.applicationName = "SceneCacheWorkload";
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.backend = RA::RenderBackend::Null;
    cfg.transientBufferBytes = 8ull * 1024 * 1024;
    ASSERT_TRUE(host.initialize(cfg));

    RA::IRenderScene* scene = host.scene();
    RA::IRenderDevice* device = host.device();
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(device, nullptr);

    // 一条线段图元的常驻几何：2 个 P3C3 顶点
    RA::GeometryStoreHandle store = device->createGeometryStore(RA::GeometryStoreDesc{});
    ASSERT_TRUE(RA::isValid(store));
    RA::GeometryBlock block{};
    ASSERT_EQ(RA::GeometryAllocResult::Ok, device->allocGeometry(store, 48, block));
    const float lineVerts[12] = { 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    ASSERT_TRUE(device->writeGeometry(store, block.id, 0, sizeof(lineVerts), lineVerts));
    device->flushGeometry(store);

    constexpr uint32_t kEntityCount = 20000;
    RA::DrawListHandle list = scene->createDrawList(
        RA::DrawListDesc{ kEntityCount, true, true, RA::ViewVolumeType::Rect2D });
    ASSERT_TRUE(RA::isValid(list));

    for (uint32_t i = 0; i < kEntityCount; ++i)
    {
        RA::DrawInstruction cmd{};
        cmd.vertexBuffer = block.buffer;
        cmd.vertexOffset = block.offset;
        cmd.vertexCount = 2;
        cmd.topology = RA::PrimitiveType::Lines;
        cmd.space = RA::RenderSpace::World;
        cmd.format = RA::VertexFormat::PositionColor;
        cmd.sortKey = RA::makeSortKey(100, 0, 0, static_cast<uint16_t>(i));
        RA::Aabb3 box{};
        const float x = static_cast<float>(i % 1000);
        const float y = static_cast<float>(i / 1000);
        box.minX = x;
        box.maxX = x + 1.0f;
        box.minY = y;
        box.maxY = y + 1.0f;
        ASSERT_TRUE(scene->upsertDrawItem(list, i, cmd, &box));
    }

    RA::ViewVolume view{};
    view.type = RA::ViewVolumeType::Rect2D;
    view.rect[0] = 0.0f;
    view.rect[1] = 0.0f;
    view.rect[2] = 1920.0f;
    view.rect[3] = 1080.0f;

    // 环境层代表工作量：8192 个顶点（工作台/网格/标尺的典型量级）
    auto submitEnv = [&]() {
        RA::TransientAlloc alloc{};
        if (!scene->allocTransient(24ull * 8192, alloc) || alloc.cpuPtr == nullptr)
        {
            return;
        }
        std::memset(alloc.cpuPtr, 0, 24ull * 8192);
        RA::DrawInstruction cmd{};
        cmd.vertexBuffer = alloc.buffer;
        cmd.vertexOffset = alloc.offset;
        cmd.vertexCount = 8192;
        cmd.topology = RA::PrimitiveType::Lines;
        cmd.space = RA::RenderSpace::World;
        cmd.format = RA::VertexFormat::PositionColor;
        cmd.sortKey = RA::makeSortKey(10, 0, 0, 0);
        RA::DrawPacket packet{};
        packet.enableCulling = false;
        packet.hasViewMatrix = false;
        packet.viewport[2] = 1920.0f;
        packet.viewport[3] = 1080.0f;
        packet.commands = &cmd;
        packet.commandCount = 1;
        scene->submit(packet);
    };

    auto submitComposite = [&](RA::TextureHandle target, uint32_t pipeline) {
        RA::TransientAlloc alloc{};
        if (!scene->allocTransient(32ull * 6, alloc) || alloc.cpuPtr == nullptr)
        {
            return;
        }
        std::memset(alloc.cpuPtr, 0, 32ull * 6);
        RA::DrawInstruction cmd{};
        cmd.vertexBuffer = alloc.buffer;
        cmd.vertexOffset = alloc.offset;
        cmd.vertexCount = 6;
        cmd.topology = RA::PrimitiveType::Triangles;
        cmd.space = RA::RenderSpace::Screen;
        cmd.format = RA::VertexFormat::PositionUVColor;
        cmd.pipelineIndex = pipeline;
        cmd.texture = target;
        cmd.sortKey = RA::makeSortKey(0, 0, 0, 0);
        RA::DrawPacket packet{};
        packet.enableCulling = false;
        packet.hasViewMatrix = false;
        packet.viewport[2] = 1920.0f;
        packet.viewport[3] = 1080.0f;
        packet.commands = &cmd;
        packet.commandCount = 1;
        scene->submit(packet);
    };

    constexpr int kFrames = 120;
    constexpr int kRebuildPeriod = 30;

    // ---------- 基线：每帧整场景 ----------
    RA::FrameStatistics baselineStats{};
    const auto baselineStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFrames; ++frame)
    {
        ASSERT_TRUE(scene->beginFrame());
        submitEnv();
        scene->submitDrawList(list, &view);
        scene->endFrame();
        baselineStats = scene->getFrameStatistics();
    }
    const double baselineMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - baselineStart).count() / kFrames;

    // ---------- 缓存：重建帧整场景，命中帧只合成 ----------
    const RA::TextureHandle target = scene->createRenderTarget(1920, 1080);
    ASSERT_TRUE(RA::isValid(target));
    const uint32_t compositePipeline = device->defaultPipeline(RA::PipelineKind::ScreenTextured);
    ASSERT_NE(compositePipeline, 0u);

    RA::FrameStatistics cacheStats{};
    const auto cacheStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFrames; ++frame)
    {
        if (frame % kRebuildPeriod == 0)
        {
            RA::RenderTargetBinding offscreen{};
            offscreen.color = target;
            offscreen.width = 1920;
            offscreen.height = 1080;
            ASSERT_TRUE(scene->setRenderTarget(offscreen));
            ASSERT_TRUE(scene->beginFrame());
            submitEnv();
            scene->submitDrawList(list, &view);
            scene->endFrame();
            ASSERT_TRUE(scene->setRenderTarget(RA::RenderTargetBinding{}));
        }
        ASSERT_TRUE(scene->beginFrame());
        submitComposite(target, compositePipeline);
        scene->endFrame();
        cacheStats = scene->getFrameStatistics();
    }
    const double cacheMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cacheStart).count() / kFrames;

    std::printf("[bench] entities=%u frames=%d baseline=%.3f ms/frame cache=%.3f ms/frame\n",
        kEntityCount,
        kFrames,
        baselineMs,
        cacheMs);
    std::printf("[bench] baseline: draws=%u lines=%u transient=%llu | cache: draws=%u lines=%u transient=%llu\n",
        baselineStats.drawCallCount,
        baselineStats.lineCount,
        static_cast<unsigned long long>(baselineStats.transientBytesUsed),
        cacheStats.drawCallCount,
        cacheStats.lineCount,
        static_cast<unsigned long long>(cacheStats.transientBytesUsed));

    EXPECT_LT(cacheMs, baselineMs);
}
