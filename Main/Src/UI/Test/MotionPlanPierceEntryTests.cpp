/**
 * @file MotionPlanPierceEntryTests.cpp
 * @brief 穿孔「配置 → 真实入口 → 刀路」闭环测试。
 *
 * 之前的 MotionPlanPierceTest 只测 MotionPlanCompiler 的静态调用，发现不了
 * 入口没接线：ProcessDetail 里配了穿孔，OperationRegistrar 构造作业规格时
 * 不读它，运行期恒等于关闭。这里走完整链路：
 *
 *   .apr（ProcessModelData） → ImportService/NativeImportReader → LayerManager
 *     → buildToolpathJobSpec（OperationRegistrar 真实取参函数） → MotionPlanCompiler
 *
 * 同时覆盖「本路径不支持的工艺细节」汇总告警：只出一次，内容列出被忽略项。
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "FileIO/AprDocument.h"
#include "FileIO/AprSerializer.h"
#include "Internal/AprDocumentData.h"

#include "Composition/ImportExportServiceSetup.h"
#include "Composition/OperationRegistrar.h"
#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"
#include "Import/Readers/NativeImportReader.h"

#include <QTemporaryDir>

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"
    #include "MotionPlanTestScene.h"

namespace PierceEntry
{
    using MotionTest::rectPoints;

    /// 保存一份带 ProcessData 的 .apr，再把 ProcessModel 注入进去重新落盘。
    ///
    /// 为什么分两步：正常导出只写 ProcessData；ProcessModelData 目前由
    /// 外部工具/后续保存路径产生。测试用内部字段构造它，确保导入端读的是
    /// 真实文件，而不是直接调用 LayerManager。
    bool writeArchiveWithModel(const QString& path, const Eg::ProcessModel& model, int& outLayerId)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);

        Eg::AprPolygon poly;
        poly.setVertices(rectPoints(0.0, 0.0, 100.0, 80.0));
        poly.bClosed = true;
        Eg::SceneWriteGateway::addEntity(scene, &poly);
        Eg::AprEntity* entity = scene.getAllEntities().back();

        const int layerId = layers.createLayer("Pierce");
        if (layerId < 0)
        {
            return false;
        }
        layers.assignEntityToLayer(entity, layerId);
        outLayerId = layerId;

        NativeDocumentSnapshot snapshot = aprSetupCaptureMachiningSnapshot(&layers);
        // 补一条默认 Profile，避免导入时产生「值集为空」的无关告警
        Fio::AprHardwareProfileInfo profile;
        profile.id = 0;
        std::strncpy(profile.name, "Default", sizeof(profile.name) - 1);
        profile.power = 100.0;
        profile.speed = 1000.0;
        snapshot.profiles.push_back(profile);

        // 一次性直接构造并落盘：图元从场景借用（layerId 已由 LayerManager 写好），
        // 图层表/Profile 值集来自快照，工艺模型写入内部唯一持有者。
        // 不走「导出 → 再解析 → 再序列化」的往返，因为反序列化只把图层归属记在
        // AprDocument 的映射表里、不回写图元的 layerId，二次落盘会丢归属。
        Fio::AprDocument doc;
        for (const auto& layer : snapshot.layers)
        {
            doc.addLayerDetail(layer);
        }
        for (const auto& profileEntry : snapshot.profiles)
        {
            doc.addHardwareProfile(profileEntry);
        }
        // 真实档案里的图元都带非 0 持久 ID，测试图元同样给一个固定值。
        entity->id = 1000;
        doc.addBorrowedEntity(entity);

        Eg::ProcessModel stored = model;
        for (Eg::LayerProcessEntry& entry : stored.layers)
        {
            entry.layerId = layerId;
        }
        Fio::syDocumentData(doc).processModel = std::make_unique<Eg::ProcessModel>(std::move(stored));

        Fio::AprSerializer serializer;
        // 走 saveToFile：.apr 是「16 字节头 + protobuf + CRC」容器，
        // serializeToMemory 只给裸 protobuf，直接落盘会被导入端判为 wrong magic。
        return serializer.saveToFile(path.toUtf8().constData(), doc, false, Fio::FileFormat::Native).success;
    }

    /// 真实导入链：NativeImportReader → ImportService → LayerManager
    ImportResult importArchive(const QString& path, Eg::SceneManager& scene, LayerManager& layers)
    {
        ImportDispatcher dispatcher;
        dispatcher.registerReader(std::make_unique<NativeImportReader>());

        // 生产链一定注入 SceneEditService + 撤销管理器：图元入库可能换 ID，
        // 换层映射依赖 addEntities 返回的最终 ID 做修正。
        UndoRedoManager undo(&scene);
        SceneEditService editService(&scene, &undo);
        editService.setLayerManager(&layers);

        ImportService service;
        service.setDispatcher(&dispatcher);
        service.setSceneManager(&scene);
        service.setLayerManager(&layers);
        service.setEditService(&editService);

        ImportContext context;
        context.sourcePath = path;
        context.preserveLayers = true;

        ImportOptions options;
        options.autoFit = false;
        options.autoCenter = false;
        options.autoSwitchWorkbench = false;

        const ImportResult result = service.importWithContext(context, options);
        if (!result.success)
        {
            ADD_FAILURE() << "import failed: " << result.message.toStdString();
        }
        return result;
    }

    /// 读取器把文档图元 clone 出来时丢了持久 ID，入库后会被重新分配，
    /// 导致 ImportService 的「文档 ID → 新 ID」换层映射对不上（读取器既有行为，
    /// 不在本次范围）。按图层名把导入图元放回目标图层，便于从真实入口编译刀路。
    void placeEntityOnLayer(Eg::SceneManager& scene, LayerManager& layers, const char* layerName)
    {
        const int layerId = layers.findLayerByName(layerName);
        ASSERT_GE(layerId, 0) << layerName;
        ASSERT_FALSE(scene.getAllEntities().empty());
        Eg::AprEntity* entity = scene.getAllEntities().front();
        layers.applyEntityLayerMap({ { entity->id, layerId } });
        ASSERT_EQ(layerId, layers.getEntityLayer(entity));
    }

    Eg::PierceSpec pierceFixture()
    {
        Eg::PierceSpec pierce;
        pierce.enabled = true;
        pierce.pierceDelayUs = 500;
        pierce.pierceCount = 2;
        pierce.dwellUs = 200;
        pierce.pierceOffsetMm = 2.0;
        return pierce;
    }

    size_t countDwell(const Hw::MotionPlanView& view)
    {
        size_t n = 0;
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::Dwell)
            {
                ++n;
            }
        }
        return n;
    }

    long firstRapidIndex(const Hw::MotionPlanView& view)
    {
        for (size_t i = 0; i < view.commandCount; ++i)
        {
            if (view.commands[i].op == Hw::MotionOp::RapidTo)
            {
                return static_cast<long>(i);
            }
        }
        return -1;
    }
}  // namespace PierceEntry

// ============================================================
//  1. ProcessDetail 里的穿孔参数真的到达刀路
// ============================================================

TEST(MotionPlanPierceEntryTest, ProcessDetailPierceReachesMotionPlan)
{
    using namespace PierceEntry;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    Eg::ProcessModel model;
    Eg::LayerProcessEntry entry;
    entry.layerId = 0;  // writeArchiveWithModel 会改成真实图层 ID
    entry.detail.hasPierce = true;
    entry.detail.pierce = pierceFixture();
    model.layers.push_back(entry);

    const QString path = dir.filePath(QStringLiteral("pierce-detail.apr"));
    int sourceLayerId = -1;
    ASSERT_TRUE(writeArchiveWithModel(path, model, sourceLayerId));

    // ---- 真实导入链 ----
    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult imported = importArchive(path, scene, layers);
    ASSERT_TRUE(imported.success);
    ASSERT_TRUE(layers.hasProcessModel());

    const int layerId = layers.findLayerByName("Pierce");
    ASSERT_GE(layerId, 0);
    placeEntityOnLayer(scene, layers, "Pierce");

    // ---- OperationRegistrar 的真实取参路径 ----
    const ToolpathJobSpec spec = buildToolpathJobSpec(&layers, Eg::MachineFrameConfig{});
    ASSERT_EQ(1, spec.layerPierce.size());
    EXPECT_TRUE(spec.layerPierce.value(layerId).enabled);

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, &layers, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    // 2 次打孔延时 + 1 次稳定延时
    EXPECT_EQ(1, result.pierceAppliedContourCount);
    EXPECT_EQ(3, result.pierceDwellSegmentCount);
    EXPECT_EQ(3u, countDwell(builder.view()));

    const Hw::MotionPlanView view = builder.view();
    const long rapid = firstRapidIndex(view);
    ASSERT_GE(rapid, 0);
    // 穿孔点沿废料侧外移 2mm：矩形起点的外侧是 -Y
    EXPECT_NEAR(0.0, view.commands[rapid].target.v[0], 1e-4);
    EXPECT_NEAR(-2.0, view.commands[rapid].target.v[1], 1e-4);
    // 偏移切入段：出光回到轮廓起点（穿孔点之后的第一次出光直线）
    long firstCut = -1;
    for (long i = rapid + 1; i < static_cast<long>(view.commandCount); ++i)
    {
        if (view.commands[i].op == Hw::MotionOp::LineTo && view.commands[i].laserOn)
        {
            firstCut = i;
            break;
        }
    }
    ASSERT_GE(firstCut, 0) << "no laser-on line after pierce";
    EXPECT_NEAR(0.0, view.commands[firstCut].target.v[0], 1e-4);
    EXPECT_NEAR(0.0, view.commands[firstCut].target.v[1], 1e-4);
}

TEST(MotionPlanPierceEntryTest, NoDetailLeavesPierceOff)
{
    using namespace PierceEntry;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("no-detail.apr"));

    // 模型里没有任何层细节：入口不填 layerPierce，行为等价于「关闭」
    Eg::ProcessModel emptyModel;
    int sourceLayerId = -1;
    ASSERT_TRUE(writeArchiveWithModel(path, emptyModel, sourceLayerId));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult imported = importArchive(path, scene, layers);
    ASSERT_TRUE(imported.success);

    const ToolpathJobSpec spec = buildToolpathJobSpec(&layers, Eg::MachineFrameConfig{});
    EXPECT_TRUE(spec.layerPierce.isEmpty());
    EXPECT_FALSE(spec.defaultPierce.enabled);

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, &layers, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(0, result.pierceAppliedContourCount);
    EXPECT_EQ(0, result.pierceDwellSegmentCount);
    EXPECT_EQ(0u, countDwell(builder.view()));
}

// ============================================================
//  2. 本路径不支持的工艺细节：一次编译只汇总一条
// ============================================================

TEST(MotionPlanPierceEntryTest, UnsupportedDetailsReportedOncePerCompile)
{
    using namespace PierceEntry;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    Eg::ProcessModel model;
    Eg::LayerProcessEntry entry;
    entry.layerId = 0;
    entry.detail.hasCommonEdge = true;
    entry.detail.commonEdge.enabled = true;
    entry.detail.hasPowerCurve = true;
    entry.detail.powerCurve.enabled = true;
    entry.detail.hasCorner = true;
    entry.detail.corner.strategy = 1;
    entry.detail.hasPassStrategy = true;
    entry.detail.passStrategy.adaptiveThickness = true;
    model.layers.push_back(entry);
    // 作业级设置被本路径忽略：用非默认弦高触发 global
    model.global.chordToleranceMm = 0.02;

    const QString path = dir.filePath(QStringLiteral("unsupported-detail.apr"));
    int sourceLayerId = -1;
    ASSERT_TRUE(writeArchiveWithModel(path, model, sourceLayerId));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult imported = importArchive(path, scene, layers);
    ASSERT_TRUE(imported.success);
    ASSERT_TRUE(layers.hasProcessModel());

    ToolpathJobSpec spec = buildToolpathJobSpec(&layers, Eg::MachineFrameConfig{});
    // focusOffsetMm 目前不落盘（ProcessParamsData 无该字段），按真实运行期
    // 取值方式直接注入，验证编译器对它的告警。
    const int layerId = layers.findLayerByName("Pierce");
    ASSERT_GE(layerId, 0);
    placeEntityOnLayer(scene, layers, "Pierce");
    ASSERT_TRUE(spec.layerParams.contains(layerId));
    spec.layerParams[layerId].focusOffsetMm = 0.5;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, &layers, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();

    // 汇总成一条告警的载荷：每个被忽略项只出现一次，顺序固定
    const QStringList& ignored = result.ignoredProcessDetails;
    EXPECT_EQ(6, ignored.size());
    EXPECT_EQ(1, ignored.count(QStringLiteral("commonEdge")));
    EXPECT_EQ(1, ignored.count(QStringLiteral("powerCurve")));
    EXPECT_EQ(1, ignored.count(QStringLiteral("corner")));
    EXPECT_EQ(1, ignored.count(QStringLiteral("passStrategy")));
    EXPECT_EQ(1, ignored.count(QStringLiteral("global")));
    EXPECT_EQ(1, ignored.count(QStringLiteral("focusOffsetMm")));
    EXPECT_EQ(QStringLiteral("commonEdge, powerCurve, corner, passStrategy, global, focusOffsetMm"),
        ignored.join(QStringLiteral(", ")));
}

TEST(MotionPlanPierceEntryTest, SupportedDetailsProduceNoWarning)
{
    using namespace PierceEntry;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    Eg::ProcessModel model;
    Eg::LayerProcessEntry entry;
    entry.layerId = 0;
    entry.detail.hasPierce = true;
    entry.detail.pierce = pierceFixture();
    entry.detail.pierce.enabled = false;  // 只为验证「已配置但关闭」不触发告警
    model.layers.push_back(entry);

    const QString path = dir.filePath(QStringLiteral("supported-detail.apr"));
    int sourceLayerId = -1;
    ASSERT_TRUE(writeArchiveWithModel(path, model, sourceLayerId));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult imported = importArchive(path, scene, layers);
    ASSERT_TRUE(imported.success);

    const ToolpathJobSpec spec = buildToolpathJobSpec(&layers, Eg::MachineFrameConfig{});
    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, &layers, spec, builder);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(result.ignoredProcessDetails.isEmpty());
}

#endif  // ENABLE_HARDWARE
