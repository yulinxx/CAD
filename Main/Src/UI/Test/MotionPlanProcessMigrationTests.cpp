/**
 * @file MotionPlanProcessMigrationTests.cpp
 * @brief 旧档案工艺迁移回归：迁移前后刀路必须逐字段一致。
 *
 * 背景：
 *   `.apr` 新增作业级工艺模型（ProcessModelData）后，旧档案没有该块，
 *   读入时由 `migrateLegacy` 从 processes + layers 迁移。
 *   迁移最容易犯的错误是「顺手用默认值补齐缺失字段」——那会静默改变加工行为，
 *   而且只在打开旧文件时才发作，现场极难定位。
 *
 * 本测试锁定两件事：
 *   1. 迁移只依据字段存在性：旧档案没有的细节块必须保持「无」（hasXxx=false），
 *      不得用构造默认值冒充配置值；
 *   2. 同一份旧 .apr，迁移前（直接读 processes）与迁移后（读 ProcessModel）
 *      编译出的 MotionCommand 序列逐字段相等，包括功率/速度/脉宽这些
 *      三套结构默认值互不相同的字段。
 */

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "FileIO/AprDocument.h"
#include "FileIO/AprSerializer.h"
#include "Internal/AprDocumentData.h"

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"
    #include "MotionCommandCompare.h"

// Unity 构建会把多个测试源合并成一个 TU，帮助函数必须放进具名命名空间，
// 避免与同组其他测试文件的同名 helper 冲突。
namespace PmMig
{
    // ---------- 旧档案构造 ----------

    struct LegacyFixture
    {
        Eg::SceneManager scene;
        LayerManager layers;
        Fio::AprDocument document;
        uint32_t cutLayerId = 0;      ///< 绑定工艺 1（切割）
        uint32_t engraveLayerId = 0;  ///< 绑定工艺 2（雕刻）

        LegacyFixture()
            : layers(&scene)
        {
        }
    };

    /// 造一份「旧 .apr」：两个工艺 + 两个图层，参数刻意避开三套结构的默认值
    /// （功率 50/50/100、速度 200/100/1000、脉宽 10/0），
    /// 任何默认值填充都会让命令对比立刻失败。
    void buildLegacyArchive(LegacyFixture& fixture)
    {
        Fio::AprDocument& doc = fixture.document;

        Fio::AprProcessInfo cut;
        cut.id = 1;
        std::strcpy(cut.name, "LegacyCut");
        cut.order = 0;
        cut.output = true;
        cut.params.powerPercent = 73.25;
        cut.params.speedMmPerSec = 213.0;
        cut.params.frequencyHz = 18500.0;
        cut.params.pulseWidthUs = 37.0;
        cut.params.waveformIndex = 3;
        cut.params.passes = 2;
        cut.params.fillEnabled = false;
        cut.params.fillIntervalMm = 0.17;
        cut.params.fillAngleDeg = 15.0;
        cut.params.fillMode = 1;
        cut.params.kerfMm = 0.35;
        cut.params.rapidSpeedMmPerSec = 0.0;
        cut.params.focusOffsetMm = 1.25;
        cut.params.leadInEnabled = true;
        cut.params.leadInLengthMm = 2.4;
        cut.params.leadInMode = 1;
        cut.params.leadInAngleDeg = 25.0;
        doc.addProcess(cut);

        Fio::AprProcessInfo engrave;
        engrave.id = 2;
        std::strcpy(engrave.name, "LegacyEngrave");
        engrave.order = 1;
        engrave.output = true;
        engrave.params.powerPercent = 41.5;
        engrave.params.speedMmPerSec = 355.0;
        engrave.params.frequencyHz = 26000.0;
        engrave.params.pulseWidthUs = 12.5;
        engrave.params.passes = 1;
        engrave.params.kerfMm = 0.0;
        engrave.params.leadInEnabled = false;
        doc.addProcess(engrave);

        // 图层 ID 由 LayerManager 决定，文档侧复用同一组 ID
        const std::vector<int> layerIds = fixture.layers.getAllLayerIds();
        ASSERT_GE(layerIds.size(), 2u);

        Fio::AprLayerInfo layerA;
        layerA.id = static_cast<uint32_t>(layerIds[0]);
        fixture.cutLayerId = layerA.id;
        std::strcpy(layerA.name, "LayerA");
        layerA.visible = true;
        layerA.output = true;
        layerA.defaultProcessId = 1;
        doc.addLayer(layerA);

        Fio::AprLayerInfo layerB;
        layerB.id = static_cast<uint32_t>(layerIds[1]);
        fixture.engraveLayerId = layerB.id;
        std::strcpy(layerB.name, "LayerB");
        layerB.visible = true;
        layerB.output = true;
        layerB.defaultProcessId = 2;
        doc.addLayer(layerB);

        doc.setDefaultProcessId(1);
        doc.setDocumentUnits(0);

        // 内部字段（公开 POD 未暴露）：contourOrdering 也必须在迁移中保留
        auto& data = Fio::syDocumentData(doc);
        for (Fio::ProcessInfo& process : data.processes)
        {
            process.contourOrdering = (process.id == 1) ? 1 : 0;
        }
    }

