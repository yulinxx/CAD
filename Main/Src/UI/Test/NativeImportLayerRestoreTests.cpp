/**
 * @file NativeImportLayerRestoreTests.cpp
 * @brief 原生 .apr 导入的图层归属回归。
 *
 * 背景：
 *   NativeImportReader 用 clone() 把文档图元交给导入链，而 clone() 刻意生成新 ID
 *   （id 是身份不是状态）。但 ImportService 的换层/换组映射以「文档持久 ID」为键，
 *   于是整批映射都查不到图元，导入后全部落到默认图层。
 *
 * 本测试锁定：
 *   1. 多图层文档导入后，每个图元落在原图层，且没有一个图元落到默认图层；
 *   2. 导入进非空文档时，与既有图元撞号的档案图元仍能按原图层还原；
 *   3. 档案内部重复 ID 不会让图元被静默丢弃（确定性：走同一条换层路径）。
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
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "FileIO/AprDocument.h"
#include "FileIO/AprSerializer.h"

#include "Composition/ImportExportServiceSetup.h"
#include "Export/ExportContext.h"
#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"
#include "Import/Readers/NativeImportReader.h"

// Unity 构建会把多个测试源合并成一个 TU，帮助函数必须放进具名命名空间。
namespace NativeImportLayerRestore
{
    /// 写一份原生 .apr：图层名由 names 给出，members[i] 是第 i 个图元所属图层下标，
    /// xs[i] 是第 i 个矩形左边界（用于事后区分成员），ids[i] 是写入档案的持久 ID。
    bool writeArchive(const QString& path,
        const std::vector<std::string>& names,
        const std::vector<int>& members,
        const std::vector<double>& xs,
        const std::vector<Eg::EntityId>& ids)
    {
        if (names.empty() || members.size() != xs.size() || xs.size() != ids.size())
        {
            return false;
        }

        Eg::SceneManager scene;
        LayerManager layers(&scene);

        std::vector<int> layerIds;
        layerIds.reserve(names.size());
        for (const std::string& name : names)
        {
            const int layerId = layers.createLayer(name);
            if (layerId < 0)
            {
                return false;
            }
            layerIds.push_back(layerId);
        }

        for (size_t i = 0; i < xs.size(); ++i)
        {
            if (members[i] < 0 || members[i] >= static_cast<int>(layerIds.size()))
            {
                return false;
            }

            Eg::AprPolygon polygon;
            polygon.setVertices({ Ut::Vec2d(xs[i], 0.0), Ut::Vec2d(xs[i] + 10.0, 0.0),
                Ut::Vec2d(xs[i] + 10.0, 10.0), Ut::Vec2d(xs[i], 10.0) });
            polygon.bClosed = true;
            Eg::SceneWriteGateway::addEntity(scene, &polygon);

            Eg::AprEntity* entity = scene.getAllEntities().back();
            entity->id = ids[i];
            if (!layers.assignEntityToLayer(entity, layerIds[members[i]]))
            {
                return false;
            }
        }

        NativeDocumentSnapshot snapshot = aprSetupCaptureMachiningSnapshot(&layers);

        // 一次性直接落盘：图元从场景借用，图层表来自快照。
        // 不走「导出 → 再解析 → 再序列化」，反序列化不回写图元的 layerId，二次落盘会丢归属。
        Fio::AprDocument doc;
        for (const auto& layer : snapshot.layers)
        {
            doc.addLayerDetail(layer);
        }
        for (const auto& profile : snapshot.profiles)
        {
            doc.addHardwareProfile(profile);
        }
        for (Eg::AprEntity* entity : scene.getAllEntities())
        {
            doc.addBorrowedEntity(entity);
        }

        Fio::AprSerializer serializer;
        return serializer.saveToFile(path.toUtf8().constData(), doc, false, Fio::FileFormat::Native).success;
    }

    /// 真实导入链：NativeImportReader → ImportService → LayerManager
    ImportResult importArchive(const QString& path, Eg::SceneManager& scene, LayerManager& layers,
        bool mergeIntoScene = false)
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
        // 默认按新文档导入会清空场景；合并场景时保留既有图元
        options.importAsNewDocument = !mergeIntoScene;

        return service.importWithContext(context, options);
    }

    /// 取某图层上所有图元的左边界 x（排序后返回），用于断言成员归属。
    std::vector<double> xsOnLayer(const LayerManager& layers, const char* name)
    {
        const int layerId = layers.findLayerByName(name);
        EXPECT_GE(layerId, 0) << name;

        std::vector<double> xs;
        for (Eg::AprEntity* entity : layers.getLayerEntities(layerId))
        {
            auto* polygon = dynamic_cast<Eg::AprPolygon*>(entity);
            EXPECT_NE(nullptr, polygon) << name;
            if (polygon && !polygon->vertices().empty())
            {
                xs.push_back(polygon->vertices().front().x());
            }
        }
        std::sort(xs.begin(), xs.end());
        return xs;
    }
}  // namespace NativeImportLayerRestore

TEST(NativeImportLayerRestoreTest, MultiLayerArchiveRestoresEveryEntityToItsLayer)
{
    using namespace NativeImportLayerRestore;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("layers.apr"));

    ASSERT_TRUE(writeArchive(path, { "Cut", "Mark", "Score" }, { 0, 0, 1, 2 },
        { 0.0, 100.0, 200.0, 300.0 }, { 1000, 1001, 1002, 1003 }));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult result = importArchive(path, scene, layers);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    ASSERT_EQ(4u, scene.getAllEntities().size());

    // 逐图层断言成员归属（Cut 两个、Mark 一个、Score 一个）
    EXPECT_EQ((std::vector<double>{ 0.0, 100.0 }), xsOnLayer(layers, "Cut"));
    EXPECT_EQ((std::vector<double>{ 200.0 }), xsOnLayer(layers, "Mark"));
    EXPECT_EQ((std::vector<double>{ 300.0 }), xsOnLayer(layers, "Score"));

    // 默认图层不能兜底接收任何导入图元
    EXPECT_TRUE(layers.getLayerEntities(0).empty());
    for (Eg::AprEntity* entity : scene.getAllEntities())
    {
        EXPECT_NE(0, layers.getEntityLayer(entity)) << "entity id=" << entity->id;
    }
}

TEST(NativeImportLayerRestoreTest, IdConflictWithExistingSceneKeepsLayerMapping)
{
    using namespace NativeImportLayerRestore;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("layers.apr"));

    // 档案里第一个图元 ID=1000，与下面预置的图元撞号
    ASSERT_TRUE(writeArchive(path, { "Cut" }, { 0, 0 }, { 0.0, 100.0 }, { 1000, 1001 }));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const int existingLayer = layers.findLayerByName("Layer 1");
    ASSERT_GE(existingLayer, 0);

    auto blocker = std::make_unique<Eg::AprPolygon>();
    blocker->id = 1000;
    blocker->setVertices({ Ut::Vec2d(500.0, 0.0), Ut::Vec2d(510.0, 0.0),
        Ut::Vec2d(510.0, 10.0), Ut::Vec2d(500.0, 10.0) });
    blocker->bClosed = true;
    Eg::AprEntity* blockerRef = blocker.get();
    ASSERT_TRUE(Eg::SceneWriteGateway::insertEntityPreserveId(scene, std::move(blocker)));
    ASSERT_TRUE(layers.assignEntityToLayer(blockerRef, existingLayer));

    const ImportResult result = importArchive(path, scene, layers, true);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    ASSERT_EQ(3u, scene.getAllEntities().size());

    // 既有图元不被顶掉，且仍留在自己的图层
    EXPECT_EQ(1000, blockerRef->id);
    EXPECT_EQ(existingLayer, layers.getEntityLayer(blockerRef));

    // 撞号的那条档案图元被重新分配 ID，但图层归属不能丢
    EXPECT_EQ((std::vector<double>{ 0.0, 100.0 }), xsOnLayer(layers, "Cut"));
    EXPECT_TRUE(layers.getLayerEntities(0).empty());
}

TEST(NativeImportLayerRestoreTest, DuplicateIdsInsideArchiveDoNotDropEntities)
{
    using namespace NativeImportLayerRestore;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("dup.apr"));

    // 档案内部重复 ID：两个图元都写 1000，分属两个图层
    ASSERT_TRUE(writeArchive(path, { "Cut", "Mark" }, { 0, 1 }, { 0.0, 100.0 }, { 1000, 1000 }));

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    const ImportResult result = importArchive(path, scene, layers);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    // 不丢图元：两条都入库，重复 ID 里的一条被重新分配
    ASSERT_EQ(2u, scene.getAllEntities().size());
    const Eg::EntityId firstId = scene.getAllEntities()[0]->id;
    const Eg::EntityId secondId = scene.getAllEntities()[1]->id;
    EXPECT_NE(firstId, secondId);

    // 重复 ID 在文档里只能解析出一个图层归属（后写覆盖），两条必须同层且不落默认层
    const int cutId = layers.findLayerByName("Cut");
    const int markId = layers.findLayerByName("Mark");
    ASSERT_GE(cutId, 0);
    ASSERT_GE(markId, 0);
    EXPECT_EQ(size_t{ 2 }, layers.getLayerEntities(markId).size());
    EXPECT_TRUE(layers.getLayerEntities(cutId).empty());
    EXPECT_TRUE(layers.getLayerEntities(0).empty());
}
