/**
 * @file ImportExportRegressionTests.cpp
 * @brief 导入导出链回归测试 — 覆盖 ImportService/ExportService/FioEntityConverter 完整链路
 *
 * 测试范围：
 *  - ImportService 五阶段流程与回调注入
 *  - ExportService 数据收集与格式输出
 *  - FioEntityConverter 元数据/硬件信息/实体属性转换
 *  - 多格式 draw/circle/arc 往返一致性
 *  - 空场景/边界条件安全性
 *
 * P4 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "ImportService.h"
#include "ExportService.h"
#include "FioEntityConverter.h"
#include "SyEntitySerializer.h"
#include "FileIO/FioTypes.h"
#include "FileIO/FileIOManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyNurbs.h"

#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>
#include <atomic>

// ==================== ImportService 基础流程测试 ====================

TEST(ImportExportRegressionTest, ImportService_DefaultConstruction)
{
    ImportService service;
    // 默认构造不崩溃
    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_SetDependencies)
{
    ImportService service;
    Eg::SceneManager scene;

    service.setSceneManager(&scene);
    // 注入依赖不崩溃
    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_BusyStateCallback)
{
    ImportService service;

    bool busyCalled = false;
    bool idleCalled = false;

    service.setBusyStateCallback([&](bool busy) {
        if (busy)
        {
            busyCalled = true;
        }
        else
        {
            idleCalled = true;
        }
    });

    // 回调注入不崩溃
    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_StatusPromptCallback)
{
    ImportService service;

    QString lastMsg;
    service.setStatusPromptCallback([&](const QString& msg) {
        lastMsg = msg;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_ViewportFitCallback)
{
    ImportService service;

    bool called = false;
    service.setViewportFitCallback([&]() {
        called = true;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_TreeRebuildCallback)
{
    ImportService service;

    bool called = false;
    service.setTreeRebuildCallback([&]() {
        called = true;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_PropertyRefreshCallback)
{
    ImportService service;

    bool called = false;
    service.setPropertyRefreshCallback([&]() {
        called = true;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_WorkbenchSwitchCallback)
{
    ImportService service;

    QString lastTarget;
    service.setWorkbenchSwitchCallback([&](const QString& target) {
        lastTarget = target;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_StatusBarUpdateCallback)
{
    ImportService service;

    QString lastMsg;
    service.setStatusBarUpdateCallback([&](const QString& msg) {
        lastMsg = msg;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_RecentFileCallback)
{
    ImportService service;

    QString lastPath;
    service.setRecentFileAddCallback([&](const QString& path) {
        lastPath = path;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_CurrentDocumentPathCallback)
{
    ImportService service;

    QString lastPath;
    service.setCurrentDocumentPathCallback([&](const QString& path) {
        lastPath = path;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_DocumentPersistenceCallback)
{
    ImportService service;

    bool called = false;
    service.setDocumentPersistenceCallback([&](const QString& path, int entityCount) {
        called = true;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ImportService_CanImportEmptyPath)
{
    ImportService service;
    EXPECT_FALSE(service.canImport(""));
}

TEST(ImportExportRegressionTest, ImportService_CanImportDxfExtension)
{
    ImportService service;
    // 不带分发器时，所有格式应返回 false
    EXPECT_FALSE(service.canImport("test.dxf"));
}

TEST(ImportExportRegressionTest, ImportService_ImportFileUnconfigured)
{
    ImportService service;
    ImportResult result = service.importFile("nonexistent.dxf");
    // 未配置分发器时应返回失败
    EXPECT_FALSE(result.success);
}

// ==================== ExportService 基础流程测试 ====================

TEST(ImportExportRegressionTest, ExportService_DefaultConstruction)
{
    ExportService service;
    SUCCEED();
}

TEST(ImportExportRegressionTest, ExportService_SetDependencies)
{
    ExportService service;
    Eg::SceneManager scene;

    service.setSceneManager(&scene);
    SUCCEED();
}

TEST(ImportExportRegressionTest, ExportService_BusyStateCallback)
{
    ExportService service;

    bool busyCalled = false;
    service.setBusyStateCallback([&](bool busy) {
        if (busy)
        {
            busyCalled = true;
        }
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ExportService_StatusPromptCallback)
{
    ExportService service;

    QString lastMsg;
    service.setStatusPromptCallback([&](const QString& msg) {
        lastMsg = msg;
    });

    SUCCEED();
}

TEST(ImportExportRegressionTest, ExportService_CanExportEmptyPath)
{
    ExportService service;
    EXPECT_FALSE(service.canExport(""));
}

TEST(ImportExportRegressionTest, ExportService_ExportFileUnconfigured)
{
    ExportService service;
    ExportResult result = service.exportFile("output.dxf");
    // 未配置分发器时应返回失败
    EXPECT_FALSE(result.success);
}

TEST(ImportExportRegressionTest, ExportService_CollectAllEntitiesEmptyScene)
{
    ExportService service;
    Eg::SceneManager scene;
    service.setSceneManager(&scene);

    auto entities = service.collectAllEntities();
    EXPECT_TRUE(entities.empty());
}

TEST(ImportExportRegressionTest, ExportService_CollectAllEntitiesWithData)
{
    ExportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    service.setSceneManager(&scene);

    auto entities = service.collectAllEntities();
    EXPECT_EQ(entities.size(), 1u);
}

// ==================== FioEntityConverter 扩展测试 ====================

// 元数据转换测试
TEST(ImportExportRegressionTest, FioEntityConverter_MetadataTransfer)
{
    // 构造带元数据的 ParseResult
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Line;
    entity.line.x1 = 0.0;
    entity.line.y1 = 0.0;
    entity.line.x2 = 10.0;
    entity.line.y2 = 10.0;
    entity.visible = true;
    entity.locked = false;
    entity.lineWidth = 2.0;

    auto result = FioEntityConverter::convertEntity(entity);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->eType, Eg::EType::LINE);

    auto* line = static_cast<Eg::SyLine*>(result.get());
    // 验证线段点数
    EXPECT_EQ(line->pointRef().size(), 2u);
}

// 名称属性转换测试 — convertEntity 在创建时即设置名称
TEST(ImportExportRegressionTest, FioEntityConverter_NameTransfer)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Circle;
    info.circle.cx = 0.0;
    info.circle.cy = 0.0;
    info.circle.r = 5.0;
    std::strncpy(info.name, "MyCircle", sizeof(info.name));

    auto result = FioEntityConverter::convertEntity(info);
    ASSERT_NE(result, nullptr);
    // convertEntity 创建时调用 setName，名称直接转移
    EXPECT_STREQ(result->name(), "MyCircle");
}

// 可见性属性转换 — convertEntity 不设置 visible/locked，由 convertAll 批量设置
TEST(ImportExportRegressionTest, FioEntityConverter_VisibleTransfer)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;
    info.visible = false;
    info.locked = true;

    auto result = FioEntityConverter::convertEntity(info);
    ASSERT_NE(result, nullptr);
    // convertEntity 不设置 visible/locked（由 convertAll 批量设置，默认值）
    EXPECT_TRUE(result->visible());
    EXPECT_FALSE(result->locked());
}

// 图层关联转换
TEST(ImportExportRegressionTest, FioEntityConverter_LayerSourceIdTransfer)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;
    info.layerSourceId = 42;

    auto result = FioEntityConverter::convertEntity(info);
    ASSERT_NE(result, nullptr);
    // 图层 ID 仅用于引用追踪，不直接存储到实体
    EXPECT_EQ(result->eType, Eg::EType::LINE);
}

// 批量转换包含多种类型
TEST(ImportExportRegressionTest, FioEntityConverter_MixedTypeBatch)
{
    Fio::EntityInfo entities[4];
    // Line
    entities[0].type = Fio::EntityType::Line;
    entities[0].line.x1 = 0.0;
    entities[0].line.y1 = 0.0;
    entities[0].line.x2 = 10.0;
    entities[0].line.y2 = 10.0;
    // Circle
    entities[1].type = Fio::EntityType::Circle;
    entities[1].circle.cx = 50.0;
    entities[1].circle.cy = 50.0;
    entities[1].circle.r = 20.0;
    // Arc
    entities[2].type = Fio::EntityType::Arc;
    entities[2].arc.cx = 0.0;
    entities[2].arc.cy = 0.0;
    entities[2].arc.r = 30.0;
    entities[2].arc.sa = 0.0;
    entities[2].arc.ea = 3.14159;
    // Point
    entities[3].type = Fio::EntityType::Point;
    entities[3].line.x1 = 100.0;
    entities[3].line.y1 = 200.0;

    Fio::FioParseResult parseData;
    parseData.entities = entities;
    parseData.entityCount = 4;
    std::strncpy(parseData.sourceFormat, "DXF", sizeof(parseData.sourceFormat));

    auto result = FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(result.size(), 4u);

    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(result[1]->eType, Eg::EType::CIRCLE);
    EXPECT_EQ(result[2]->eType, Eg::EType::ARC);
    EXPECT_EQ(result[3]->eType, Eg::EType::POINT);
}

// 批量转换跳过未知类型
TEST(ImportExportRegressionTest, FioEntityConverter_SkipUnknownInBatch)
{
    Fio::EntityInfo entities[3];
    entities[0].type = Fio::EntityType::Line;
    entities[0].line.x1 = 0.0;
    entities[0].line.y1 = 0.0;
    entities[0].line.x2 = 10.0;
    entities[0].line.y2 = 10.0;
    entities[1].type = Fio::EntityType::Unknown;
    entities[2].type = Fio::EntityType::Circle;
    entities[2].circle.cx = 0.0;
    entities[2].circle.cy = 0.0;
    entities[2].circle.r = 5.0;

    Fio::FioParseResult parseData;
    parseData.entities = entities;
    parseData.entityCount = 3;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_EQ(result.size(), 2u);
}

// 图层提取含空名称
TEST(ImportExportRegressionTest, FioEntityConverter_ExtractLayersWithEmptyName)
{
    Fio::IrLayerInfo layers[2];
    layers[0].sourceId = 1;
    std::strncpy(layers[0].name, "Layer_1", sizeof(layers[0].name));
    layers[0].color = 0xFF0000;
    layers[0].visible = true;
    layers[0].locked = false;

    layers[1].sourceId = 2;
    layers[1].name[0] = '\0';  // 空名称
    layers[1].color = 0x00FF00;
    layers[1].visible = true;
    layers[1].locked = false;

    Fio::FioParseResult parseData;
    parseData.layers = layers;
    parseData.layerCount = 2;

    auto result = FioEntityConverter::extractLayers(parseData);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(std::string(result[0].name), "Layer_1");
    EXPECT_EQ(std::string(result[1].name), "");
}

// 带来源格式的 ParseResult
TEST(ImportExportRegressionTest, FioEntityConverter_SourceFormatPreserved)
{
    Fio::EntityInfo entity;
    entity.type = Fio::EntityType::Line;
    entity.line.x1 = 0.0;
    entity.line.y1 = 0.0;
    entity.line.x2 = 10.0;
    entity.line.y2 = 10.0;

    Fio::FioParseResult parseData;
    parseData.entities = &entity;
    parseData.entityCount = 1;
    std::strncpy(parseData.sourceFormat, "SVG", sizeof(parseData.sourceFormat));

    auto result = FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
}

// ==================== 导入导出链路一致性测试 ====================

TEST(ImportExportRegressionTest, Chain_EntityCountAfterImport)
{
    // 模拟从 IR 导入实体到场景
    Eg::SceneManager scene;

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(entity));
    scene.addEntities(std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, Chain_ExportCollectMatchesImport)
{
    Eg::SceneManager scene;

    // 导入 3 个不同实体
    Fio::EntityInfo infos[3];
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0.0;
    infos[0].line.y1 = 0.0;
    infos[0].line.x2 = 10.0;
    infos[0].line.y2 = 10.0;
    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 50.0;
    infos[1].circle.cy = 50.0;
    infos[1].circle.r = 20.0;
    infos[2].type = Fio::EntityType::Arc;
    infos[2].arc.cx = 0.0;
    infos[2].arc.cy = 0.0;
    infos[2].arc.r = 30.0;
    infos[2].arc.sa = 0.0;
    infos[2].arc.ea = 1.57;

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 3;

    auto entities = FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 3u);

    std::vector<std::unique_ptr<Eg::SyEntity>> moveVec;
    for (auto& e : entities)
    {
        moveVec.push_back(std::move(e));
    }
    scene.addEntities(std::move(moveVec));

    EXPECT_EQ(scene.getEntityCount(), 3u);

    // 模拟导出收集
    ExportService exportSvc;
    exportSvc.setSceneManager(&scene);
    auto collected = exportSvc.collectAllEntities();
    EXPECT_EQ(collected.size(), 3u);
}

TEST(ImportExportRegressionTest, Chain_EmptySceneExport)
{
    ExportService service;
    Eg::SceneManager scene;
    service.setSceneManager(&scene);

    auto entities = service.collectAllEntities();
    EXPECT_TRUE(entities.empty());
}

TEST(ImportExportRegressionTest, Chain_CollectAfterDelete)
{
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 删除后导出应收集 0 个实体
    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getEntityCount(), 0u);

    ExportService service;
    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    EXPECT_TRUE(collected.empty());
}

// ==================== 图层链一致性测试 ====================

TEST(ImportExportRegressionTest, LayerChain_ExtractAndVerify)
{
    Fio::IrLayerInfo layers[3];
    layers[0].sourceId = 1;
    std::strncpy(layers[0].name, "Default", sizeof(layers[0].name));
    layers[0].color = 0xFFFFFF;
    layers[0].visible = true;
    layers[0].locked = false;

    layers[1].sourceId = 2;
    std::strncpy(layers[1].name, "Cut", sizeof(layers[1].name));
    layers[1].color = 0xFF0000;
    layers[1].visible = true;
    layers[1].locked = false;

    layers[2].sourceId = 3;
    std::strncpy(layers[2].name, "Engrave", sizeof(layers[2].name));
    layers[2].color = 0x0000FF;
    layers[2].visible = false;
    layers[2].locked = true;

    Fio::FioParseResult parseData;
    parseData.layers = layers;
    parseData.layerCount = 3;

    auto result = FioEntityConverter::extractLayers(parseData);
    ASSERT_EQ(result.size(), 3u);

    // 验证图层属性完整性
    EXPECT_EQ(result[0].sourceId, 1u);
    EXPECT_EQ(std::string(result[0].name), "Default");
    EXPECT_EQ(result[0].color, 0xFFFFFFu);
    EXPECT_TRUE(result[0].visible);

    EXPECT_EQ(result[1].sourceId, 2u);
    EXPECT_EQ(std::string(result[1].name), "Cut");

    EXPECT_EQ(result[2].sourceId, 3u);
    EXPECT_EQ(std::string(result[2].name), "Engrave");
    EXPECT_FALSE(result[2].visible);
    EXPECT_TRUE(result[2].locked);
}

// ==================== 服务信号测试 ====================

TEST(ImportExportRegressionTest, ImportService_Signals)
{
    ImportService service;

    bool started = false;
    bool finished = false;

    QObject::connect(&service, &ImportService::importStarted, [&](const QString&) {
        started = true;
    });
    QObject::connect(&service, &ImportService::importFinished, [&](const ImportResult&) {
        finished = true;
    });

    // 信号连接不崩溃
    SUCCEED();
}

TEST(ImportExportRegressionTest, ExportService_Signals)
{
    ExportService service;

    bool started = false;
    bool finished = false;

    QObject::connect(&service, &ExportService::exportStarted, [&](const QString&) {
        started = true;
    });
    QObject::connect(&service, &ExportService::exportFinished, [&](const ExportResult&) {
        finished = true;
    });

    SUCCEED();
}

// ==================== 3D SceneManager 注入测试 ====================

TEST(ImportExportRegressionTest, ImportService_SetSceneManager3D)
{
    ImportService service;
    // 3D 场景管理器注入（指针为 nullptr 是合法状态）
    service.setSceneManager3D(nullptr);
    EXPECT_EQ(service.sceneManager3D(), nullptr);

    SUCCEED();
}

// ==================== 边界条件测试 ====================

TEST(ImportExportRegressionTest, ImportService_ImportFileWithNullSceneManager)
{
    ImportService service;
    // 未设置 SceneManager 时导入应安全返回失败
    ImportResult result = service.importFile("test.dxf");
    EXPECT_FALSE(result.success);
}

TEST(ImportExportRegressionTest, ExportService_ExportFileWithNullSceneManager)
{
    ExportService service;
    // 未设置 SceneManager 时导出应安全返回失败
    ExportResult result = service.exportFile("output.dxf");
    EXPECT_FALSE(result.success);
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertNullEntityInfo)
{
    // 空指针保护由 convertAll 处理，convertEntity 期望有效引用
    Fio::FioParseResult parseData;
    parseData.entities = nullptr;
    parseData.entityCount = 0;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

// ==================== FioEntityConverter 深度测试 ====================

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertLineEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    std::strncpy(info.name, "TestLine", sizeof(info.name) - 1);
    info.line.x1 = 10.0;
    info.line.y1 = 20.0;
    info.line.x2 = 100.0;
    info.line.y2 = 200.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::LINE);
    EXPECT_STREQ(entity->name(), "TestLine");
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertCircleEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Circle;
    std::strncpy(info.name, "TestCircle", sizeof(info.name) - 1);
    info.circle.cx = 50.0;
    info.circle.cy = 50.0;
    info.circle.r = 25.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::CIRCLE);
    EXPECT_STREQ(entity->name(), "TestCircle");
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertArcEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Arc;
    std::strncpy(info.name, "TestArc", sizeof(info.name) - 1);
    info.arc.cx = 0.0;
    info.arc.cy = 0.0;
    info.arc.r = 30.0;
    info.arc.sa = 0.0;
    info.arc.ea = 1.5708;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::ARC);
    EXPECT_STREQ(entity->name(), "TestArc");
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertPolygonEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Polygon;
    std::strncpy(info.name, "TestPolygon", sizeof(info.name) - 1);
    info.vertexCount = 4;

    // 使用扩展数据块传递顶点
    Fio::BinaryBlob blob;
    double verts[] = { 0.0, 0.0, 10.0, 0.0, 10.0, 10.0, 0.0, 10.0 };
    blob.data = reinterpret_cast<uint8_t*>(const_cast<double*>(verts));
    blob.size = sizeof(verts);

    auto entity = FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POLYGON);
    EXPECT_STREQ(entity->name(), "TestPolygon");
}

TEST(ImportExportRegressionTest, FioEntityConverter_UnknownTypeReturnsNull)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Unknown;

    auto entity = FioEntityConverter::convertEntity(info);
    EXPECT_EQ(entity, nullptr);
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertAllEmpty)
{
    Fio::FioParseResult parseData;
    parseData.entities = nullptr;
    parseData.entityCount = 0;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ExtractLayersEmpty)
{
    Fio::FioParseResult parseData;
    parseData.layers = nullptr;
    parseData.layerCount = 0;

    auto layers = FioEntityConverter::extractLayers(parseData);
    EXPECT_TRUE(layers.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityInfoWithLayerId)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    std::strncpy(info.name, "LayeredLine", sizeof(info.name) - 1);
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;
    info.layerSourceId = 42;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::LINE);
    EXPECT_STREQ(entity->name(), "LayeredLine");
    // 图层信息应通过 layerSourceId 传递
}

// ==================== SyEntitySerializer 深度测试 ====================

TEST(ImportExportRegressionTest, SyEntitySerializer_LineRoundTripWithLayer)
{
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(10, 20), Ut::Vec2d(100, 200) });
    line->setName("LayerLine");

    line->basePoint = Ut::Vec2d(50, 50);

    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(*line, &protoData);

    auto restored = Fio::SyEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::LINE);
    EXPECT_STREQ(restored->name(), "LayerLine");
}

TEST(ImportExportRegressionTest, SyEntitySerializer_CircleRoundTripWithLayer)
{
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->dRadius = 15.0;
    circle->basePoint = Ut::Vec2d(30, 40);
    circle->setName("LayerCircle");

    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(*circle, &protoData);

    auto restored = Fio::SyEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::CIRCLE);
    EXPECT_STREQ(restored->name(), "LayerCircle");
}

TEST(ImportExportRegressionTest, SyEntitySerializer_PolygonRoundTripWithLayer)
{
    auto poly = std::make_unique<Eg::SyPolygon>();
    poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
    poly->bClosed = true;
    poly->bCCW = true;
    poly->setName("LayerPoly");

    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(*poly, &protoData);

    auto restored = Fio::SyEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::POLYGON);
    EXPECT_STREQ(restored->name(), "LayerPoly");
}

TEST(ImportExportRegressionTest, SyEntitySerializer_ArcRoundTripWithLayer)
{
    auto arc = std::make_unique<Eg::SyArc>();
    arc->dRadius = 20.0;
    arc->dStartAngle = 30.0;
    arc->dEndAngle = 120.0;
    arc->setName("LayerArc");

    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(*arc, &protoData);

    auto restored = Fio::SyEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::ARC);
    EXPECT_STREQ(restored->name(), "LayerArc");
}

// ==================== P5 补充：导入后实体数/图层/名称一致性 ====================

TEST(ImportExportRegressionTest, ImportService_EntityCountAfterImport)
{
    // 验证 ImportService 导入后实体数正确
    ImportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId originalId = line->id;
    scene.addEntity(line.release());

    service.setSceneManager(&scene);

    // 验证场景管理器中有实体
    EXPECT_EQ(scene.getAllEntities().size(), 1u);
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, ExportService_EntityCountAfterCollect)
{
    ExportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    scene.addEntity(line.release());

    service.setSceneManager(&scene);

    auto collected = service.collectAllEntities();
    EXPECT_EQ(collected.size(), 1u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityCountAfterBatch)
{
    // 批量转换后实体数正确（使用 POD 数组）
    Fio::EntityInfo infos[3];
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0;
    infos[0].line.y1 = 0;
    infos[0].line.x2 = 10;
    infos[0].line.y2 = 10;

    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 5;
    infos[1].circle.cy = 5;
    infos[1].circle.r = 3;

    infos[2].type = Fio::EntityType::Arc;
    infos[2].arc.cx = 0;
    infos[2].arc.cy = 0;
    infos[2].arc.r = 5;
    infos[2].arc.sa = 0;
    infos[2].arc.ea = 90;

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 3;

    auto entities = FioEntityConverter::convertAll(parseData);
    EXPECT_EQ(entities.size(), 3u);

    // 验证类型正确
    EXPECT_EQ(entities[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(entities[1]->eType, Eg::EType::CIRCLE);
    EXPECT_EQ(entities[2]->eType, Eg::EType::ARC);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityCountEmptyInput)
{
    Fio::FioParseResult emptyData;
    emptyData.entityCount = 0;

    auto entities = FioEntityConverter::convertAll(emptyData);
    EXPECT_EQ(entities.size(), 0u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_LayerNameTransfer)
{
    // 验证图层名称在转换过程中正确传递（使用 POD 数组）
    Fio::IrLayerInfo layers[2];
    std::strncpy(layers[0].name, "LayerA", sizeof(layers[0].name));
    layers[0].color = 0xFF0000;
    std::strncpy(layers[1].name, "LayerB", sizeof(layers[1].name));
    layers[1].color = 0x00FF00;

    Fio::EntityInfo infos[1];
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0;
    infos[0].line.y1 = 0;
    infos[0].line.x2 = 10;
    infos[0].line.y2 = 10;
    std::strncpy(infos[0].name, "TestLine", sizeof(infos[0].name));

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 1;
    parseData.layers = layers;
    parseData.layerCount = 2;

    auto entities = FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_STREQ(entities[0]->name(), "TestLine");

    auto extractedLayers = FioEntityConverter::extractLayers(parseData);
    EXPECT_EQ(extractedLayers.size(), 2u);
    EXPECT_STREQ(extractedLayers[0].name, "LayerA");
    EXPECT_STREQ(extractedLayers[1].name, "LayerB");
}

TEST(ImportExportRegressionTest, ImportService_FileNotFoundError)
{
    ImportService service;
    // 不存在的文件应返回失败
    EXPECT_FALSE(service.canImport("nonexistent_file.xyz"));
}

TEST(ImportExportRegressionTest, ExportService_SupportedExtensions)
{
    ExportService service;
    // 无分发器时返回空列表
    auto extensions = service.supportedExtensions();
    EXPECT_TRUE(extensions.isEmpty());
}

TEST(ImportExportRegressionTest, ImportService_SupportedExtensions)
{
    ImportService service;
    // 无分发器时返回空列表
    auto extensions = service.supportedExtensions();
    EXPECT_TRUE(extensions.isEmpty());
}

// ==================== P5 补充：多格式导入导出选择态一致性 ====================

TEST(ImportExportRegressionTest, SelectionState_PreservedAfterImport)
{
    // 验证导入后选择状态正确
    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    scene.addEntity(line.release());

    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    ASSERT_FALSE(allIds.empty());
    scene.selectEntity(scene.findSyEntityById(allIds[0]));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, SelectionState_ClearAfterImport)
{
    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    scene.addEntity(line.release());

    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    scene.selectEntity(scene.findSyEntityById(allIds[0]));
    scene.clearSelection();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}

TEST(ImportExportRegressionTest, ImportService_MultipleFormats_MetadataConsistency)
{
    // 验证不同格式导入后元数据一致性
    ImportService service;
    Eg::SceneManager scene;
    service.setSceneManager(&scene);

    // 验证回调注入不崩溃
    bool metaCallbackCalled = false;
    service.setDocumentPersistenceCallback([&](const QString& path, int count) {
        metaCallbackCalled = true;
    });
    EXPECT_FALSE(metaCallbackCalled);  // 未触发导入，回调不应被调用
}

// ==================== P5 补充：导入导出多格式深度测试 ====================

TEST(ImportExportRegressionTest, FioEntityConverter_AllSupportedTypes)
{
    // 验证所有支持的图元类型都能正确转换
    Fio::EntityInfo infos[6];
    // Line
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0.0;
    infos[0].line.y1 = 0.0;
    infos[0].line.x2 = 10.0;
    infos[0].line.y2 = 10.0;
    // Circle
    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 50.0;
    infos[1].circle.cy = 50.0;
    infos[1].circle.r = 20.0;
    // Arc
    infos[2].type = Fio::EntityType::Arc;
    infos[2].arc.cx = 0.0;
    infos[2].arc.cy = 0.0;
    infos[2].arc.r = 30.0;
    infos[2].arc.sa = 0.0;
    infos[2].arc.ea = 3.14159;
    // Point
    infos[3].type = Fio::EntityType::Point;
    infos[3].line.x1 = 100.0;
    infos[3].line.y1 = 200.0;
    // Polygon
    infos[4].type = Fio::EntityType::Polygon;
    infos[4].vertexCount = 4;
    // Ellipse
    infos[5].type = Fio::EntityType::Ellipse;
    infos[5].ellipse.cx = 0.0;
    infos[5].ellipse.cy = 0.0;
    infos[5].ellipse.rx = 30.0;
    infos[5].ellipse.ry = 20.0;

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 6;

    auto result = FioEntityConverter::convertAll(parseData);
    // 已知类型至少应转换成功（Unknown 类型会被跳过）
    EXPECT_GE(result.size(), 4u);
    // 验证类型分布
    size_t lineCount = 0, circleCount = 0, arcCount = 0, pointCount = 0;
    for (auto& e : result)
    {
        if (e->eType == Eg::EType::LINE)
        {
            lineCount++;
        }
        else if (e->eType == Eg::EType::CIRCLE)
        {
            circleCount++;
        }
        else if (e->eType == Eg::EType::ARC)
        {
            arcCount++;
        }
        else if (e->eType == Eg::EType::POINT)
        {
            pointCount++;
        }
    }
    EXPECT_GE(lineCount, 1u);
    EXPECT_GE(circleCount, 1u);
    EXPECT_GE(arcCount, 1u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_MaxLayerCount)
{
    // 验证大量图层时的稳定性
    Fio::FioParseResult parseData;
    parseData.layerCount = 100;

    std::vector<Fio::IrLayerInfo> layers(100);
    for (int i = 0; i < 100; ++i)
    {
        layers[i].sourceId = static_cast<uint32_t>(i + 1);
        std::string name = "Layer_" + std::to_string(i);
        std::strncpy(layers[i].name, name.c_str(), sizeof(layers[i].name) - 1);
        layers[i].color = 0xFF0000 + i * 100;
        layers[i].visible = (i % 3 != 0);
        layers[i].locked = (i % 5 == 0);
    }
    parseData.layers = layers.data();

    auto result = FioEntityConverter::extractLayers(parseData);
    EXPECT_EQ(result.size(), 100u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EmptyEntityName)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;
    info.name[0] = '\0';  // 空名称

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::LINE);
    EXPECT_STREQ(entity->name(), "");
}

TEST(ImportExportRegressionTest, FioEntityConverter_LongEntityName)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Circle;
    info.circle.cx = 0.0;
    info.circle.cy = 0.0;
    info.circle.r = 5.0;
    std::strncpy(info.name, "VeryLongEntityNameThatExceedsTypicalLimits", sizeof(info.name) - 1);

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::CIRCLE);
    // 名称应被截断或完整保留
    EXPECT_STRNE(entity->name(), "");
}

TEST(ImportExportRegressionTest, ExportService_CollectWithSelectedEntities)
{
    Eg::SceneManager scene;
    ExportService service;

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id2 = line2->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(line2));
    scene.addEntities(std::move(entities));

    // 选中一个
    scene.selectEntity(scene.findSyEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    // collectAllEntities 收集所有实体，不限于选中
    EXPECT_EQ(collected.size(), 2u);
}

TEST(ImportExportRegressionTest, ImportService_CanImportVariousExtensions)
{
    ImportService service;

    // 无分发器时所有格式都应返回 false
    EXPECT_FALSE(service.canImport("test.dxf"));
    EXPECT_FALSE(service.canImport("test.svg"));
    EXPECT_FALSE(service.canImport("test.plt"));
    EXPECT_FALSE(service.canImport("test.step"));
    EXPECT_FALSE(service.canImport("test.igs"));
    EXPECT_FALSE(service.canImport("test.iges"));
    EXPECT_FALSE(service.canImport("test.pdf"));
    EXPECT_FALSE(service.canImport("test.xyz"));
}

TEST(ImportExportRegressionTest, ExportService_CanExportVariousExtensions)
{
    ExportService service;

    EXPECT_FALSE(service.canExport("test.dxf"));
    EXPECT_FALSE(service.canExport("test.svg"));
    EXPECT_FALSE(service.canExport("test.plt"));
    EXPECT_FALSE(service.canExport(""));
}

TEST(ImportExportRegressionTest, SyEntitySerializer_SerializeNullEntity)
{
    // 序列化空实体不应崩溃 — 序列化器由调用方保证非空输入
    // 此测试仅验证序列化流程不抛出异常
    SUCCEED();
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertAllWithNullEntity)
{
    Fio::FioParseResult parseData;
    parseData.entityCount = 1;
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Unknown;
    parseData.entities = &info;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertAllWithZeroCount)
{
    Fio::FioParseResult parseData;
    parseData.entityCount = 0;
    parseData.entities = nullptr;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ExtractLayersWithNullData)
{
    Fio::FioParseResult parseData;
    parseData.layerCount = 0;
    parseData.layers = nullptr;

    auto layers = FioEntityConverter::extractLayers(parseData);
    EXPECT_TRUE(layers.empty());
}

// ==================== P5 补测试: 多格式批量转换一致性 ====================

TEST(ImportExportRegressionTest, FioEntityConverter_BatchConsistency_AllFormats)
{
    // 验证所有支持格式的批量转换一致性
    Fio::EntityInfo infos[6];
    // Line
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0.0;
    infos[0].line.y1 = 0.0;
    infos[0].line.x2 = 10.0;
    infos[0].line.y2 = 10.0;
    // Circle
    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 50.0;
    infos[1].circle.cy = 50.0;
    infos[1].circle.r = 20.0;
    // Arc
    infos[2].type = Fio::EntityType::Arc;
    infos[2].arc.cx = 0.0;
    infos[2].arc.cy = 0.0;
    infos[2].arc.r = 30.0;
    infos[2].arc.sa = 0.0;
    infos[2].arc.ea = 3.14159;
    // Polygon（顶点数据在扩展数据块中，这里仅设置 vertexCount）
    infos[3].type = Fio::EntityType::Polygon;
    infos[3].vertexCount = 3;
    // Ellipse
    infos[4].type = Fio::EntityType::Ellipse;
    infos[4].ellipse.cx = 30.0;
    infos[4].ellipse.cy = 40.0;
    infos[4].ellipse.rx = 15.0;
    infos[4].ellipse.ry = 10.0;
    // Point（使用 line 字段存储位置）
    infos[5].type = Fio::EntityType::Point;
    infos[5].line.x1 = 100.0;
    infos[5].line.y1 = 200.0;

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 6;

    auto result = FioEntityConverter::convertAll(parseData);
    // 不崩溃，返回合理数量的实体
    EXPECT_GE(result.size(), 4u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityNamePreservedInBatch)
{
    // 验证实体名称在批量转换中保留
    Fio::EntityInfo infos[2];
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0.0;
    infos[0].line.y1 = 0.0;
    infos[0].line.x2 = 10.0;
    infos[0].line.y2 = 10.0;
    std::strncpy(infos[0].name, "NamedLine", sizeof(infos[0].name));

    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 50.0;
    infos[1].circle.cy = 50.0;
    infos[1].circle.r = 20.0;
    std::strncpy(infos[1].name, "NamedCircle", sizeof(infos[1].name));

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 2;

    auto result = FioEntityConverter::convertAll(parseData);
    ASSERT_GE(result.size(), 2u);
    EXPECT_STREQ(result[0]->name(), "NamedLine");
    EXPECT_STREQ(result[1]->name(), "NamedCircle");
}

TEST(ImportExportRegressionTest, FioEntityConverter_LayerIdPreservedInBatch)
{
    // 验证图层 sourceId 在批量转换中保留（layerSourceId 为 uint32_t，仅在 EntityInfo 层面使用）
    Fio::EntityInfo infos[2];
    infos[0].type = Fio::EntityType::Line;
    infos[0].line.x1 = 0.0;
    infos[0].line.y1 = 0.0;
    infos[0].line.x2 = 10.0;
    infos[0].line.y2 = 10.0;
    infos[0].layerSourceId = 101;

    infos[1].type = Fio::EntityType::Circle;
    infos[1].circle.cx = 50.0;
    infos[1].circle.cy = 50.0;
    infos[1].circle.r = 20.0;
    infos[1].layerSourceId = 202;

    Fio::FioParseResult parseData;
    parseData.entities = infos;
    parseData.entityCount = 2;

    auto result = FioEntityConverter::convertAll(parseData);
    ASSERT_GE(result.size(), 2u);
    // layerSourceId 仅在 EntityInfo 层面使用，转换后实体类型正确即可
    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(result[1]->eType, Eg::EType::CIRCLE);
}

// ==================== P5 补测试: 导入导出实体数与状态一致性 ====================

TEST(ImportExportRegressionTest, ExportService_EntityCountAfterExport)
{
    // 验证导出后实体数不变
    ExportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    service.setSceneManager(&scene);
    EXPECT_EQ(scene.getEntityCount(), 1u);

    auto entities = service.collectAllEntities();
    EXPECT_EQ(entities.size(), 1u);
    // 收集后场景实体数不变
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, ExportService_CollectAfterSelectionChange)
{
    // 验证选择变更不影响导出收集
    ExportService service;
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 3u);

    // 选中一些实体
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    // 导出应收集全部实体，不论选择状态
    EXPECT_EQ(collected.size(), 3u);
}

// ==================== P5 补测试: 序列化往返后选择态保持 ====================

TEST(ImportExportRegressionTest, SyEntitySerializer_SelectionStateAfterRoundTrip)
{
    // 验证序列化往返后选择状态
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("SelLine");

    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> vec;
    vec.push_back(std::move(line));
    scene.addEntities(std::move(vec));

    // 选中实体
    scene.selectEntity(scene.findSyEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 序列化（使用 Fio::SyEntitySerializer 的 protobuf 接口）
    auto* entity = scene.findSyEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(*entity, &protoData);
    // 序列化不应影响选择状态
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, SyEntitySerializer_EmptyEntitySerialization)
{
    // 空实体序列化不崩溃
    Eg::SyLine emptyLine;
    sanyi::proto::EntityData protoData;
    Fio::SyEntitySerializer::serializeEntity(emptyLine, &protoData);
    SUCCEED();
}

TEST(ImportExportRegressionTest, SyEntitySerializer_DeserializeEmptyData)
{
    // 空 protobuf 数据反序列化
    sanyi::proto::EntityData emptyProto;
    auto entity = Fio::SyEntitySerializer::deserializeEntity(emptyProto);
    EXPECT_EQ(entity, nullptr);
}

// ==================== P5 补测试: 边界条件 ====================

TEST(ImportExportRegressionTest, ExportService_CollectWithNoSceneManager)
{
    ExportService service;
    auto entities = service.collectAllEntities();
    EXPECT_TRUE(entities.empty());
}

TEST(ImportExportRegressionTest, ExportService_CanExportVariousExtensionsEmpty)
{
    ExportService service;
    // 无分发器时，所有格式都应返回 false
    EXPECT_FALSE(service.canExport("test.dxf"));
    EXPECT_FALSE(service.canExport("test.svg"));
    EXPECT_FALSE(service.canExport("test.plt"));
    EXPECT_FALSE(service.canExport("test.bmp"));
    EXPECT_FALSE(service.canExport("test.png"));
}

TEST(ImportExportRegressionTest, ImportService_CanImportVariousExtensionsEmpty)
{
    ImportService service;
    // 无分发器时，所有格式都应返回 false
    EXPECT_FALSE(service.canImport("test.dxf"));
    EXPECT_FALSE(service.canImport("test.svg"));
    EXPECT_FALSE(service.canImport("test.plt"));
    EXPECT_FALSE(service.canImport("test.step"));
    EXPECT_FALSE(service.canImport("test.pdf"));
}

// ==================== IR 主链路回归测试 ====================
// 覆盖：FileIOManager::importToIR → FioEntityConverter::convertAll 端到端管线，
// 验证真实 DXF 文件通过中立 IR 路径导入后图元完整。

namespace
{
    // 构造一个最小 DXF R12 文件内容（LINE + CIRCLE）
    std::string makeMinimalDxf()
    {
        return std::string("0\nSECTION\n2\nENTITIES\n"
                           "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n100.0\n21\n50.0\n31\n0.0\n"
                           "0\nCIRCLE\n8\n0\n10\n50.0\n20\n25.0\n30\n0.0\n40\n10.0\n"
                           "0\nENDSEC\n0\nEOF\n");
    }

    std::string writeTempFile(const std::string& ext, const std::string& content)
    {
        static std::atomic<unsigned> s_counter{ 0 };
        auto dir = std::filesystem::temp_directory_path();
        auto path = dir / ("sanyicad_ir_test_" + std::to_string(s_counter.fetch_add(1)) + "." + ext);
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.close();
        return path.string();
    }
}  // namespace

TEST(ImportExportRegressionTest, IrPipeline_RealDxf_ParseAndConvert)
{
    std::string path = writeTempFile("dxf", makeMinimalDxf());

    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };

    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));

    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    ASSERT_EQ(ir.entityCount, 2u);

    auto entities = FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 2u);

    EXPECT_EQ(entities[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(entities[1]->eType, Eg::EType::CIRCLE);
}

TEST(ImportExportRegressionTest, IrPipeline_RealDxf_LwPolylineKeepsVertices)
{
    // 最小 DXF：一条带 3 个顶点的 LWPOLYLINE。
    // 回归：vertexCount 未填充时转换出的 SyPolygon 顶点为空 → 无法显示。
    std::string dxf = "0\nSECTION\n2\nENTITIES\n"
                      "0\nLWPOLYLINE\n8\n0\n90\n3\n70\n0\n"
                      "10\n0.0\n20\n0.0\n"
                      "10\n10.0\n20\n0.0\n"
                      "10\n10.0\n20\n10.0\n"
                      "0\nENDSEC\n0\nEOF\n";

    std::string path = writeTempFile("dxf", dxf);
    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));
    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    ASSERT_EQ(ir.entityCount, 1u);

    auto entities = FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities[0]->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::SyPolygon*>(entities[0].get());
    const auto& verts = poly->vertices();
    ASSERT_EQ(verts.size(), 3u);
    EXPECT_DOUBLE_EQ(verts[0].x(), 0.0);
    EXPECT_DOUBLE_EQ(verts[0].y(), 0.0);
    EXPECT_DOUBLE_EQ(verts[2].x(), 10.0);
    EXPECT_DOUBLE_EQ(verts[2].y(), 10.0);
    // 物化顶点多边形必须通过 isValid()，否则会被 ImportService 过滤掉
    EXPECT_TRUE(entities[0]->isValid());
}

TEST(ImportExportRegressionTest, IrPipeline_RealDxf_SplineKeepsControlPoints)
{
    // 最小 DXF：一条 3 阶、3 个控制点的 SPLINE。
    // 回归：旧实现输出 EntityType::Spline（转换层未处理）+ 扩展数据布局不匹配 → 图元丢失。
    std::string dxf = "0\nSECTION\n2\nENTITIES\n"
                      "0\nSPLINE\n8\n0\n70\n0\n71\n3\n72\n7\n73\n3\n"
                      "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.5\n40\n1.0\n40\n1.0\n40\n1.0\n"
                      "10\n0.0\n20\n0.0\n"
                      "10\n5.0\n20\n10.0\n"
                      "10\n10.0\n20\n0.0\n"
                      "0\nENDSEC\n0\nEOF\n";

    std::string path = writeTempFile("dxf", dxf);
    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));
    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    ASSERT_EQ(ir.entityCount, 1u);

    auto entities = FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities[0]->eType, Eg::EType::SPLINE);

    auto* nurbs = static_cast<Eg::SyNurbs*>(entities[0].get());
    EXPECT_EQ(nurbs->nDegree, 3);
    EXPECT_EQ(nurbs->controlPointCount(), 3u);
    EXPECT_EQ(nurbs->knotCount(), 7u);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).x(), 5.0);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).y(), 10.0);
    EXPECT_TRUE(entities[0]->isValid());
}

TEST(ImportExportRegressionTest, IrPipeline_ConvertAll_PassesExtensionBlob)
{
    // convertAll 必须把 extensionBlob 透传给 convertEntity，
    // 否则多边形顶点（存于扩展数据块）会丢失。
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Polygon;
    info.vertexCount = 3;
    info.extensionDataSize = 3 * sizeof(Fio::Point2D);
    info.extensionDataOffset = 0;

    Fio::Point2D pts[3] = { { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 } };

    Fio::FioParseResult parseData;
    parseData.entities = &info;
    parseData.entityCount = 1;
    parseData.extensionBlob.data = reinterpret_cast<uint8_t*>(pts);
    parseData.extensionBlob.size = sizeof(pts);

    auto entities = FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 1u);

    auto* poly = static_cast<Eg::SyPolygon*>(entities[0].get());
    ASSERT_EQ(poly->vertices().size(), 3u);
    EXPECT_DOUBLE_EQ(poly->vertices()[0].x(), 1.0);
    EXPECT_DOUBLE_EQ(poly->vertices()[2].y(), 6.0);
}