/**
 * @file MixedSelectionRenderTests.cpp
 * @brief 混选（矢量 + 色块 + 位图）渲染回归测试
 *
 * 缺陷：开启「选中时隐藏本体」后把三类图元一起选中，色块与位图会从画面上消失，
 * 只剩矢量（虚线轮廓）。根因是隐藏本体只该作用于「能离散出虚线轮廓」的矢量类型：
 * SyImage / SyFillRegion 的 value() 退回基点，SelectionOutlineBuilder 产不出轮廓，
 * 本体被摘掉后没有任何替代视觉。
 *
 * 本测试不依赖 GPU / 真实窗口：
 *   - Null 后端设备 + 最小假场景走 RenderSceneBuilder 的装配与隐藏过滤链路；
 *   - BitmapReconciler 只做 CPU 侧账本登记；
 *   - SceneRefreshCoordinator 通过诊断接口检查真正会被摘出世界层的图元 ID 集合。
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine2D/Render/SelectionOutlineBuilder.h"
#include "Engine2D/SyEntity/SyFillRegion.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/SyEntity/SyLine.h"

#include "Render/RenderSceneBuilder.h"
#include "RenderAbstraction/IRenderFactory.h"
#include "RenderAbstraction/IRenderScene.h"
#include "RenderBridge/RenderXAdapter.h"
#include "RenderWidget.h"
#include "UI/Render/BitmapReconciler.h"
#include "UI/Render/SceneRefreshCoordinator.h"

namespace
{
    /// 三类图元的公共样本：矢量线 + 三角色块 + 带像素数据的位图
    struct MixedSceneSample
    {
        Eg::SyLine line;
        Eg::SyFillRegion fill;
        Eg::SyImage image;
        uint64_t lineId{ 0 };
        uint64_t fillId{ 0 };
        uint64_t imageId{ 0 };
    };

    void buildSample(MixedSceneSample& sample)
    {
        sample.line.setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });

        sample.fill.setVertexVector({
            Ut::Vec2d(20.0, 20.0),
            Ut::Vec2d(30.0, 20.0),
            Ut::Vec2d(20.0, 30.0),
        });

        sample.image.nWidth = 2;
        sample.image.nHeight = 2;
        sample.image.ePixelFormat = Eg::SyPixelFormat::Gray8;
        sample.image.setPixelDataVector(std::vector<unsigned char>(4, 0x80));
        sample.image.topLeft = Ut::Vec2d(40.0, 40.0);
        sample.image.topRight = Ut::Vec2d(60.0, 40.0);
        sample.image.bottomLeft = Ut::Vec2d(40.0, 60.0);
        sample.image.bottomRight = Ut::Vec2d(60.0, 60.0);
    }

    /// 把样本加入场景并记录持久 ID，再把三类一起选中
    void addAndSelectAll(Eg::SceneManager& scene, MixedSceneSample& sample)
    {
        Eg::SyEntity* line = scene.addEntity(&sample.line);
        Eg::SyEntity* fill = scene.addEntity(&sample.fill);
        Eg::SyEntity* image = scene.addEntity(&sample.image);
        EXPECT_NE(line, nullptr);
        EXPECT_NE(fill, nullptr);
        EXPECT_NE(image, nullptr);
        sample.lineId = static_cast<uint64_t>(line->id);
        sample.fillId = static_cast<uint64_t>(fill->id);
        sample.imageId = static_cast<uint64_t>(image->id);

        Eg::VecSyEntityPtr selection;
        selection.push_back(line);
        selection.push_back(fill);
        selection.push_back(image);
        scene.selectEntities(selection);
    }

    /// 最小化假场景：只为让常驻绘制列表初始化成功，其余全部空实现
    class MixedSelectionNullSceneStub final : public RenderAbstraction::IRenderScene
    {
    public:
        bool allocTransient(uint64_t, RenderAbstraction::TransientAlloc&) override
        {
            return false;
        }
        bool submit(const RenderAbstraction::DrawPacket&) override
        {
            return false;
        }
        void setModelMatrix(const RenderAbstraction::Matrix4x4*) override {}

        RenderAbstraction::DrawListHandle createDrawList(const RenderAbstraction::DrawListDesc&) override
        {
            RenderAbstraction::DrawListHandle h;
            h.value = 1;
            return h;
        }
        void destroyDrawList(RenderAbstraction::DrawListHandle) override {}
        void clearDrawList(RenderAbstraction::DrawListHandle) override {}
        bool upsertDrawItem(RenderAbstraction::DrawListHandle,
            uint32_t,
            const RenderAbstraction::DrawInstruction&,
            const RenderAbstraction::Aabb3*) override
        {
            return true;
        }
        bool removeDrawItem(RenderAbstraction::DrawListHandle, uint32_t) override
        {
            return true;
        }
        bool submitDrawList(RenderAbstraction::DrawListHandle, const RenderAbstraction::ViewVolume*) override
        {
            return false;
        }
        bool beginFrame() override
        {
            return true;
        }
        void endFrame() override {}
        RenderAbstraction::TextureHandle createRenderTarget(uint32_t, uint32_t) override
        {
            return {};
        }
        bool readPixels(uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }
        bool setRenderTarget(const RenderAbstraction::RenderTargetBinding&) override
        {
            return false;
        }
        bool readPixelsFromTexture(
            RenderAbstraction::TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint64_t) override
        {
            return false;
        }
        void setCamera(const RenderAbstraction::CameraDesc&) override {}
        void setLighting(const RenderAbstraction::LightingDesc&) override {}
        RenderAbstraction::FrameStatistics getFrameStatistics() const override
        {
            return {};
        }
        bool isValid() const override
        {
            return true;
        }
    };
}  // namespace

// 选择轮廓能力是「能否隐藏本体」的唯一类型门槛：只有矢量类型能离散出轮廓路径
TEST(MixedSelectionRenderTest, SelectionOutlineOnlySupportsVectorTypes)
{
    MixedSceneSample sample;
    buildSample(sample);

    EXPECT_TRUE(Eg::SelectionOutlineBuilder::supportsSelectionOutline(&sample.line));
    EXPECT_FALSE(Eg::SelectionOutlineBuilder::supportsSelectionOutline(&sample.fill));
    EXPECT_FALSE(Eg::SelectionOutlineBuilder::supportsSelectionOutline(&sample.image));
    EXPECT_FALSE(Eg::SelectionOutlineBuilder::supportsSelectionOutline(nullptr));
}

// 混选 + 隐藏开关打开时，协调器只允许矢量本体被摘出世界层
TEST(MixedSelectionRenderTest, HiddenSelectionOnlyRemovesOutlineCapableBodies)
{
    MixedSceneSample sample;
    buildSample(sample);

    Eg::SceneManager scene;
    addAndSelectAll(scene, sample);

    RenderWidget widget;
    widget.resize(800, 600);
    widget.setHideSelectedOriginal(true);
    widget.setSelectionOutlinesActive(true);

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&scene);

    ASSERT_TRUE(widget.hideSelectedOriginalEffective());
    const std::unordered_set<uint64_t> hidden = coordinator.selectedBodyHideIds();
    EXPECT_EQ(hidden.size(), 1u);
    EXPECT_TRUE(hidden.count(sample.lineId) > 0);
    EXPECT_EQ(hidden.count(sample.fillId), 0u);
    EXPECT_EQ(hidden.count(sample.imageId), 0u);

    coordinator.stop();
}

// 同一批装配里，矢量被隐藏时色块批不被裁剪；矢量恢复后三段都回来
TEST(MixedSelectionRenderTest, MixedBatchesSurviveSelectionHiding)
{
    MixedSceneSample sample;
    buildSample(sample);

    Eg::SceneManager scene;
    addAndSelectAll(scene, sample);

    auto* line = scene.findEntityById(static_cast<Eg::EntityId>(sample.lineId));
    auto* fill = scene.findEntityById(static_cast<Eg::EntityId>(sample.fillId));
    auto* image = scene.findEntityById(static_cast<Eg::EntityId>(sample.imageId));
    ASSERT_NE(line, nullptr);
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(image, nullptr);

    auto factory = RenderBridge::RenderXAdapter::createFactory();
    ASSERT_NE(factory, nullptr);
    RenderAbstraction::DeviceConfig config;
    config.backend = RenderAbstraction::RenderBackend::Null;
    config.enableValidation = true;
    config.applicationName = "MixedSelectionRenderTests";
    auto device = factory->createDevice(config);
    ASSERT_NE(device, nullptr);

    MixedSelectionNullSceneStub renderScene;
    Render::RenderSceneBuilder builder;
    ASSERT_TRUE(builder.initialize(*device, renderScene));
    ASSERT_TRUE(builder.valid());

    auto emitAll = [&builder, line, fill, image]() {
        builder.beginRebuild();
        Eg::emitEntityGeometry(*line, builder);
        Eg::emitEntityGeometry(*fill, builder);
        Eg::emitEntityGeometry(*image, builder);
        builder.endRebuild();
    };

    // 未隐藏：矢量与色块都产生几何段；位图走独立位图通道，不出几何段
    builder.unsetHiddenEntityIds();
    emitAll();
    EXPECT_GT(builder.pieceCountFor(static_cast<Render::EntityId>(sample.lineId)), 0u);
    EXPECT_GT(builder.pieceCountFor(static_cast<Render::EntityId>(sample.fillId)), 0u);
    EXPECT_EQ(builder.pieceCountFor(static_cast<Render::EntityId>(sample.imageId)), 0u);

    // 选中隐藏只摘矢量：色块批必须原样保留
    const std::unordered_set<uint64_t> hiddenLine{ sample.lineId };
    builder.setHiddenEntityIds(hiddenLine);
    emitAll();
    EXPECT_EQ(builder.pieceCountFor(static_cast<Render::EntityId>(sample.lineId)), 0u);
    EXPECT_GT(builder.pieceCountFor(static_cast<Render::EntityId>(sample.fillId)), 0u);

    // 取消隐藏：矢量段重新装配
    builder.unsetHiddenEntityIds();
    emitAll();
    EXPECT_GT(builder.pieceCountFor(static_cast<Render::EntityId>(sample.lineId)), 0u);
    EXPECT_GT(builder.pieceCountFor(static_cast<Render::EntityId>(sample.fillId)), 0u);

    builder.shutdown();
}

// 位图账本：选中位图不在隐藏集合里，对账后仍然保留（不会被选中态裁剪）
TEST(MixedSelectionRenderTest, SelectedBitmapStaysInBitmapLedger)
{
    MixedSceneSample sample;
    buildSample(sample);

    Eg::SceneManager scene;
    addAndSelectAll(scene, sample);

    RenderWidget widget;
    widget.resize(800, 600);
    BitmapReconciler reconciler(&widget);

    // 协调器对位图/文字固定传 hideSelected=false：选中时保持图像本体
    const std::unordered_set<uint64_t> selected{ sample.imageId, sample.fillId, sample.lineId };
    reconciler.reconcile(&scene, /*fullReconcile=*/false, selected, /*hideSelected=*/false, {});
    EXPECT_TRUE(reconciler.has(sample.imageId));
}
