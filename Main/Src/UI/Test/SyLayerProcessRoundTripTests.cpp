/**
 * @file SyLayerProcessRoundTripTests.cpp
 * @brief P0-2 回归测试：.sy 保存/重开后图层与工艺不丢
 *
 * 覆盖点：
 *  - 写路径：ExportService 从 LayerManager 抓取图层表（颜色/output/默认工艺/profile id）
 *    与工艺表（功率/速度/频率/焦距/遍数），NativeExportWriter 写进 .sy；
 *  - 读路径：ImportService 打开时重建 LayerManager（换文档重置 + 逐字段还原）并
 *    按原 id 重建 HardwareProfileManager；
 *  - 兼容：旧版 .sy（无图层/工艺段）不崩，图元落到默认图层。
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QString>

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Interaction/HardwareProfileManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/AprEntity/AprLine.h"

#include "ExportDispatcher.h"
#include "ExportService.h"
#include "Writers/NativeExportWriter.h"

#include "ImportDispatcher.h"
#include "ImportOptions.h"
#include "ImportService.h"
#include "Readers/NativeImportReader.h"

#include "FileIO/AprDocument.h"
#include "FileIO/Writers/NativeWriter.h"

namespace
{
    /// 临时文件路径：测试结束后由调用方删除
    std::filesystem::path makeTempFile(const std::string& name)
    {
        return std::filesystem::temp_directory_path() / ("sanyi_p0_sy_" + name);
    }

    /// 往场景加一条线；SceneManager::addEntity 内部会 clone 并分配持久 id
    Eg::AprEntity* addLine(Eg::SceneManager& scene,
        const std::vector<Ut::Vec2d>& points)
    {
        Eg::AprLine line;
        line.setPointVector(points);
        return Eg::SceneWriteGateway::addEntity(scene, &line);
    }

    void expectColor(const Ut::Color& color, int r, int g, int b)
    {
        EXPECT_EQ(r, color.red255());
        EXPECT_EQ(g, color.green255());
        EXPECT_EQ(b, color.blue255());
    }
}  // namespace

TEST(SyLayerProcessRoundTrip, TwoLayersSurviveSaveAndReopen)
{
    const auto path = makeTempFile("two_layers.sy");
    std::filesystem::remove(path);

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    HardwareProfileManager hardwareProfiles;
    layers.setHardwareProfileManager(&hardwareProfiles);

    // ---- 两个工艺：参数逐字段可往返（含焦距补偿） ----
    const int cutProfileId = hardwareProfiles.createProfile("Cut 101");
    HardwareProfile cutProfile;
    cutProfile.id = cutProfileId;
    cutProfile.name = "Cut 101";
    cutProfile.power = 80.0;
    cutProfile.speed = 12.5;
    cutProfile.frequency = 30.0;
    cutProfile.focusOffset = 1.25;
    cutProfile.passes = 2;
    ASSERT_TRUE(hardwareProfiles.setProfile(cutProfileId, cutProfile));

    const int markProfileId = hardwareProfiles.createProfile("Mark 202");
    HardwareProfile markProfile;
    markProfile.id = markProfileId;
    markProfile.name = "Mark 202";
    markProfile.power = 40.0;
    markProfile.speed = 200.0;
    markProfile.frequency = 80.0;
    markProfile.focusOffset = -0.5;
    markProfile.passes = 1;
    ASSERT_TRUE(hardwareProfiles.setProfile(markProfileId, markProfile));

    // ---- 两层：颜色 / output / 默认工艺 / 硬件 profile 各不相同 ----
    const int cutLayerId = layers.createLayer("Cut");
    const int markLayerId = layers.createLayer("Mark");
    ASSERT_GE(cutLayerId, 0);
    ASSERT_GE(markLayerId, 0);

    ASSERT_TRUE(layers.setLayerColor(cutLayerId, Ut::Color::fromRGB255(17, 34, 51, 255)));
    ASSERT_TRUE(layers.setLayerOutput(cutLayerId, false));
    ASSERT_TRUE(layers.setLayerHardwareProfileId(cutLayerId, cutProfileId));
    ASSERT_TRUE(layers.setLayerDefaultProcessId(cutLayerId, static_cast<uint32_t>(cutProfileId)));

    ASSERT_TRUE(layers.setLayerColor(markLayerId, Ut::Color::fromRGB255(204, 102, 51, 255)));
    ASSERT_TRUE(layers.setLayerOutput(markLayerId, true));
    ASSERT_TRUE(layers.setLayerHardwareProfileId(markLayerId, markProfileId));
    ASSERT_TRUE(layers.setLayerDefaultProcessId(markLayerId, static_cast<uint32_t>(markProfileId)));

    Eg::AprEntity* cutEntity = addLine(scene, { Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0) });
    Eg::AprEntity* markEntity = addLine(scene, { Ut::Vec2d(0.0, 10.0), Ut::Vec2d(10.0, 10.0) });
    ASSERT_NE(cutEntity, nullptr);
    ASSERT_NE(markEntity, nullptr);
    ASSERT_TRUE(layers.assignEntityToLayer(cutEntity, cutLayerId));
    ASSERT_TRUE(layers.assignEntityToLayer(markEntity, markLayerId));

    // 记录保存前的规模：重开后必须一致
    const int writtenLayerCount = layers.layerCount();
    const int writtenProfileCount = hardwareProfiles.profileCount();

    // ---- 保存 ----
    ExportDispatcher exportDispatcher;
    exportDispatcher.registerWriter(std::make_unique<NativeExportWriter>());
    ExportService exportService;
    exportService.setDispatcher(&exportDispatcher);
    exportService.setSceneManager(&scene);
    exportService.setNativeMachiningSnapshotProvider([]() -> NativeDocumentSnapshot { return {}; });

    const ExportResult writeResult = exportService.exportFile(QString::fromStdString(path.string()));
    ASSERT_TRUE(writeResult.success) << writeResult.message.toStdString();
    ASSERT_TRUE(std::filesystem::exists(path));

    // ---- 重开：复用同一个 LayerManager，等价于应用的「打开即新文档」 ----
    ImportDispatcher importDispatcher;
    importDispatcher.registerReader(std::make_unique<NativeImportReader>());
    ImportService importService;
    importService.setDispatcher(&importDispatcher);
    importService.setSceneManager(&scene);
    importService.setLayerManager(&layers);

    ImportOptions importOptions;
    importOptions.importAsNewDocument = true;
    const ImportResult importResult =
        importService.importFile(QString::fromStdString(path.string()), importOptions);
    ASSERT_TRUE(importResult.success) << importResult.message.toStdString();

    // 图层数量：LayerManager 内置 9 层默认图层也会被完整写入文件，
    // 重开后按名复用；测试关注的两个业务图层 "Cut"/"Mark" 由文件重建。
    EXPECT_EQ(writtenLayerCount, layers.layerCount());

    const int reopenedCutLayerId = layers.findLayerByName("Cut");
    const int reopenedMarkLayerId = layers.findLayerByName("Mark");
    ASSERT_GE(reopenedCutLayerId, 0);
    ASSERT_GE(reopenedMarkLayerId, 0);

    // ---- 逐字段断言：颜色 / output / 默认工艺 / 硬件 profile ----
    expectColor(layers.layerColor(reopenedCutLayerId), 17, 34, 51);
    EXPECT_FALSE(layers.isLayerOutput(reopenedCutLayerId));
    EXPECT_EQ(cutProfileId, layers.layerHardwareProfileId(reopenedCutLayerId));
    EXPECT_EQ(static_cast<uint32_t>(cutProfileId), layers.layerDefaultProcessId(reopenedCutLayerId));

    expectColor(layers.layerColor(reopenedMarkLayerId), 204, 102, 51);
    EXPECT_TRUE(layers.isLayerOutput(reopenedMarkLayerId));
    EXPECT_EQ(markProfileId, layers.layerHardwareProfileId(reopenedMarkLayerId));
    EXPECT_EQ(static_cast<uint32_t>(markProfileId), layers.layerDefaultProcessId(reopenedMarkLayerId));

    // ---- 工艺参数与 id 一致 ----
    EXPECT_EQ(writtenProfileCount, hardwareProfiles.profileCount());
    const HardwareProfile reopenedCut = hardwareProfiles.profile(cutProfileId);
    EXPECT_NEAR(80.0, reopenedCut.power, 1e-9);
    EXPECT_NEAR(12.5, reopenedCut.speed, 1e-9);
    EXPECT_NEAR(30.0, reopenedCut.frequency, 1e-9);
    EXPECT_NEAR(1.25, reopenedCut.focusOffset, 1e-9);
    EXPECT_EQ(2, reopenedCut.passes);

    const HardwareProfile reopenedMark = hardwareProfiles.profile(markProfileId);
    EXPECT_NEAR(40.0, reopenedMark.power, 1e-9);
    EXPECT_NEAR(200.0, reopenedMark.speed, 1e-9);
    EXPECT_NEAR(80.0, reopenedMark.frequency, 1e-9);
    EXPECT_NEAR(-0.5, reopenedMark.focusOffset, 1e-9);
    EXPECT_EQ(1, reopenedMark.passes);

    // ---- 图元归属：每层恰好一个图元，且都绑到正确的运行时图层 ----
    EXPECT_EQ(2u, scene.getEntityCount());
    EXPECT_EQ(1u, layers.getLayerEntities(reopenedCutLayerId).size());
    EXPECT_EQ(1u, layers.getLayerEntities(reopenedMarkLayerId).size());
    for (const auto& entity : scene.getAllEntities())
    {
        // getAllEntities 返回裸指针集合（非 owning），这里不需要 .get()
        const int layerId = layers.getEntityLayer(entity);
        EXPECT_TRUE(layerId == reopenedCutLayerId || layerId == reopenedMarkLayerId);
    }

    std::filesystem::remove(path);
}

TEST(SyLayerProcessRoundTrip, LegacyFileWithoutLayerSectionFallsBackToDefaultLayer)
{
    const auto path = makeTempFile("legacy_no_layers.sy");
    std::filesystem::remove(path);

    // 构造一份「旧版」文件：只有图元，没有图层表/工艺表
    {
        Eg::AprLine line;
        line.setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 10.0) });

        Fio::AprDocument legacyDoc;
        legacyDoc.addBorrowedEntity(&line);

        Fio::NativeWriter writer(Fio::FileFormat::Native);
        const auto writeResult = writer.writeDocument(path.string().c_str(), legacyDoc);
        ASSERT_TRUE(writeResult.success) << writeResult.errorMessage;
    }

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    HardwareProfileManager hardwareProfiles;
    layers.setHardwareProfileManager(&hardwareProfiles);

    ImportDispatcher importDispatcher;
    importDispatcher.registerReader(std::make_unique<NativeImportReader>());
    ImportService importService;
    importService.setDispatcher(&importDispatcher);
    importService.setSceneManager(&scene);
    importService.setLayerManager(&layers);

    ImportOptions importOptions;
    importOptions.importAsNewDocument = true;
    const ImportResult importResult =
        importService.importFile(QString::fromStdString(path.string()), importOptions);

    // 不能崩、不能失败；缺字段走默认：图层为出厂 9 层，图元落默认图层 0
    EXPECT_TRUE(importResult.success) << importResult.message.toStdString();
    EXPECT_EQ(9, layers.layerCount());
    EXPECT_EQ(1u, layers.getLayerEntities(0).size());
    // 旧文件没有工艺段：HardwareProfileManager 只保留默认 profile
    EXPECT_EQ(1, hardwareProfiles.profileCount());

    std::filesystem::remove(path);
}
