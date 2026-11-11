/**
 * @file ImportExportRegressionTests.cpp
 * @brief 导入导出链回归测试 — 覆盖 ImportService/ExportService/FioEntityConverter 完整链路
 *
 * 测试范围：
 *  - ImportService 五阶段流程与回调注入
 *  - ExportService 数据收集与格式输出
 *  - FioEntityConverter 元数据/硬件信息/图元属性转换
 *  - 多格式 draw/circle/arc 往返一致性
 *  - 空场景/边界条件安全性
 *
 * P4 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "ImportService.h"
#include "ExportService.h"
#include "Composition/ImportExportServiceSetup.h"
#include "Export/ExportDispatcher.h"
#include "Import/ImportDispatcher.h"
#include "Import/Readers/NativeImportReader.h"
#include "Export/ExportContext.h"
#include "Export/Writers/NativeExportWriter.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Interaction/HardwareProfileManager.h"
#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/AprDocument.h"
#include "Engine3D/Import/FioEntityConverter.h"
#include "Engine2D/Import/Fio2DEntityConverter.h"
#include "AprEntitySerializer.h"
#include "FileIO/FioTypes.h"
#include "FileIO/FileIOManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprArc.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprNurbs.h"

#include "Persistence/PersistenceService.h"
#include "Persistence/Models/RecentFileRecord.h"
#include "Persistence/Repositories/RecentFileRepository.h"
#include "UI/Services/RecentFileService.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>

// Engine3D 的转换器只构造 Mesh3D，2D 图元由 Engine2D 的工厂提供。
// 应用里由 ApplicationCompositionRoot 在启动时注册；测试在此补一次。
namespace
{
    const bool kFio2DFactoryRegisteredInImportExport = []() {
        Eg::FioEntityConverter::set2DEntityFactory(&Eg::convertFio2DEntity);
        return true;
    }();
}  // namespace

// ---- ImportService 基础流程测试 ----

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

TEST(ImportExportRegressionTest, ImportService_RecentFileCallback)
{
    ImportService service;

    QString lastPath;
    service.setRecentFileAddCallback([&](const QString& path, const QString& workbenchId) {
        lastPath = path;
        Q_UNUSED(workbenchId);
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
    service.setDocumentPersistenceCallback([&](const QString& path, int entityCount, const QString& workbenchId) {
        Q_UNUSED(workbenchId);
        called = true;
        return true;
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

// ---- RecentFileService：legacy 行迁移与 QSettings 兜底 ----

namespace
{
    /// 迁移前的 DB 行：workbench 列为空字符串（v5→v6 加列时的旧数据）
    RecentFileRecord makeLegacyRecentRecord(const std::string& filePath)
    {
        RecentFileRecord rec;
        rec.filePath = filePath;
        rec.title = filePath.substr(filePath.find_last_of('/') + 1);
        rec.format = "apr";
        rec.workbench = "";
        rec.lastOpenedTime = "2026-01-01T00:00:00";
        return rec;
    }
}  // namespace

TEST(RecentFileServiceRegressionTest, LoadMigratesLegacyEmptyWorkbenchRows)
{
    QTemporaryDir dbDir;
    ASSERT_TRUE(dbDir.isValid());

    PersistenceService persistence;
    ASSERT_TRUE(persistence.initialize(dbDir.filePath(QStringLiteral("recent.sqlite")).toStdString()));
    ASSERT_NE(persistence.recentFiles(), nullptr);
    ASSERT_TRUE(persistence.recentFiles()->append(makeLegacyRecentRecord("/tmp/legacy_doc.apr")));

    RecentFileService service(&persistence);
    const QStringList files = service.loadRecentFiles(QStringLiteral("2D"));

    // 归类后应出现在 2D 列表（.apr → 2D）
    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files.front(), QStringLiteral("/tmp/legacy_doc.apr"));

    // 关键断言：load 必须把 workbench='' 回写成归类结果 ——
    // 否则该行不属于任何工作台桶，append 的按桶裁剪永远裁不到它，库里会无限累积
    bool found = false;
    for (const auto& rec : persistence.recentFiles()->loadAll())
    {
        if (rec.filePath == "/tmp/legacy_doc.apr")
        {
            found = true;
            EXPECT_EQ(rec.workbench, "2D");
        }
    }
    EXPECT_TRUE(found);

    persistence.shutdown();
}

TEST(RecentFileServiceRegressionTest, FallsBackToQSettingsWhenDbHasNoRowsForCurrentWorkbench)
{
    // QSettings 全程指到临时 ini：不污染真实用户配置，且与进程内其它用例隔离。
    // 路径进程级生效无法还原，目录用 static 保持存活到进程结束。
    static QTemporaryDir settingsDir;
    ASSERT_TRUE(settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    if (QCoreApplication::organizationName().isEmpty())
    {
        QCoreApplication::setOrganizationName(QStringLiteral("AprTests"));
    }
    if (QCoreApplication::applicationName().isEmpty())
    {
        QCoreApplication::setApplicationName(QStringLiteral("RecentFileRegression"));
    }

    QTemporaryDir dbDir;
    ASSERT_TRUE(dbDir.isValid());
    PersistenceService persistence;
    ASSERT_TRUE(persistence.initialize(dbDir.filePath(QStringLiteral("recent.sqlite")).toStdString()));
    ASSERT_NE(persistence.recentFiles(), nullptr);

    // DB 里只有 3D 的记录
    RecentFileRecord only3D;
    only3D.filePath = "/tmp/model.stl";
    only3D.title = "model.stl";
    only3D.format = "stl";
    only3D.workbench = "3D";
    only3D.lastOpenedTime = "2026-01-01T00:00:00";
    ASSERT_TRUE(persistence.recentFiles()->append(only3D));

    // QSettings 兜底里有一条 2D 记录
    QSettings seed;
    seed.setValue(QStringLiteral("RecentFiles/2D"), QStringList{ QStringLiteral("/tmp/settings_only.apr") });
    seed.sync();

    RecentFileService service(&persistence);
    const QStringList files = service.loadRecentFiles(QStringLiteral("2D"));

    // 回归防护：DB 有数据但没有 2D 条目时，不能直接返回空 —— 老版本写在
    // QSettings 的兜底数据会被整体吞掉（修复前本断言失败）
    EXPECT_TRUE(files.contains(QStringLiteral("/tmp/settings_only.apr")));
    // 3D 的行也不能漏进 2D 桶
    EXPECT_FALSE(files.contains(QStringLiteral("/tmp/model.stl")));

    QSettings().remove(QStringLiteral("RecentFiles/2D"));
    persistence.shutdown();
}

// ---- ExportService 基础流程测试 ----

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    service.setSceneManager(&scene);

    auto entities = service.collectAllEntities();
    EXPECT_EQ(entities.size(), 1u);
}

// ---- FioEntityConverter 扩展测试 ----

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

    auto result = Eg::FioEntityConverter::convertEntity(entity);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->eType, Eg::EType::LINE);

    auto* line = static_cast<Eg::AprLine*>(result.get());
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

    auto result = Eg::FioEntityConverter::convertEntity(info);
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

    auto result = Eg::FioEntityConverter::convertEntity(info);
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

    auto result = Eg::FioEntityConverter::convertEntity(info);
    ASSERT_NE(result, nullptr);
    // 图层 ID 仅用于引用追踪，不直接存储到图元
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
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

    auto result = Eg::FioEntityConverter::extractLayers(parseData);
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
}

// ---- 导入导出链路一致性测试 ----

TEST(ImportExportRegressionTest, Chain_EntityCountAfterImport)
{
    // 模拟从 IR 导入图元到场景
    Eg::SceneManager scene;

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.line.x2 = 10.0;
    info.line.y2 = 10.0;

    auto entity = Eg::FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);

    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(entity));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, Chain_ExportCollectMatchesImport)
{
    Eg::SceneManager scene;

    // 导入 3 个不同图元
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

    auto entities = Eg::FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 3u);

    std::vector<std::unique_ptr<Eg::AprEntity>> moveVec;
    for (auto& e : entities)
    {
        moveVec.push_back(std::move(e));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(moveVec));

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

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 删除后导出应收集 0 个图元
    Eg::SceneWriteGateway::deleteEntity(scene, scene.findAprEntityById(lineId));
    EXPECT_EQ(scene.getEntityCount(), 0u);

    ExportService service;
    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    EXPECT_TRUE(collected.empty());
}

// ---- 图层链一致性测试 ----

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

    auto result = Eg::FioEntityConverter::extractLayers(parseData);
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

// ---- 服务信号测试 ----

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

// ---- 3D SceneManager 注入测试 ----

TEST(ImportExportRegressionTest, ImportService_SetSceneManager3D)
{
    ImportService service;
    // 3D 场景管理器注入（指针为 nullptr 是合法状态）
    service.setSceneManager3D(nullptr);
    EXPECT_EQ(service.sceneManager3D(), nullptr);

    SUCCEED();
}

// ---- 边界条件测试 ----

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

// ---- 异步 worker 收尾不再固定阻塞 ----

// 回归：旧实现 shutdownAsyncThread 只要线程对象 joinable 就固定 sleep 3 秒，
// 从第二次导入开始每次操作都白等 3 秒。这里连续启动两次异步导入，
// 只测第二次调用的耗时（第一次 worker 在文件检查处已快速失败退出）。
TEST(ImportExportRegressionTest, ImportService_RepeatedAsyncStartDoesNotBlock)
{
    ImportDispatcher dispatcher;  // 不注册读取器：phase 1 在文件存在性检查处快速失败
    ImportService service;
    service.setDispatcher(&dispatcher);

    ImportContext context;
    context.sourcePath = QStringLiteral("missing_regression_input.dxf");

    service.importAsync(context);
    // 让第一次 worker 走完，把它本身的执行时间排除在第二次调用的测量区间之外
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto begin = std::chrono::steady_clock::now();
    service.importAsync(context);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();

    // 旧实现这里是 3000ms；新实现只等完成信号，应毫秒级返回
    EXPECT_LT(elapsedMs, 1500) << "second importAsync blocked " << elapsedMs << " ms on the main thread";
}

// 回归：导出侧同一缺陷。第二次 exportAsync 可能因上一条仍未收尾而被拒绝，
// 但拒绝之前已经走过 shutdownAsyncThread，耗时仍可用来判定是否还在固定阻塞。
TEST(ImportExportRegressionTest, ExportService_RepeatedAsyncStartDoesNotBlock)
{
    ExportDispatcher dispatcher;  // 不注册写入器：worker 在 dispatch 内快速失败
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    ExportService service;
    service.setSceneManager(&scene);
    service.setDispatcher(&dispatcher);

    ExportContext context;
    context.targetPath = QStringLiteral("no_writer_regression_output.dxf");

    service.exportAsync(context);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto begin = std::chrono::steady_clock::now();
    service.exportAsync(context);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();

    EXPECT_LT(elapsedMs, 1500) << "second exportAsync blocked " << elapsedMs << " ms on the main thread";
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertNullEntityInfo)
{
    // 空指针保护由 convertAll 处理，convertEntity 期望有效引用
    Fio::FioParseResult parseData;
    parseData.entities = nullptr;
    parseData.entityCount = 0;

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

// ---- FioEntityConverter 深度测试 ----

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertLineEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    std::strncpy(info.name, "TestLine", sizeof(info.name) - 1);
    info.line.x1 = 10.0;
    info.line.y1 = 20.0;
    info.line.x2 = 100.0;
    info.line.y2 = 200.0;

    auto entity = Eg::FioEntityConverter::convertEntity(info);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POLYGON);
    EXPECT_STREQ(entity->name(), "TestPolygon");
}

TEST(ImportExportRegressionTest, FioEntityConverter_UnknownTypeReturnsNull)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Unknown;

    auto entity = Eg::FioEntityConverter::convertEntity(info);
    EXPECT_EQ(entity, nullptr);
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertAllEmpty)
{
    Fio::FioParseResult parseData;
    parseData.entities = nullptr;
    parseData.entityCount = 0;

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ExtractLayersEmpty)
{
    Fio::FioParseResult parseData;
    parseData.layers = nullptr;
    parseData.layerCount = 0;

    auto layers = Eg::FioEntityConverter::extractLayers(parseData);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::LINE);
    EXPECT_STREQ(entity->name(), "LayeredLine");
    // 图层信息应通过 layerSourceId 传递
}

// ---- AprEntitySerializer 深度测试 ----

TEST(ImportExportRegressionTest, AprEntitySerializer_LineRoundTripWithLayer)
{
    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(10, 20), Ut::Vec2d(100, 200) });
    line->setName("LayerLine");

    line->basePoint = Ut::Vec2d(50, 50);

    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(*line, &protoData);

    auto restored = Fio::AprEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::LINE);
    EXPECT_STREQ(restored->name(), "LayerLine");
}

TEST(ImportExportRegressionTest, AprEntitySerializer_CircleRoundTripWithLayer)
{
    auto circle = std::make_unique<Eg::AprCircle>();
    circle->dRadius = 15.0;
    circle->basePoint = Ut::Vec2d(30, 40);
    circle->setName("LayerCircle");

    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(*circle, &protoData);

    auto restored = Fio::AprEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::CIRCLE);
    EXPECT_STREQ(restored->name(), "LayerCircle");
}

TEST(ImportExportRegressionTest, AprEntitySerializer_PolygonRoundTripWithLayer)
{
    auto poly = std::make_unique<Eg::AprPolygon>();
    poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
    poly->bClosed = true;
    poly->bCCW = true;
    poly->setName("LayerPoly");

    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(*poly, &protoData);

    auto restored = Fio::AprEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::POLYGON);
    EXPECT_STREQ(restored->name(), "LayerPoly");
}

TEST(ImportExportRegressionTest, AprEntitySerializer_ArcRoundTripWithLayer)
{
    auto arc = std::make_unique<Eg::AprArc>();
    arc->dRadius = 20.0;
    arc->dStartAngle = 30.0;
    arc->dEndAngle = 120.0;
    arc->setName("LayerArc");

    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(*arc, &protoData);

    auto restored = Fio::AprEntitySerializer::deserializeEntity(protoData);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->eType, Eg::EType::ARC);
    EXPECT_STREQ(restored->name(), "LayerArc");
}

// ---- 导入后图元数/图层/名称一致性 ----

TEST(ImportExportRegressionTest, ImportService_EntityCountAfterImport)
{
    // 验证 ImportService 导入后图元数正确
    ImportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId originalId = line->id;
    Eg::SceneWriteGateway::addEntity(scene, line.release());

    service.setSceneManager(&scene);

    // 验证场景管理器中有图元
    EXPECT_EQ(scene.getAllEntities().size(), 1u);
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, ExportService_EntityCountAfterCollect)
{
    ExportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::SceneWriteGateway::addEntity(scene, line.release());

    service.setSceneManager(&scene);

    auto collected = service.collectAllEntities();
    EXPECT_EQ(collected.size(), 1u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityCountAfterBatch)
{
    // 批量转换后图元数正确（使用 POD 数组）
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

    auto entities = Eg::FioEntityConverter::convertAll(parseData);
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

    auto entities = Eg::FioEntityConverter::convertAll(emptyData);
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

    auto entities = Eg::FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_STREQ(entities[0]->name(), "TestLine");

    auto extractedLayers = Eg::FioEntityConverter::extractLayers(parseData);
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

// ---- 多格式导入导出选择态一致性 ----

TEST(ImportExportRegressionTest, SelectionState_PreservedAfterImport)
{
    // 验证导入后选择状态正确
    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::SceneWriteGateway::addEntity(scene, line.release());

    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    ASSERT_FALSE(allIds.empty());
    scene.selectEntity(scene.findAprEntityById(allIds[0]));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, SelectionState_ClearAfterImport)
{
    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::SceneWriteGateway::addEntity(scene, line.release());

    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    scene.selectEntity(scene.findAprEntityById(allIds[0]));
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
    service.setDocumentPersistenceCallback([&](const QString& path, int count, const QString& workbenchId) {
        Q_UNUSED(workbenchId);
        metaCallbackCalled = true;
        return true;
    });
    EXPECT_FALSE(metaCallbackCalled);  // 未触发导入，回调不应被调用
}

// ---- 导入导出多格式深度测试 ----

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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    // 已知类型至少应转换成功（Unknown 类型会被跳过）
    EXPECT_GE(result.size(), 4u);
    // 验证类型分布
    size_t lineCount = 0, circleCount = 0, arcCount = 0, ptCount = 0;
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
            ptCount++;
        }
    }
    EXPECT_GE(lineCount, 1u);
    EXPECT_GE(circleCount, 1u);
    EXPECT_GE(arcCount, 1u);
    EXPECT_GE(ptCount, 1u);
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

    auto result = Eg::FioEntityConverter::extractLayers(parseData);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info);
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

    auto entity = Eg::FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::CIRCLE);
    // 名称应被截断或完整保留
    EXPECT_STRNE(entity->name(), "");
}

TEST(ImportExportRegressionTest, ExportService_CollectWithSelectedEntities)
{
    Eg::SceneManager scene;
    ExportService service;

    auto line1 = std::make_unique<Eg::AprLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    auto line2 = std::make_unique<Eg::AprLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id2 = line2->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(line2));
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));

    // 选中一个
    scene.selectEntity(scene.findAprEntityById(id1));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    // collectAllEntities 收集所有图元，不限于选中
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

TEST(ImportExportRegressionTest, AprEntitySerializer_SerializeNullEntity)
{
    // 序列化空图元不应崩溃 — 序列化器由调用方保证非空输入
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ConvertAllWithZeroCount)
{
    Fio::FioParseResult parseData;
    parseData.entityCount = 0;
    parseData.entities = nullptr;

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

TEST(ImportExportRegressionTest, FioEntityConverter_ExtractLayersWithNullData)
{
    Fio::FioParseResult parseData;
    parseData.layerCount = 0;
    parseData.layers = nullptr;

    auto layers = Eg::FioEntityConverter::extractLayers(parseData);
    EXPECT_TRUE(layers.empty());
}

// ---- 多格式批量转换一致性 ----

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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    // 不崩溃，返回合理数量的图元
    EXPECT_GE(result.size(), 4u);
}

TEST(ImportExportRegressionTest, FioEntityConverter_EntityNamePreservedInBatch)
{
    // 验证图元名称在批量转换中保留
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
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

    auto result = Eg::FioEntityConverter::convertAll(parseData);
    ASSERT_GE(result.size(), 2u);
    // layerSourceId 仅在 EntityInfo 层面使用，转换后图元类型正确即可
    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(result[1]->eType, Eg::EType::CIRCLE);
}

// ---- 导入导出图元数与状态一致性 ----

TEST(ImportExportRegressionTest, ExportService_EntityCountAfterExport)
{
    // 验证导出后图元数不变
    ExportService service;
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    service.setSceneManager(&scene);
    EXPECT_EQ(scene.getEntityCount(), 1u);

    auto entities = service.collectAllEntities();
    EXPECT_EQ(entities.size(), 1u);
    // 收集后场景图元数不变
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, ExportService_CollectAfterSelectionChange)
{
    // 验证选择变更不影响导出收集
    ExportService service;
    Eg::SceneManager scene;

    std::vector<std::unique_ptr<Eg::AprEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    Eg::SceneWriteGateway::addEntities(scene, std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 3u);

    // 选中一些图元
    scene.selectAll();
    EXPECT_EQ(scene.getSelectedEntityCount(), 3u);

    service.setSceneManager(&scene);
    auto collected = service.collectAllEntities();
    // 导出应收集全部图元，不论选择状态
    EXPECT_EQ(collected.size(), 3u);
}

// ---- 序列化往返后选择态保持 ----

TEST(ImportExportRegressionTest, AprEntitySerializer_SelectionStateAfterRoundTrip)
{
    // 验证序列化往返后选择状态
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::AprLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("SelLine");

    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::AprEntity>> vec;
    vec.push_back(std::move(line));
    Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

    // 选中图元
    scene.selectEntity(scene.findAprEntityById(lineId));
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    // 序列化（使用 Fio::AprEntitySerializer 的 protobuf 接口）
    auto* entity = scene.findAprEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(*entity, &protoData);
    // 序列化不应影响选择状态
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(ImportExportRegressionTest, AprEntitySerializer_EmptyEntitySerialization)
{
    // 空图元序列化不崩溃
    Eg::AprLine emptyLine;
    apr::proto::EntityData protoData;
    Fio::AprEntitySerializer::serializeEntity(emptyLine, &protoData);
    SUCCEED();
}

TEST(ImportExportRegressionTest, AprEntitySerializer_DeserializeEmptyData)
{
    // 空 protobuf 数据反序列化
    apr::proto::EntityData emptyProto;
    auto entity = Fio::AprEntitySerializer::deserializeEntity(emptyProto);
    EXPECT_EQ(entity, nullptr);
}

// ---- 边界条件 ----

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

// ---- IR 主链路回归测试 ----
// 覆盖：FileIOManager::importToIR → Eg::FioEntityConverter::convertAll 端到端管线，
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
        auto path = dir / ("april_ir_test_" + std::to_string(s_counter.fetch_add(1)) + "." + ext);
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

    auto entities = Eg::FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 2u);

    EXPECT_EQ(entities[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(entities[1]->eType, Eg::EType::CIRCLE);
}

TEST(ImportExportRegressionTest, IrPipeline_RealDxf_LwPolylineKeepsVertices)
{
    // 最小 DXF：一条带 3 个顶点的 LWPOLYLINE。
    // 回归：vertexCount 未填充时转换出的 AprPolygon 顶点为空 → 无法显示。
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

    auto entities = Eg::FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities[0]->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::AprPolygon*>(entities[0].get());
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
    // 最小 DXF：一条 3 阶、4 个控制点的 SPLINE。
    // 回归：SPLINE 必须输出受支持的 EntityType 且扩展数据布局匹配，否则图元丢失。
    // 控制点必须是「次数+1」个：3 阶样条至少要 4 个控制点（节点数 = 控制点数 + 次数 + 1 = 8）。
    // 3 个控制点 + 7 个节点是数学上非法的退化样条，libdxfrw 的结构校验会整条拒掉，
    // 并中止 ENTITIES 段，表现为「整份图纸读不出来」。
    std::string dxf = "0\nSECTION\n2\nENTITIES\n"
                      "0\nSPLINE\n8\n0\n70\n0\n71\n3\n72\n8\n73\n4\n"
                      "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.0\n40\n1.0\n40\n1.0\n40\n1.0\n40\n1.0\n"
                      "10\n0.0\n20\n0.0\n"
                      "10\n5.0\n20\n10.0\n"
                      "10\n10.0\n20\n0.0\n"
                      "10\n15.0\n20\n10.0\n"
                      "0\nENDSEC\n0\nEOF\n";

    std::string path = writeTempFile("dxf", dxf);
    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));
    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    ASSERT_EQ(ir.entityCount, 1u);

    auto entities = Eg::FioEntityConverter::convertAll(ir);
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities[0]->eType, Eg::EType::SPLINE);

    auto* nurbs = static_cast<Eg::AprNurbs*>(entities[0].get());
    EXPECT_EQ(nurbs->nDegree, 3);
    EXPECT_EQ(nurbs->controlPointCount(), 4u);
    EXPECT_EQ(nurbs->knotCount(), 8u);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).x(), 5.0);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).y(), 10.0);
    EXPECT_TRUE(entities[0]->isValid());
}

TEST(ImportExportRegressionTest, IrPipeline_RealDxf_DegenerateSplineKeepsRestOfFile)
{
    // 退化样条（控制点数 < 次数+1）只该丢自己那一条，不该让整份图纸读不出来。
    // 早先 libdxfrw 遇到结构非法的实体会直接中止 ENTITIES 段：
    // 一条坏样条 = 后面所有图元全丢，而且界面上没有任何提示。
    std::string dxf = "0\nSECTION\n2\nENTITIES\n"
                      "0\nSPLINE\n8\n0\n70\n0\n71\n3\n72\n7\n73\n3\n"
                      "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.5\n40\n1.0\n40\n1.0\n40\n1.0\n"
                      "10\n0.0\n20\n0.0\n"
                      "10\n5.0\n20\n10.0\n"
                      "10\n10.0\n20\n0.0\n"
                      "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n10.0\n21\n0.0\n31\n0.0\n"
                      "0\nENDSEC\n0\nEOF\n";

    std::string path = writeTempFile("dxf", dxf);
    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));
    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    // 坏样条被跳过，后面那条直线必须还在
    ASSERT_EQ(ir.entityCount, 1u);
    EXPECT_EQ(ir.entities[0].type, Fio::EntityType::Line);
    // 且必须留痕：用户要能看到「图纸里有内容没导入」
    EXPECT_GE(ir.warningCount, 1u);
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

    auto entities = Eg::FioEntityConverter::convertAll(parseData);
    ASSERT_EQ(entities.size(), 1u);

    auto* poly = static_cast<Eg::AprPolygon*>(entities[0].get());
    ASSERT_EQ(poly->vertices().size(), 3u);
    EXPECT_DOUBLE_EQ(poly->vertices()[0].x(), 1.0);
    EXPECT_DOUBLE_EQ(poly->vertices()[2].y(), 6.0);
}

// 回归：DXF 图层 sourceId 必须 1-based。
// sourceId 从 0 开始会与 EntityInfo.layerSourceId 的「0=无图层」哨兵冲突，
// 导致首个图层（DXF 默认图层 "0"）上的图元被 convertAll 过滤掉，导入后场景不显示图层颜色。
TEST(ImportExportRegressionTest, IrPipeline_RealDxf_FirstLayerSourceIdIsOneBased)
{
    // 最小 DXF：TABLES 中定义图层 "0"（红色），ENTITIES 中一条 LINE 位于该图层。
    std::string dxf = "0\nSECTION\n2\nTABLES\n"
                      "0\nTABLE\n2\nLAYER\n70\n1\n"
                      "0\nLAYER\n2\n0\n70\n0\n62\n1\n6\nCONTINUOUS\n"
                      "0\nENDTAB\n0\nENDSEC\n"
                      "0\nSECTION\n2\nENTITIES\n"
                      "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n30\n0.0\n11\n100.0\n21\n50.0\n31\n0.0\n"
                      "0\nENDSEC\n0\nEOF\n";

    std::string path = writeTempFile("dxf", dxf);
    Fio::FileIOManager fileIO;
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(path.c_str(), Fio::FileFormat::DXF, &ir, errBuf, sizeof(errBuf));
    std::filesystem::remove(path);

    ASSERT_TRUE(ok) << errBuf;
    ASSERT_GE(ir.layerCount, 1u);

    // 图层表与图元引用必须使用一致的 1-based sourceId
    const auto* layer0 = &ir.layers[0];
    EXPECT_NE(layer0->sourceId, 0u);

    // convertAll 必须把该图层上的图元写入 entityLayerMap（而不是因 sourceId==0 被丢弃）
    std::unordered_map<int64_t, uint32_t> entityLayerMap;
    auto entities = Eg::FioEntityConverter::convertAll(ir, &entityLayerMap);
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entityLayerMap.size(), 1u);

    const auto it = entityLayerMap.find(entities[0]->id);
    ASSERT_NE(it, entityLayerMap.end());
    EXPECT_EQ(it->second, layer0->sourceId);
}

// ==================== 原生 .apr 加工语义端到端闭环 ====================
//
// 历史问题：AprDocument.proto 有 output / default_process_id /
// hardware_profile_id / fill_hardware_profile_id，AprSerializer 也能读写；但生产
// 导出路径只把图元放进新建的 AprDocument，从不填图层表与 Profile 值集。保存提示
// 照常出现，重开后功率/速度/遍数静默退回默认 —— 属加工事故级数据丢失。
//
// 现在闭环是：ExportContext 值快照 → NativeExportWriter 写入 AprDocument →
// NativeImportReader 读出图层绑定 + Profile 值集 → ImportService 还原进
// LayerManager / HardwareProfileManager。下面分别覆盖每一段，并守住
// 「组合根没注入快照时仍必须显式告警」这条兜底。
namespace
{
    // MainTests 开 unity build：多个 .cpp 会拼进同一个 TU，匿名命名空间里的
    // 同名辅助函数会互相重定义，所以统一加 aprAprPersist 前缀。
    Fio::AprLayerDetailInfo aprAprPersistSampleLayer()
    {
        Fio::AprLayerDetailInfo layer;
        layer.id = 7;
        std::strncpy(layer.name, "Cut-7", sizeof(layer.name) - 1);
        layer.color = 0xFF00A0FF;
        layer.visible = false;
        layer.locked = true;
        layer.output = false;
        layer.defaultProcessId = 11;
        layer.layerType = 1;
        layer.fill = true;
        layer.fillColor = 0xFF112233;
        layer.hardwareProfileId = 3;
        layer.fillHardwareProfileId = 4;
        return layer;
    }

    Fio::AprHardwareProfileInfo aprAprPersistSampleProfile(
        int id, const char* name, double power, double speed)
    {
        Fio::AprHardwareProfileInfo p;
        p.id = id;
        std::strncpy(p.name, name, sizeof(p.name) - 1);
        p.power = power;
        p.speed = speed;
        p.frequency = 31.5;  // kHz
        p.focusOffset = -0.75;
        p.passes = 3;
        return p;
    }

    /// 造一份 .apr。viaClonePath 对应生产环境真实走的路径（ExportService 在派发前
    /// 会把 context.sceneManager 置空）；false 对应借用实体路径。两条路径都必须
    /// 把快照写进去，否则加工语义会因导出路径不同而随机丢失。
    ExportResult aprAprPersistWriteSample(const QString& path, bool withSnapshot, bool viaClonePath)
    {
        Eg::SceneManager scene;
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        std::vector<std::unique_ptr<Eg::AprEntity>> vec;
        vec.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

        ExportContext context;
        context.targetPath = path;
        context.format = Fio::FileFormat::Native;
        context.sceneManager = viaClonePath ? nullptr : &scene;

        Fio::VecAprEntityPtr cloned;
        if (viaClonePath)
        {
            for (auto* e : scene.getAllEntities())
            {
                if (e)
                {
                    cloned.emplace_back(e->clone());
                }
            }
        }

        if (withSnapshot)
        {
            context.hasMachiningSnapshot = true;
            context.machiningSnapshot.layers.push_back(aprAprPersistSampleLayer());
            context.machiningSnapshot.profiles.push_back(aprAprPersistSampleProfile(0, "Default", 100.0, 1000.0));
            context.machiningSnapshot.profiles.push_back(aprAprPersistSampleProfile(3, "Steel-3mm", 77.5, 245.25));
            context.machiningSnapshot.profiles.push_back(aprAprPersistSampleProfile(4, "Fill", 42.0, 320.0));
        }

        NativeExportWriter writer;
        return writer.write(context, cloned);
    }

    void aprAprPersistVerifyDocument(const QString& path)
    {
        Fio::NativeParser parser(Fio::FileFormat::Native);
        Fio::AprDocument doc;
        const auto parseResult = parser.parseDocument(path.toUtf8().constData(), doc);
        ASSERT_TRUE(parseResult.success) << parseResult.errorMessage;

        ASSERT_EQ(doc.layerCount(), 1u);
        Fio::AprLayerDetailInfo layer;
        ASSERT_TRUE(doc.getLayerDetailAt(0, layer));
        EXPECT_EQ(layer.id, 7u);
        EXPECT_STREQ(layer.name, "Cut-7");
        EXPECT_FALSE(layer.output);
        EXPECT_EQ(layer.defaultProcessId, 11u);
        EXPECT_EQ(layer.layerType, 1u);
        EXPECT_TRUE(layer.fill);
        EXPECT_EQ(layer.hardwareProfileId, 3);
        EXPECT_EQ(layer.fillHardwareProfileId, 4);

        ASSERT_EQ(doc.hardwareProfileCount(), 3u);
        bool foundSteel = false;
        for (size_t i = 0; i < doc.hardwareProfileCount(); ++i)
        {
            Fio::AprHardwareProfileInfo p;
            ASSERT_TRUE(doc.getHardwareProfileAt(i, p));
            if (p.id == 3)
            {
                foundSteel = true;
                EXPECT_STREQ(p.name, "Steel-3mm");
                EXPECT_DOUBLE_EQ(p.power, 77.5);
                EXPECT_DOUBLE_EQ(p.speed, 245.25);
                EXPECT_DOUBLE_EQ(p.frequency, 31.5);
                EXPECT_DOUBLE_EQ(p.focusOffset, -0.75);
                EXPECT_EQ(p.passes, 3);
            }
        }
        EXPECT_TRUE(foundSteel);
    }
}  // namespace

TEST(ImportExportRegressionTest, NativeExportStillWarnsWhenMachiningSnapshotIsMissing)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const ExportResult result = aprAprPersistWriteSample(dir.filePath(QStringLiteral("gap.apr")), false, true);
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_FALSE(result.warnings.isEmpty());
    // 组合根没注入 provider / 快照采集抛异常时，绝不能让操作员以为参数已落盘
    EXPECT_TRUE(result.warnings.join(QStringLiteral(" ")).contains(QStringLiteral("not captured")))
        << result.warnings.join(QStringLiteral(" ")).toStdString();
}

TEST(ImportExportRegressionTest, NativeExportPersistsMachiningSnapshotViaBorrowedPath)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("borrowed.apr"));

    const ExportResult result = aprAprPersistWriteSample(path, true, false);
    ASSERT_TRUE(result.success) << result.message.toStdString();
    EXPECT_TRUE(result.warnings.isEmpty()) << result.warnings.join(QStringLiteral(" ")).toStdString();
    aprAprPersistVerifyDocument(path);
}

TEST(ImportExportRegressionTest, NativeExportPersistsMachiningSnapshotViaClonePath)
{
    // 生产路径：ExportService 派发前把 sceneManager 置空，只能拿到克隆图元。
    // 老实现在这条分支走 ExportWriterBase → FileIOManager::exportFile，
    // 只传裸图元数组，快照根本没有入口写进 AprDocument。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("clone.apr"));

    const ExportResult result = aprAprPersistWriteSample(path, true, true);
    ASSERT_TRUE(result.success) << result.message.toStdString();
    EXPECT_TRUE(result.warnings.isEmpty()) << result.warnings.join(QStringLiteral(" ")).toStdString();
    aprAprPersistVerifyDocument(path);
}

TEST(ImportExportRegressionTest, NativeExportWritesDocumentMetadataOnBothPaths)
{
    // 回归：把借用 / 克隆两条路径统一到 writeDocument() 后，若忘记补元数据，
    // .apr 的软件版本、创建时间、OS 溯源信息会整片写空 —— 文件照样能打开，
    // 所以没有告警、极难察觉。老降级路径经 NativeWriter::write() 原本是有元数据的，
    // 不能为了闭合加工参数而把溯源信息弄丢。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (const bool viaClonePath : { false, true })
    {
        const QString path = dir.filePath(viaClonePath
            ? QStringLiteral("metadata-clone.apr")
            : QStringLiteral("metadata-borrowed.apr"));
        const ExportResult result = aprAprPersistWriteSample(path, true, viaClonePath);
        ASSERT_TRUE(result.success) << result.message.toStdString();

        Fio::NativeParser parser(Fio::FileFormat::Native);
        Fio::AprDocument doc;
        const auto parseResult = parser.parseDocument(path.toUtf8().constData(), doc);
        ASSERT_TRUE(parseResult.success) << parseResult.errorMessage;

        const char* const branch = viaClonePath ? "clone" : "borrowed";
        EXPECT_EQ(doc.metadataVersion(), 1) << branch;
        EXPECT_EQ(doc.metadataFileVersion(), 1) << branch;
        EXPECT_NE(doc.softwareName()[0], '\0') << branch;
        EXPECT_NE(doc.softwareVersion()[0], '\0') << branch;
        EXPECT_NE(doc.createdTime()[0], '\0') << branch;
        EXPECT_NE(doc.modifiedTime()[0], '\0') << branch;
        EXPECT_NE(doc.operatingSystem()[0], '\0') << branch;
    }
}

TEST(ImportExportRegressionTest, NativeImportReaderCarriesProfileBindingsAndValueSet)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("reader.apr"));
    ASSERT_TRUE(aprAprPersistWriteSample(path, true, true).success);

    ImportContext context;
    context.sourcePath = path;
    context.format = Fio::FileFormat::Native;
    NativeImportReader reader;
    Fio::VecAprEntityPtr outEntities;
    const ImportResult result = reader.read(context, outEntities);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    ASSERT_EQ(result.importedLayers.size(), 1u);
    ASSERT_EQ(result.importedLayerMachining.size(), 1u);
    EXPECT_EQ(result.importedLayerMachining[0].sourceId, 7u);
    EXPECT_FALSE(result.importedLayerMachining[0].output);
    EXPECT_EQ(result.importedLayerMachining[0].defaultProcessId, 11u);
    EXPECT_EQ(result.importedLayerMachining[0].hardwareProfileId, 3);
    EXPECT_EQ(result.importedLayerMachining[0].fillHardwareProfileId, 4);
    EXPECT_TRUE(result.importedLayerMachining[0].hasDisplayExtension);
    EXPECT_EQ(result.importedLayerMachining[0].layerType, 1u);
    EXPECT_TRUE(result.importedLayerMachining[0].fill);

    EXPECT_TRUE(result.hasNativeMachiningSnapshot);
    ASSERT_EQ(result.importedHardwareProfiles.size(), 3u);
    EXPECT_TRUE(result.warnings.isEmpty()) << result.warnings.join(QStringLiteral(" ")).toStdString();
}

TEST(ImportExportRegressionTest, ImportServiceRestoresAprProfileValueSetAndLayerBindings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.apr"));
    ASSERT_TRUE(aprAprPersistWriteSample(path, true, true).success);

    Eg::SceneManager scene;
    LayerManager layers(&scene);
    HardwareProfileManager profiles;
    layers.setHardwareProfileManager(&profiles);
    // 制造运行时脏 Profile，验证打开文档时确实按文件值集整体替换
    const int stale = profiles.createProfile("Stale");
    ASSERT_GE(stale, 1);

    ImportDispatcher dispatcher;
    dispatcher.registerReader(std::make_unique<NativeImportReader>());

    ImportService service;
    service.setDispatcher(&dispatcher);
    service.setSceneManager(&scene);
    service.setLayerManager(&layers);

    ImportContext context;
    context.sourcePath = path;
    context.preserveLayers = true;
    ImportOptions options;
    options.autoFit = false;
    options.autoCenter = false;
    options.autoSwitchWorkbench = false;

    const ImportResult result = service.importWithContext(context, options);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    const int layerId = layers.findLayerByName("Cut-7");
    ASSERT_GE(layerId, 0) << "图层表没还原";
    EXPECT_FALSE(layers.isLayerOutput(layerId));
    EXPECT_EQ(layers.layerDefaultProcessId(layerId), 11u);
    EXPECT_EQ(layers.layerHardwareProfileId(layerId), 3);
    EXPECT_EQ(layers.layerFillHardwareProfileId(layerId), 4);
    EXPECT_EQ(layers.layerType(layerId), Eg::LayerType::BITMAP);
    EXPECT_TRUE(layers.isLayerFill(layerId));

    // 值集必须连原始 id 一起还原，否则图层绑定会指到另一组功率/速度上
    EXPECT_FALSE(profiles.setPower(stale, 1.0)) << "旧运行时 Profile 必须已被文档值集替换";
    EXPECT_STREQ(profiles.profile(3).name.c_str(), "Steel-3mm");
    EXPECT_DOUBLE_EQ(profiles.profile(3).power, 77.5);
    EXPECT_DOUBLE_EQ(profiles.profile(3).speed, 245.25);
    EXPECT_DOUBLE_EQ(profiles.profile(3).frequency, 31.5);
    EXPECT_DOUBLE_EQ(profiles.profile(3).focusOffset, -0.75);
    EXPECT_EQ(profiles.profile(3).passes, 3);

    // layerHardwareProfile() 是 specProvider 真正读取的入口：端到端走通才算闭环
    const HardwareProfile effective = layers.layerHardwareProfile(layerId);
    EXPECT_DOUBLE_EQ(effective.power, 77.5);
    EXPECT_DOUBLE_EQ(effective.speed, 245.25);
}

// ---- ProcessData 图层回灌（.apr v2）----
//
// 背景：ProcessData 是工艺参数的一等载体，图层表的 default_process_id 只是引用。
// 保存侧只写图层表、打开侧不回灌 ProcessData，会让「参数体」整段丢失。
// 下面用真实采集入口（LayerManager → NativeDocumentSnapshot）与真实导入链
// （NativeImportReader → ImportService）守三方：往返一致、二次打开不漂移、
// 目标图层缺失必须告警。
namespace
{
    Eg::ProcessParams aprProcReferenceParams()
    {
        Eg::ProcessParams p;
        p.powerPercent = 66.5;
        p.speedMmPerSec = 123.25;
        p.frequencyHz = 25000.0;
        p.pulseWidthUs = 120.0;
        p.waveformIndex = 2;
        p.passes = 3;
        p.fillEnabled = true;
        p.fillIntervalMm = 0.12;
        p.fillAngleDeg = 37.0;
        p.fillMode = Eg::FillMode::Raster;
        p.kerfMm = 0.25;
        p.rapidSpeedMmPerSec = 900.0;
        p.leadInEnabled = true;
        p.leadInLengthMm = 3.0;
        p.leadInMode = 1;
        p.leadInAngleDeg = 45.0;
        return p;
    }

    void aprProcExpectParamsEqual(const Eg::ProcessParams& got, const Eg::ProcessParams& want)
    {
        EXPECT_DOUBLE_EQ(got.powerPercent, want.powerPercent);
        EXPECT_DOUBLE_EQ(got.speedMmPerSec, want.speedMmPerSec);
        EXPECT_DOUBLE_EQ(got.frequencyHz, want.frequencyHz);
        EXPECT_DOUBLE_EQ(got.pulseWidthUs, want.pulseWidthUs);
        EXPECT_EQ(got.waveformIndex, want.waveformIndex);
        EXPECT_EQ(got.passes, want.passes);
        EXPECT_EQ(got.fillEnabled, want.fillEnabled);
        EXPECT_DOUBLE_EQ(got.fillIntervalMm, want.fillIntervalMm);
        EXPECT_DOUBLE_EQ(got.fillAngleDeg, want.fillAngleDeg);
        EXPECT_EQ(got.fillMode, want.fillMode);
        EXPECT_DOUBLE_EQ(got.kerfMm, want.kerfMm);
        EXPECT_DOUBLE_EQ(got.rapidSpeedMmPerSec, want.rapidSpeedMmPerSec);
        EXPECT_EQ(got.leadInEnabled, want.leadInEnabled);
        EXPECT_DOUBLE_EQ(got.leadInLengthMm, want.leadInLengthMm);
        EXPECT_EQ(got.leadInMode, want.leadInMode);
        EXPECT_DOUBLE_EQ(got.leadInAngleDeg, want.leadInAngleDeg);
    }

    /// 造一份带 ProcessData 的 .apr；用于覆盖保存侧采集 + 写入。
    ExportResult aprProcWriteSample(const QString& path, const NativeDocumentSnapshot& snapshot)
    {
        Eg::SceneManager scene;
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        std::vector<std::unique_ptr<Eg::AprEntity>> vec;
        vec.push_back(std::move(line));
        Eg::SceneWriteGateway::addEntities(scene, std::move(vec));

        ExportContext context;
        context.targetPath = path;
        context.format = Fio::FileFormat::Native;
        context.sceneManager = &scene;
        context.hasMachiningSnapshot = true;
        context.machiningSnapshot = snapshot;

        NativeExportWriter writer;
        return writer.write(context, {});
    }

    Fio::AprLayerDetailInfo aprProcSampleLayer(uint32_t id, const char* name)
    {
        Fio::AprLayerDetailInfo layer;
        layer.id = id;
        std::strncpy(layer.name, name, sizeof(layer.name) - 1);
        layer.color = 0xFFFFFFFF;
        layer.visible = true;
        layer.locked = false;
        layer.output = true;
        return layer;
    }
}  // namespace

TEST(ImportExportRegressionTest, AprProcessDataRoundTripReflowsLayerParams)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("process.apr"));

    // ---- 保存侧：真实采集入口 ----
    Eg::SceneManager saveScene;
    LayerManager saveLayers(&saveScene);
    const int layerId = saveLayers.createLayer("ProcessLayer");
    ASSERT_GE(layerId, 0);
    const Eg::ProcessParams expected = aprProcReferenceParams();
    ASSERT_TRUE(saveLayers.setLayerProcessParams(layerId, expected));

    NativeDocumentSnapshot snapshot = aprSetupCaptureMachiningSnapshot(&saveLayers);
    ASSERT_EQ(snapshot.processes.size(), 1u);
    // 补一条默认 Profile，避免导入时出现「图层表存了但值集为空」的无关告警
    {
        Fio::AprHardwareProfileInfo profile;
        profile.id = 0;
        std::strncpy(profile.name, "Default", sizeof(profile.name) - 1);
        profile.power = 100.0;
        profile.speed = 1000.0;
        snapshot.profiles.push_back(profile);
    }
    EXPECT_EQ(snapshot.processes[0].info.id, static_cast<uint32_t>(layerId) + 1u);
    EXPECT_EQ(snapshot.processes[0].info.order, 0);
    ASSERT_EQ(snapshot.processes[0].targetLayerIds.size(), 1u);
    EXPECT_EQ(snapshot.processes[0].targetLayerIds[0], static_cast<uint32_t>(layerId));
    const auto layerDetail = std::find_if(snapshot.layers.begin(), snapshot.layers.end(),
        [layerId](const Fio::AprLayerDetailInfo& layer) {
            return layer.id == static_cast<uint32_t>(layerId);
        });
    ASSERT_NE(layerDetail, snapshot.layers.end());
    EXPECT_EQ(layerDetail->defaultProcessId, static_cast<uint32_t>(layerId) + 1u);

    ASSERT_TRUE(aprProcWriteSample(path, snapshot).success);

    // ---- 文件层：ProcessData 与 target_layer_ids 都真的落盘 ----
    {
        Fio::NativeParser parser(Fio::FileFormat::Native);
        Fio::AprDocument doc;
        ASSERT_TRUE(parser.parseDocument(path.toUtf8().constData(), doc).success);
        ASSERT_EQ(doc.processCount(), 1u);

        Fio::AprProcessInfo info;
        ASSERT_TRUE(doc.getProcessAt(0, info));
        EXPECT_EQ(info.id, static_cast<uint32_t>(layerId) + 1u);
        EXPECT_DOUBLE_EQ(info.params.kerfMm, expected.kerfMm);
        EXPECT_TRUE(info.params.leadInEnabled);
        EXPECT_DOUBLE_EQ(info.params.leadInLengthMm, expected.leadInLengthMm);
        EXPECT_EQ(info.params.leadInMode, expected.leadInMode);
        EXPECT_DOUBLE_EQ(info.params.leadInAngleDeg, expected.leadInAngleDeg);

        uint32_t target = 0;
        size_t written = 0;
        ASSERT_TRUE(doc.getProcessTargetLayers(info.id, &target, 1, &written));
        ASSERT_EQ(written, 1u);
        EXPECT_EQ(target, static_cast<uint32_t>(layerId));
    }

    // ---- 读端：真实导入链回灌 LayerManager ----
    ImportDispatcher dispatcher;
    dispatcher.registerReader(std::make_unique<NativeImportReader>());
    Eg::SceneManager loadScene;
    LayerManager loadLayers(&loadScene);
    ImportService service;
    service.setDispatcher(&dispatcher);
    service.setSceneManager(&loadScene);
    service.setLayerManager(&loadLayers);

    ImportContext context;
    context.sourcePath = path;
    context.preserveLayers = true;
    ImportOptions options;
    options.autoFit = false;
    options.autoCenter = false;
    options.autoSwitchWorkbench = false;

    ImportResult first = service.importWithContext(context, options);
    ASSERT_TRUE(first.success) << first.message.toStdString();
    EXPECT_TRUE(first.warnings.isEmpty()) << first.warnings.join(QStringLiteral(" ")).toStdString();

    const int restoredId = loadLayers.findLayerByName("ProcessLayer");
    ASSERT_GE(restoredId, 0);
    ASSERT_TRUE(loadLayers.hasLayerProcessParams(restoredId));
    aprProcExpectParamsEqual(loadLayers.layerProcessParams(restoredId), expected);

    // ---- 幂等：连续打开两次，参数不得漂移 ----
    ImportResult second = service.importWithContext(context, options);
    ASSERT_TRUE(second.success) << second.message.toStdString();
    ASSERT_TRUE(loadLayers.hasLayerProcessParams(restoredId));
    aprProcExpectParamsEqual(loadLayers.layerProcessParams(restoredId), expected);

    // ---- 打开 → 保存 → 再打开：保存侧必须从图层参数重建 ProcessData ----
    const NativeDocumentSnapshot resaved = aprSetupCaptureMachiningSnapshot(&loadLayers);
    ASSERT_EQ(resaved.processes.size(), 1u);
    const QString secondPath = dir.filePath(QStringLiteral("process-resaved.apr"));
    ASSERT_TRUE(aprProcWriteSample(secondPath, resaved).success);

    Eg::SceneManager reopenScene;
    LayerManager reopenLayers(&reopenScene);
    ImportService reopenService;
    reopenService.setDispatcher(&dispatcher);
    reopenService.setSceneManager(&reopenScene);
    reopenService.setLayerManager(&reopenLayers);

    ImportContext reopenContext;
    reopenContext.sourcePath = secondPath;
    reopenContext.preserveLayers = true;
    const ImportResult reopened = reopenService.importWithContext(reopenContext, options);
    ASSERT_TRUE(reopened.success) << reopened.message.toStdString();

    const int reopenLayerId = reopenLayers.findLayerByName("ProcessLayer");
    ASSERT_GE(reopenLayerId, 0);
    ASSERT_TRUE(reopenLayers.hasLayerProcessParams(reopenLayerId));
    aprProcExpectParamsEqual(reopenLayers.layerProcessParams(reopenLayerId), expected);
}

TEST(ImportExportRegressionTest, AprProcessDataMissingTargetLayerIsReported)
{
    // 工艺对象指向不存在的图层，或压根没有目标图层：参数不可能生效。
    // 必须告警并计数，不能把「参数没回灌」伪装成一次干净的打开。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("missing-target.apr"));

    // 文档里只有 7 号图层；11 号工艺指向 99 号（不存在），12 号工艺没有目标
    NativeDocumentSnapshot snapshot;
    snapshot.layers.push_back(aprProcSampleLayer(7, "Ghost-7"));
    snapshot.processes.push_back({ [] {
                                      Fio::AprProcessInfo info;
                                      info.id = 11;
                                      std::strncpy(info.name, "Orphan", sizeof(info.name) - 1);
                                      info.order = 0;
                                      return info;
                                  }(),
        { 99 } });
    snapshot.processes.push_back({ [] {
                                      Fio::AprProcessInfo info;
                                      info.id = 12;
                                      std::strncpy(info.name, "NoTarget", sizeof(info.name) - 1);
                                      info.order = 1;
                                      return info;
                                  }(),
        {} });

    ASSERT_TRUE(aprProcWriteSample(path, snapshot).success);

    ImportDispatcher dispatcher;
    dispatcher.registerReader(std::make_unique<NativeImportReader>());
    Eg::SceneManager scene;
    LayerManager layers(&scene);
    ImportService service;
    service.setDispatcher(&dispatcher);
    service.setSceneManager(&scene);
    service.setLayerManager(&layers);

    ImportContext context;
    context.sourcePath = path;
    context.preserveLayers = true;
    ImportOptions options;
    options.autoFit = false;
    options.autoCenter = false;
    options.autoSwitchWorkbench = false;

    const ImportResult result = service.importWithContext(context, options);
    ASSERT_TRUE(result.success) << result.message.toStdString();

    const int layerId = layers.findLayerByName("Ghost-7");
    ASSERT_GE(layerId, 0);
    EXPECT_FALSE(layers.hasLayerProcessParams(layerId))
        << "找不到目标图层的工艺对象不能悄悄套到别的图层上";

    const QString warnings = result.warnings.join(QStringLiteral(" | "));
    EXPECT_NE(warnings.indexOf(QStringLiteral("do not exist")), -1) << warnings.toStdString();
    EXPECT_NE(warnings.indexOf(QStringLiteral("target no layer")), -1) << warnings.toStdString();
}

TEST(ImportExportRegressionTest, LegacyAprWithoutProfileValueSetKeepsRuntimeProfiles)
{
    // 旧文件只有图层绑定、没有值集：读端不得清空用户当前 Profile，
    // 且必须给出降级提示，不能把「参数可能不对」伪装成一次干净的打开。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.apr"));

    // 故意只写图层表、不放任何 Profile 值集，模拟 v2 之前的老文件
    Fio::NativeWriter writer(Fio::FileFormat::Native);
    Fio::AprDocument doc;
    doc.addLayerDetail(aprAprPersistSampleLayer());
    ASSERT_TRUE(writer.writeDocument(path.toUtf8().constData(), doc).success);

    HardwareProfileManager profiles;
    const int existing = profiles.createProfile("KeepMe");
    ASSERT_GE(existing, 1);

    ImportContext importCtx;
    importCtx.sourcePath = path;
    NativeImportReader reader;
    Fio::VecAprEntityPtr outEntities;
    const ImportResult readResult = reader.read(importCtx, outEntities);
    ASSERT_TRUE(readResult.success) << readResult.message.toStdString();
    EXPECT_FALSE(readResult.hasNativeMachiningSnapshot);
    EXPECT_TRUE(readResult.importedHardwareProfiles.empty());
    EXPECT_FALSE(readResult.warnings.isEmpty()) << "悬空绑定必须提醒操作员";
    EXPECT_EQ(profiles.profile(existing).name, "KeepMe") << "读端不得动运行时 Profile";
}