    /// 两个图层各放一个矩形，位置不同，便于区分两层的出光参数
    void buildScene(LegacyFixture& fixture)
    {
        const std::vector<int> layerIds = fixture.layers.getAllLayerIds();
        ASSERT_GE(layerIds.size(), 2u);

        Eg::AprPolygon polyA;
        polyA.setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(60.0, 0.0),
            Ut::Vec2d(60.0, 40.0), Ut::Vec2d(0.0, 40.0) });
        polyA.bClosed = true;
        Eg::SceneWriteGateway::addEntity(fixture.scene, &polyA);
        Eg::AprEntity* entityA = fixture.scene.getAllEntities().back();
        fixture.layers.assignEntityToLayer(entityA, layerIds[0]);

        Eg::AprPolygon polyB;
        polyB.setVertices({ Ut::Vec2d(80.0, 0.0), Ut::Vec2d(140.0, 0.0),
            Ut::Vec2d(140.0, 40.0), Ut::Vec2d(80.0, 40.0) });
        polyB.bClosed = true;
        Eg::SceneWriteGateway::addEntity(fixture.scene, &polyB);
        Eg::AprEntity* entityB = fixture.scene.getAllEntities().back();
        fixture.layers.assignEntityToLayer(entityB, layerIds[1]);
    }

    // ---------- 迁移前 / 迁移后两种取参数方式 ----------

    /// 迁移前：旧代码直接读 processes + layer.defaultProcessId
    Eg::ProcessParams legacyParams(const Fio::ProcessInfo& p)
    {
        Eg::ProcessParams params;
        params.enabled = p.paramsEnabled;
        params.powerPercent = p.powerPercent;
        params.speedMmPerSec = p.speedMmPerSec;
        params.frequencyHz = p.frequencyHz;
        params.pulseWidthUs = p.pulseWidthUs;
        params.waveformIndex = p.waveformIndex;
        params.passes = p.passes;
        params.fillEnabled = p.fillEnabled;
        params.fillIntervalMm = p.fillIntervalMm;
        params.fillAngleDeg = p.fillAngleDeg;
        params.fillMode = static_cast<Eg::FillMode>(p.fillMode);
        params.kerfMm = p.kerfMm;
        params.rapidSpeedMmPerSec = p.rapidSpeedMmPerSec;
        params.focusOffsetMm = p.focusOffsetMm;
        params.contourOrdering = p.contourOrdering;
        params.leadInEnabled = p.leadInEnabled;
        params.leadInLengthMm = p.leadInLengthMm;
        params.leadInMode = p.leadInMode;
        params.leadInAngleDeg = p.leadInAngleDeg;
        return params;
    }

    const Fio::ProcessInfo* findProcess(const Fio::AprDocumentData& data, uint32_t id)
    {
        if (id == 0)
        {
            return nullptr;
        }
        for (const Fio::ProcessInfo& p : data.processes)
        {
            if (p.id == id)
            {
                return &p;
            }
        }
        return nullptr;
    }

    ToolpathJobSpec specFromLegacyArchive(const Fio::AprDocumentData& data)
    {
        ToolpathJobSpec spec;

        const Fio::ProcessInfo* defaultProcess = findProcess(data, data.defaultProcessId);
        if (!defaultProcess && !data.processes.empty())
        {
            defaultProcess = &data.processes.front();
        }
        if (defaultProcess)
        {
            spec.defaultParams = legacyParams(*defaultProcess);
        }

        for (const Fio::LayerInfo& layer : data.layers)
        {
            const Fio::ProcessInfo* process = findProcess(data, layer.defaultProcessId);
            if (!process)
            {
                process = defaultProcess;
            }
            if (process)
            {
                spec.layerParams[static_cast<int>(layer.id)] = legacyParams(*process);
            }
        }
        return spec;
    }

    ToolpathJobSpec specFromProcessModel(const Eg::ProcessModel& model)
    {
        ToolpathJobSpec spec;
        spec.defaultParams = model.defaults;
        spec.rapidSpeedMmPerSec = model.global.rapidSpeedMmPerSec;
        spec.chordToleranceMm = model.global.chordToleranceMm;
        spec.emitArcs = model.global.emitArcs;
        spec.skipHiddenLayers = model.global.skipHiddenLayers;
        spec.skipLockedLayers = model.global.skipLockedLayers;
        spec.emitTrailingLaserOff = model.global.emitTrailingLaserOff;
        spec.failOnUnmachinableEntity = model.global.failOnUnmachinable;

        for (const Eg::LayerProcessEntry& layer : model.layers)
        {
            spec.layerParams[layer.layerId] = Eg::resolveLayerParams(model, layer.layerId);
        }
        return spec;
    }

    // ---------- 序列化辅助 ----------

    std::vector<uint8_t> saveArchive(Fio::AprDocument& doc)
    {
        Fio::AprSerializer serializer;
        std::vector<uint8_t> bytes;
        Fio::BinaryBlobOut query;
        EXPECT_TRUE(serializer.serializeToMemory(doc, &query).success);
        bytes.resize(query.written);
        Fio::BinaryBlobOut out{ bytes.data(), bytes.size(), 0 };
        EXPECT_TRUE(serializer.serializeToMemory(doc, &out).success);
        return bytes;
    }

    bool loadArchive(const std::vector<uint8_t>& bytes, Fio::AprDocument& doc)
    {
        Fio::AprSerializer serializer;
        Fio::BinaryBlob in{ const_cast<uint8_t*>(bytes.data()), bytes.size() };
        return serializer.deserializeFromMemory(in, doc).success;
    }

    // ---------- 指令比较 ----------

    void expectSamePlan(const Hw::MotionPlanView& expected, const Hw::MotionPlanView& actual)
    {
        ASSERT_EQ(expected.commandCount, actual.commandCount);
        for (size_t i = 0; i < expected.commandCount; ++i)
        {
            EXPECT_TRUE(MotionTest::sameCommand(expected.commands[i], actual.commands[i]))
                << "MotionCommand #" << i << " differs after migration";
        }
    }
}  // namespace PmMig

