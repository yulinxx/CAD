/**
 * @file MachiningOrderRoundTripTests.cpp
 * @brief 加工顺序端到端：保存采集 → .apr → 打开/追加导入 → LayerManager → 加工链路
 *
 *  - 自定义加工顺序随 .apr 往返，文档图层 ID 经 sourceToLayerId 换成运行时 ID
 *  - 打开不带加工顺序的文件：清掉上一份文档的自定义顺序
 *  - 追加导入：不改当前自定义顺序
 *  - MotionPlanCompiler / CAM 桥接按加工顺序（而非显示顺序）排列图层
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <QTemporaryDir>

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "FileIO/AprDocument.h"
#include "FileIO/AprSerializer.h"

#include "Composition/ImportExportServiceSetup.h"
#include "Export/ExportContext.h"
#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"
#include "Import/Readers/NativeImportReader.h"

#include "Hardware/ToolpathJobSpec.h"
#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"
#endif
#if defined(BUILD_CAM)
    #include "CAM/CamInputBridge.h"
    #include "CAM/ToolpathJobInput.h"
#endif

// Unity 构建会把多个测试源合并成一个 TU，帮助函数必须放进具名命名空间。
namespace MachiningOrderRoundTrip
{
    Eg::AprEntity* addSquare(Eg::SceneManager& scene, double x)
    {
        Eg::AprPolygon polygon;
        polygon.setVertices({ Ut::Vec2d(x, 0.0), Ut::Vec2d(x + 10.0, 0.0), Ut::Vec2d(x + 10.0, 10.0),
            Ut::Vec2d(x, 10.0) });
        polygon.bClosed = true;
        Eg::SceneWriteGateway::addEntity(scene, &polygon);
        return scene.getAllEntities().back();
    }

    /// 图层 names 各放一个图元；machining 非空时设为自定义加工顺序（按名字），然后走真实采集入口落盘
    bool writeArchive(const QString& path, const std::vector<std::string>& names,
        const std::vector<std::string>& machining)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);
        double x = 0.0;
        for (const std::string& name : names)
        {
            const int id = layers.createLayer(name);
            if (id < 0 || !layers.assignEntityToLayer(addSquare(scene, x), id))
            {
                return false;
            }
            x += 100.0;
        }
        if (!machining.empty())
        {
            std::vector<int> order;
            for (const std::string& name : machining)
            {
                order.push_back(layers.findLayerByName(name));
            }
            layers.setMachiningLayerOrder(order);
        }

        const NativeDocumentSnapshot snapshot = aprSetupCaptureMachiningSnapshot(&layers);
        Fio::AprDocument doc;
        for (const auto& layer : snapshot.layers)
        {
            doc.addLayer(layer);
        }
        for (const auto& profile : snapshot.profiles)
        {
            doc.addHardwareProfile(profile);
        }
        if (!snapshot.machiningLayerOrder.empty())
        {
            doc.setMachiningLayerOrder(snapshot.machiningLayerOrder.data(), snapshot.machiningLayerOrder.size());
        }
        for (Eg::AprEntity* entity : scene.getAllEntities())
        {
            doc.addBorrowedEntity(entity);
        }
        Fio::AprSerializer serializer;
        return serializer.saveToFile(path.toUtf8().constData(), doc, false, Fio::FileFormat::Native).success;
    }

    ImportResult importArchive(const QString& path, Eg::SceneManager& scene, LayerManager& layers, bool asNewDocument)
    {
        ImportDispatcher dispatcher;
        dispatcher.registerReader(std::make_unique<NativeImportReader>());
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
        options.importAsNewDocument = asNewDocument;
        return service.importWithContext(context, options);
    }

    /// 加工顺序里属于 names 的那些图层，按加工顺序给出名字
    std::vector<std::string> machiningNames(const LayerManager& layers, const std::vector<std::string>& names)
    {
        std::vector<std::string> out;
        for (int id : layers.machiningLayerOrder())
        {
            const std::string name = layers.layerName(id);
            if (std::find(names.begin(), names.end(), name) != names.end())
            {
                out.push_back(name);
            }
        }
        return out;
    }
}  // namespace MachiningOrderRoundTrip

TEST(MachiningOrderRoundTripTest, CustomOrderSurvivesSaveAndOpen)
{
    using namespace MachiningOrderRoundTrip;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("order.apr"));
    ASSERT_TRUE(writeArchive(path, { "Cut", "Engrave", "Mark" }, { "Mark", "Cut", "Engrave" }));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult result = importArchive(path, scene, layers, true);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    EXPECT_TRUE(layers.hasCustomMachiningOrder());
    EXPECT_EQ(machiningNames(layers, { "Cut", "Engrave", "Mark" }),
        (std::vector<std::string>{ "Mark", "Cut", "Engrave" }));
    // 显示顺序不受影响
    std::vector<std::string> display;
    for (int id : layers.getAllLayerIds())
    {
        const std::string n = layers.layerName(id);
        if (n == "Cut" || n == "Engrave" || n == "Mark")
        {
            display.push_back(n);
        }
    }
    EXPECT_EQ(display, (std::vector<std::string>{ "Cut", "Engrave", "Mark" }));
}

TEST(MachiningOrderRoundTripTest, SaveWithoutCustomOrderWritesNothing)
{
    Eg::SceneManager scene;
    LayerManager layers(&scene);
    layers.createLayer("Cut");
    EXPECT_TRUE(aprSetupCaptureMachiningSnapshot(&layers).machiningLayerOrder.empty());
}

TEST(MachiningOrderRoundTripTest, OpeningFileWithoutOrderClearsPreviousCustomOrder)
{
    using namespace MachiningOrderRoundTrip;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plain.apr"));
    ASSERT_TRUE(writeArchive(path, { "Cut", "Mark" }, {}));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const int previous = layers.createLayer("Previous");
    ASSERT_TRUE(layers.setMachiningLayerOrder({ previous }));

    ASSERT_TRUE(importArchive(path, scene, layers, true).success);
    EXPECT_FALSE(layers.hasCustomMachiningOrder());
}

TEST(MachiningOrderRoundTripTest, AppendImportKeepsCurrentOrder)
{
    using namespace MachiningOrderRoundTrip;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("append.apr"));
    ASSERT_TRUE(writeArchive(path, { "Cut", "Mark" }, { "Mark", "Cut" }));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const int mine = layers.createLayer("Mine");
    // 追加导入按颜色落层：Mine 换一个文件里没有的颜色，免得 Cut 的图元落进 Mine
    ASSERT_TRUE(layers.setLayerColor(mine, Ut::Color::fromRGB255(1, 2, 3)));
    ASSERT_TRUE(layers.assignEntityToLayer(addSquare(scene, 500.0), mine));
    ASSERT_TRUE(layers.setMachiningLayerOrder({ mine }));

    ASSERT_TRUE(importArchive(path, scene, layers, false).success);
    EXPECT_EQ(layers.machiningLayerOrder().front(), mine);
    // 新建的颜色层（名字取源图层名）排在加工顺序末尾，没有被漏掉
    EXPECT_EQ(machiningNames(layers, { "Cut", "Mark" }).size(), 2u);
}

#ifdef ENABLE_HARDWARE
TEST(MachiningOrderRoundTripTest, MotionPlanFollowsMachiningOrder)
{
    using namespace MachiningOrderRoundTrip;
    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const int cut = layers.createLayer("Cut");
    const int engrave = layers.createLayer("Engrave");  // 显示顺序：Cut 在前
    ASSERT_TRUE(layers.assignEntityToLayer(addSquare(scene, 0.0), cut));
    ASSERT_TRUE(layers.assignEntityToLayer(addSquare(scene, 100.0), engrave));
    ASSERT_TRUE(layers.setMachiningLayerOrder({ engrave, cut }));  // 先雕后切

    ToolpathJobSpec spec;
    spec.layerParams.insert(cut, LayerProcessParams{});
    spec.layerParams.insert(engrave, LayerProcessParams{});
    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult r = MotionPlanCompiler::compile(scene, &layers, spec, builder);
    ASSERT_TRUE(r.ok) << r.error.toStdString();

    std::vector<int> begun;
    const Hw::MotionPlanView view = builder.view();
    for (size_t i = 0; i < view.commandCount; ++i)
    {
        if (view.commands[i].op == Hw::MotionOp::BeginLayer)
        {
            begun.push_back(view.commands[i].intArg);
        }
    }
    EXPECT_EQ(begun, (std::vector<int>{ engrave, cut }));
}
#endif  // ENABLE_HARDWARE

#if defined(BUILD_CAM)
TEST(MachiningOrderRoundTripTest, CamInputFollowsMachiningOrder)
{
    using namespace MachiningOrderRoundTrip;
    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const int cut = layers.createLayer("Cut");
    const int engrave = layers.createLayer("Engrave");
    ASSERT_TRUE(layers.assignEntityToLayer(addSquare(scene, 0.0), cut));
    ASSERT_TRUE(layers.assignEntityToLayer(addSquare(scene, 100.0), engrave));
    ASSERT_TRUE(layers.setMachiningLayerOrder({ engrave, cut }));

    const Cam::ToolpathJobInput input = CamBridge::buildJobInput(scene, &layers, ToolpathJobSpec{});
    std::vector<int> ids;
    for (const auto& lb : input.layers)
    {
        if (lb.layerId == cut || lb.layerId == engrave)
        {
            ids.push_back(lb.layerId);
        }
    }
    EXPECT_EQ(ids, (std::vector<int>{ engrave, cut }));
}
#endif  // BUILD_CAM