// ============================================================
//  1. 迁移只依据字段存在性，不填默认值
// ============================================================

TEST(MotionPlanProcessMigrationTest, LegacyArchiveMigratesWithoutFillingDefaults)
{
    using namespace PmMig;

    LegacyFixture fixture;
    fixture.layers.createLayer("LayerA");
    fixture.layers.createLayer("LayerB");
    buildLegacyArchive(fixture);

    const std::vector<uint8_t> legacyBytes = saveArchive(fixture.document);
    ASSERT_FALSE(legacyBytes.empty());

    Fio::AprDocument loaded;
    ASSERT_TRUE(loadArchive(legacyBytes, loaded));

    const Fio::AprDocumentData& data = Fio::syDocumentData(loaded);
    ASSERT_TRUE(data.processModelMigrated);
    ASSERT_NE(nullptr, data.processModel);
    const Eg::ProcessModel& model = *data.processModel;

    // 基础参数原样继承（默认值不同的一组字段）
    EXPECT_DOUBLE_EQ(73.25, model.defaults.powerPercent);
    EXPECT_DOUBLE_EQ(213.0, model.defaults.speedMmPerSec);
    EXPECT_DOUBLE_EQ(37.0, model.defaults.pulseWidthUs);
    EXPECT_DOUBLE_EQ(18500.0, model.defaults.frequencyHz);
    EXPECT_EQ(1, model.defaults.contourOrdering);
    EXPECT_DOUBLE_EQ(0.35, model.defaults.kerfMm);
    EXPECT_TRUE(model.defaults.leadInEnabled);

    ASSERT_EQ(2u, model.layers.size());
    const Eg::LayerProcessEntry* layerA =
        Eg::findLayerProcess(model, static_cast<int32_t>(fixture.cutLayerId));
    ASSERT_NE(nullptr, layerA);
    EXPECT_DOUBLE_EQ(73.25, layerA->params.powerPercent);
    const Eg::LayerProcessEntry* layerB =
        Eg::findLayerProcess(model, static_cast<int32_t>(fixture.engraveLayerId));
    ASSERT_NE(nullptr, layerB);
    EXPECT_DOUBLE_EQ(41.5, layerB->params.powerPercent);
    EXPECT_DOUBLE_EQ(355.0, layerB->params.speedMmPerSec);
    EXPECT_DOUBLE_EQ(12.5, layerB->params.pulseWidthUs);

    // 旧档案没有细节块：所有 has* 必须为 false，不得用构造默认值冒充配置
    for (const Eg::LayerProcessEntry& layer : model.layers)
    {
        EXPECT_FALSE(layer.detail.hasKerf);
        EXPECT_FALSE(layer.detail.hasLeadInOut);
        EXPECT_FALSE(layer.detail.hasPierce);
        EXPECT_FALSE(layer.detail.hasCorner);
        EXPECT_FALSE(layer.detail.hasPowerCurve);
        EXPECT_FALSE(layer.detail.hasPassStrategy);
        EXPECT_FALSE(layer.detail.hasCommonEdge);
        EXPECT_FALSE(layer.detail.pierce.enabled);
    }
    EXPECT_FALSE(model.defaultDetail.hasPierce);

    // 细节由基础参数派生（继承），不是写死的默认值
    const Eg::ProcessDetail resolved =
        Eg::resolveLayerDetail(model, static_cast<int32_t>(fixture.cutLayerId));
    EXPECT_DOUBLE_EQ(0.35, resolved.kerf.kerfMm);
    EXPECT_TRUE(resolved.kerf.enabled);
    EXPECT_TRUE(resolved.leadInOut.enabled);
    EXPECT_DOUBLE_EQ(2.4, resolved.leadInOut.leadInLengthMm);
    EXPECT_EQ(1, resolved.leadInOut.leadInMode);
    EXPECT_FALSE(resolved.hasPierce);
}

// ============================================================
//  2. 迁移前后刀路逐字段一致（核心回归）
// ============================================================

TEST(MotionPlanProcessMigrationTest, LegacyArchiveMotionCommandsAreIdenticalAfterMigration)
{
    using namespace PmMig;

    LegacyFixture fixture;
    fixture.layers.createLayer("LayerA");
    fixture.layers.createLayer("LayerB");
    buildScene(fixture);
    buildLegacyArchive(fixture);

    const std::vector<uint8_t> legacyBytes = saveArchive(fixture.document);

    Fio::AprDocument loaded;
    ASSERT_TRUE(loadArchive(legacyBytes, loaded));
    const Fio::AprDocumentData& data = Fio::syDocumentData(loaded);
    ASSERT_NE(nullptr, data.processModel);

    const ToolpathJobSpec legacySpec = specFromLegacyArchive(data);
    const ToolpathJobSpec modelSpec = specFromProcessModel(*data.processModel);

    // 参数层面的关键字段先对齐，失败时比命令差异更好定位
    EXPECT_DOUBLE_EQ(legacySpec.defaultParams.powerPercent, modelSpec.defaultParams.powerPercent);
    EXPECT_DOUBLE_EQ(legacySpec.defaultParams.speedMmPerSec, modelSpec.defaultParams.speedMmPerSec);
    EXPECT_DOUBLE_EQ(legacySpec.defaultParams.pulseWidthUs, modelSpec.defaultParams.pulseWidthUs);
    EXPECT_EQ(legacySpec.layerParams.size(), modelSpec.layerParams.size());

    Hw::MotionPlanBuilder legacyBuilder;
    const ToolpathCompileResult legacyResult =
        MotionPlanCompiler::compile(fixture.scene, &fixture.layers, legacySpec, legacyBuilder);
    ASSERT_TRUE(legacyResult.ok) << legacyResult.error.toStdString();

    Hw::MotionPlanBuilder modelBuilder;
    const ToolpathCompileResult modelResult =
        MotionPlanCompiler::compile(fixture.scene, &fixture.layers, modelSpec, modelBuilder);
    ASSERT_TRUE(modelResult.ok) << modelResult.error.toStdString();

    expectSamePlan(legacyBuilder.view(), modelBuilder.view());
}

// ============================================================
//  3. 迁移结果存盘再读回，刀路仍然一致
// ============================================================

TEST(MotionPlanProcessMigrationTest, MigratedArchiveSaveReloadKeepsMotionCommands)
{
    using namespace PmMig;

    LegacyFixture fixture;
    fixture.layers.createLayer("LayerA");
    fixture.layers.createLayer("LayerB");
    buildScene(fixture);
    buildLegacyArchive(fixture);

    const std::vector<uint8_t> legacyBytes = saveArchive(fixture.document);

    Fio::AprDocument loaded;
    ASSERT_TRUE(loadArchive(legacyBytes, loaded));
    const ToolpathJobSpec legacySpec = specFromLegacyArchive(Fio::syDocumentData(loaded));

    // 迁移后的文档带 ProcessModelData 存盘：新档案走 readModel，不再迁移
    const std::vector<uint8_t> migratedBytes = saveArchive(loaded);
    Fio::AprDocument reloaded;
    ASSERT_TRUE(loadArchive(migratedBytes, reloaded));
    const Fio::AprDocumentData& reloadedData = Fio::syDocumentData(reloaded);
    EXPECT_FALSE(reloadedData.processModelMigrated);
    ASSERT_NE(nullptr, reloadedData.processModel);

    const ToolpathJobSpec reloadedSpec = specFromProcessModel(*reloadedData.processModel);

    Hw::MotionPlanBuilder legacyBuilder;
    ASSERT_TRUE(MotionPlanCompiler::compile(fixture.scene, &fixture.layers, legacySpec, legacyBuilder).ok);

    Hw::MotionPlanBuilder reloadedBuilder;
    ASSERT_TRUE(
        MotionPlanCompiler::compile(fixture.scene, &fixture.layers, reloadedSpec, reloadedBuilder).ok);

    expectSamePlan(legacyBuilder.view(), reloadedBuilder.view());
}

#endif  // ENABLE_HARDWARE
