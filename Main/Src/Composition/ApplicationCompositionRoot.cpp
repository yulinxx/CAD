#include "ApplicationCompositionRoot.h"

#include "../UI/UiLayoutService.h"
#include "../UI/UiServices.h"
#include "../UI/UiShellHost.h"
#include "../UI/WorkbenchWindow.h"
#include "../UI/UiStateCenter.h"
#include "../UI/UiThemeService.h"
#include "../Common/AppInitializer.h"
#include "../Persistence/LayerPersistenceBridge.h"
#include "../Persistence/PersistenceService.h"
#include "../Persistence/Models/DocumentRecord.h"
#include "../Persistence/Repositories/DocumentRepository.h"

#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Log/SyLogger.h"

#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/FilletChamfer.h"
#include "UI/FileDialogService.h"
#include "UI/RecentFileService.h"
#include "UI/HelpDialogService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "Import/ImportService.h"
#include "Import/ImportDispatcher.h"
#include "Import/Readers/DxfImportReader.h"
#include "Import/Readers/SvgImportReader.h"
#include "Import/Readers/PdfImportReader.h"
#include "Import/Readers/StepImportReader.h"
#include "Import/Readers/ObjImportReader.h"
#include "Export/ExportService.h"
#include "Export/ExportDispatcher.h"
#include "Export/Writers/DxfExportWriter.h"
#include "Export/Writers/SvgExportWriter.h"
#include "Export/Writers/PdfExportWriter.h"
#include "Export/Writers/BmpExportWriter.h"
#include "Export/Writers/PngExportWriter.h"
#include "Export/Writers/ObjExportWriter.h"
#include "Export/Writers/StepExportWriter.h"

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::~ApplicationCompositionRoot() = default;

ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_interactionDispatcher(std::make_unique<DefaultInteractionDispatcher>())
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_operationBus(std::make_unique<OperationBus>())
    , m_sceneManager(std::make_unique<Eg::SceneManager>())
    , m_undoRedoManager(std::make_unique<UndoRedoManager>(m_sceneManager.get()))
    , m_sceneEditService(std::make_unique<SceneEditService>(m_sceneManager.get(), m_undoRedoManager.get()))
    , m_document2D(std::make_unique<SceneDocument2D>(m_sceneEditService.get()))
    , m_layerManager(std::make_unique<LayerManager>(m_sceneManager.get()))
    , m_layerManagerBridge(std::make_unique<QtLayerManagerBridge>(nullptr))
    , m_layerEditService(std::make_unique<LayerEditService>(m_layerManager.get(), m_undoRedoManager.get(), m_sceneManager.get()))
    , m_fileIOManager(std::make_unique<Fio::FileIOManager>())
    , m_importService(std::make_unique<ImportService>())
    , m_importDispatcher(std::make_unique<ImportDispatcher>())
    , m_exportService(std::make_unique<ExportService>())
    , m_exportDispatcher(std::make_unique<ExportDispatcher>())
{
    // 组装UI服务集合
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    uiServices.layerManager = m_layerManager.get();
    uiServices.layerManagerBridge = m_layerManagerBridge.get();
    uiServices.layerEditService = m_layerEditService.get();
    uiServices.persistenceService = persistenceService();

    // 将 LayerManagerBridge 注册为 LayerManager 的观察者
    m_layerManager->addObserver(m_layerManagerBridge.get());

    // 创建图层持久化桥接器（将运行态图层变更同步写入数据库）
    if (persistenceService() && persistenceService()->isOpen() && persistenceService()->layers())
    {
        m_layerPersistenceBridge = std::make_unique<LayerPersistenceBridge>(
            m_layerManager.get(), persistenceService()->layers());
        m_layerPersistenceBridge->attach();
        uiServices.layerPersistenceBridge = m_layerPersistenceBridge.get();
        SY_INFO("[ApplicationCompositionRoot] LayerPersistenceBridge attached");
    }

    // 将 LayerManager 注入到 SceneEditService，使其在添加实体时自动分配图层
    m_sceneEditService->setLayerManager(m_layerManager.get());

    // 配置UI壳宿主的核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setOperationBus(m_operationBus.get());

    // ---- 初始化导入/导出服务层 ----
    // 注册导入读取器
    m_importDispatcher->registerReader(std::make_unique<DxfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<SvgImportReader>());
    m_importDispatcher->registerReader(std::make_unique<PdfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<StepImportReader>());
    m_importDispatcher->registerReader(std::make_unique<ObjImportReader>());
    // 配置导入服务
    m_importService->setDispatcher(m_importDispatcher.get());
    m_importService->setSceneManager(m_sceneManager.get());
    m_importService->setEditService(m_sceneEditService.get());
    m_importService->setStateCenter(m_stateCenter.get());
    // 注册导出写入器
    m_exportDispatcher->registerWriter(std::make_unique<DxfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<SvgExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PdfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<BmpExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PngExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<ObjExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<StepExportWriter>());
    // 配置导出服务
    m_exportService->setDispatcher(m_exportDispatcher.get());
    m_exportService->setSceneManager(m_sceneManager.get());
    m_exportService->setStateCenter(m_stateCenter.get());
    // 将导入/导出服务注入到 UI 服务集合
    uiServices.importService = m_importService.get();
    uiServices.exportService = m_exportService.get();
    m_shellHost->setUiServices(uiServices);

    // 导入进度 → 状态中心
    QObject::connect(m_importService.get(), &ImportService::importStarted,
        m_stateCenter.get(), [this](const QString&) {
            if (m_stateCenter) m_stateCenter->setBusy(true);
        });
    QObject::connect(m_importService.get(), &ImportService::importFinished,
        m_stateCenter.get(), [this](const ImportResult& result) {
            if (!m_stateCenter) return;
            m_stateCenter->setBusy(false);
            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success
                ? QString("Import complete: %1 entities").arg(result.entityCount)
                : QString("Import failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";
            m_stateCenter->setMetadata(meta);
        });

    // 导出进度 → 状态中心
    QObject::connect(m_exportService.get(), &ExportService::exportStarted,
        m_stateCenter.get(), [this](const QString&) {
            if (m_stateCenter) m_stateCenter->setBusy(true);
        });
    QObject::connect(m_exportService.get(), &ExportService::exportFinished,
        m_stateCenter.get(), [this](const ExportResult& result) {
            if (!m_stateCenter) return;
            m_stateCenter->setBusy(false);
            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success
                ? QString("Export complete: %1 entities").arg(result.exportedEntityCount)
                : QString("Export failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";
            m_stateCenter->setMetadata(meta);
        });
    // ---- 导入/导出服务层初始化完成 ----

    // ---- UI 对话框服务 ----
    m_fileDialogService = std::make_unique<FileDialogService>();
    m_recentFileService = std::make_unique<RecentFileService>(persistenceService());
    m_helpDialogService = std::make_unique<HelpDialogService>();

    // 脏状态同步：场景变更 → 标记为未保存
    if (m_sceneManager)
    {
        m_sceneManager->setSceneChangedCallback([this]() {
            if (m_stateCenter && !m_stateCenter->dirty())
                m_stateCenter->setDirty(true);
            });
    }

    // 将 OperationBus 设为活动实例，供无上下文调用方使用
    OperationBus::setActiveInstance(m_operationBus.get());

    // 注册各模块操作到 OperationBus
    SY_INFO("[ApplicationCompositionRoot] registering module operations on OperationBus");
    registerCoreOperations();
    registerFileOperations();
    registerHelpOperations();
    registerPendingToolOperations();
    registerPendingAlgorithmOperations();

    SY_INFO("[ApplicationCompositionRoot] initialized with module-based operation registration");
}

UiShellHost* ApplicationCompositionRoot::shellHost()
{
    return m_shellHost.get();
}
UiStateCenter* ApplicationCompositionRoot::stateCenter()
{
    return m_stateCenter.get();
}
UiThemeService* ApplicationCompositionRoot::themeService()
{
    return m_themeService.get();
}
UiLayoutService* ApplicationCompositionRoot::layoutService()
{
    return m_layoutService.get();
}

IInteractionDispatcher* ApplicationCompositionRoot::interactionDispatcher()
{
    return m_interactionDispatcher.get();
}

OperationBus* ApplicationCompositionRoot::operationBus()
{
    return m_operationBus.get();
}

IUndoRedoManager* ApplicationCompositionRoot::undoRedoManager()
{
    return m_undoRedoManager.get();
}

LayerManager* ApplicationCompositionRoot::layerManager()
{
    return m_layerManager.get();
}

QtLayerManagerBridge* ApplicationCompositionRoot::layerManagerBridge()
{
    return m_layerManagerBridge.get();
}

LayerEditService* ApplicationCompositionRoot::layerEditService()
{
    return m_layerEditService.get();
}

LayerPersistenceBridge* ApplicationCompositionRoot::layerPersistenceBridge()
{
    return m_layerPersistenceBridge.get();
}

PersistenceService* ApplicationCompositionRoot::persistenceService()
{
    // 从 AppInitializer 获取已初始化的持久化服务
    if (!m_persistenceService)
        m_persistenceService = AppInitializer::persistenceService();
    return m_persistenceService;
}

// 注册缺失的工具切换操作
// 这些工具在旧框架中通过 ToolSwitchOperation 注册到 ViewWidget 工具系统
// 新框架 RenderViewport2D 暂未实现工具系统，此处注册占位提示
void ApplicationCompositionRoot::registerPendingToolOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();

    // 尚未实现的绘图工具占位
    const OperationId toolOps[] = {
        OperationId::Tool_Point,
        OperationId::Tool_Rectangle,
        OperationId::Tool_Ellipse,
        OperationId::Tool_Triangle,
        OperationId::Tool_Spline,
        OperationId::Tool_Text,
        OperationId::Tool_Bitmap,
        OperationId::Tool_QRCode,
    };
    for (const auto& opId : toolOps)
    {
        // 仅在 OperationBus 中未注册时才添加占位
        if (!reg.has(opId))
        {
            reg.registerOperation(std::make_unique<LambdaOperation>(
                opId, [opId] {
                    SY_WARNF("[PendingTool] OperationId=%d not yet implemented in new framework",
                        static_cast<int>(opId));
                }));
        }
    }

    SY_INFO("[ApplicationCompositionRoot] Registered pending tool operations (placeholders)");
}

// 注册缺失的算法/编辑操作
// 这些操作在旧框架中通过 OperationAlgo/OperationEdit 注册，依赖 AlgorithmRunner
// 新框架暂未接入 AlgorithmApplicationService，此处注册占位提示
void ApplicationCompositionRoot::registerPendingAlgorithmOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();

    const OperationId pendingOps[] = {
        // 算法操作
        OperationId::Algo_Fill,
        OperationId::Algo_Nesting,
        OperationId::Algo_Offset,
        OperationId::Algo_Array,
        OperationId::Algo_BooleanUnion,
        OperationId::Algo_BooleanIntersection,
        OperationId::Algo_BooleanDifference,
        OperationId::Algo_BooleanXor,
        OperationId::Algo_ReliefEngravingFromImage,
        // 编辑操作
        OperationId::Edit_Trim,
        OperationId::Edit_Extend,
        OperationId::Edit_Align,
        OperationId::Edit_Cut,
        OperationId::Edit_Paste,
        // Edit_GroupToggle, 已在 registerCoreOperations 中实现
        // Edit_Ungroup,     已在 registerCoreOperations 中实现
        OperationId::Edit_MirrorH,
        OperationId::Edit_MirrorV,
        // 视图操作
        OperationId::View_ZoomSelection,
        OperationId::View_Pan,
        OperationId::View_Reset,
        OperationId::View_GridVisible,
        OperationId::View_SnapEnabled,
        OperationId::View_OrthoMode,
        OperationId::View_AngleSnap,
        OperationId::View_LayerManager,
        OperationId::View_NewLayer,
        OperationId::View_DeleteLayer,
        OperationId::View_SetDisplayUnit,
    };

    int count = 0;
    for (const auto& opId : pendingOps)
    {
        if (!reg.has(opId))
        {
            reg.registerOperation(std::make_unique<LambdaOperation>(
                opId, [opId] {
                    SY_WARNF("[PendingOperation] OperationId=%d not yet implemented in new framework",
                        static_cast<int>(opId));
                }));
            ++count;
        }
    }

    SY_INFOF("[ApplicationCompositionRoot] Registered %d pending algorithm/edit/view operations (placeholders)", count);
}

void ApplicationCompositionRoot::registerCoreOperations()
{
    if (!m_operationBus || !m_sceneEditService || !m_undoRedoManager)
        return;

    auto& reg = m_operationBus->registry();
    auto* editService = m_sceneEditService.get();
    auto* undoManager = m_undoRedoManager.get();
    auto* helpDlg = m_helpDialogService.get();

    // ---- 撤销/重做 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Undo, [undoManager] {
            SY_INFO("[CoreOperations] Edit_Undo");
            if (undoManager && undoManager->canUndo())
                undoManager->undo();
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Redo, [undoManager] {
            SY_INFO("[CoreOperations] Edit_Redo");
            if (undoManager && undoManager->canRedo())
                undoManager->redo();
        }));

    // ---- 删除 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Delete, [editService] {
            SY_INFO("[CoreOperations] Edit_Delete");
            if (editService)
                editService->deleteSelected("Delete");
        }));

    // ---- 全选 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_SelectAll, [] {
            SY_INFO("[CoreOperations] Edit_SelectAll");
        }));

    // ---- 群组/取消群组 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_GroupToggle, [editService] {
            SY_INFO("[CoreOperations] Edit_GroupToggle");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Ungroup, [editService] {
            SY_INFO("[CoreOperations] Edit_Ungroup");
        }));

    // ---- 圆角 ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Fillet, [editService, helpDlg](const QVariantMap& params) {
            double radius = params.value("radius", -1.0).toDouble();
            if (radius < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    SY_WARN("[CoreOperations] Fillet: select >=2 lines first"); return;
                }
                bool ok = false;
                radius = HelpDialogService::getDouble(nullptr, QObject::tr("Fillet Radius"),
                    QObject::tr("Radius:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || radius < 0.1) return;
            }
            SY_INFOF("[CoreOperations] Edit_Fillet: radius=%f", radius);
            Eg::FilletChamfer::applyFillet(*editService, radius);
        }));

    // ---- 倒角 ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Chamfer, [editService, helpDlg](const QVariantMap& params) {
            double distance = params.value("distance", -1.0).toDouble();
            if (distance < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    SY_WARN("[CoreOperations] Chamfer: select >=2 lines first"); return;
                }
                bool ok = false;
                distance = HelpDialogService::getDouble(nullptr, QObject::tr("Chamfer Distance"),
                    QObject::tr("Distance:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || distance < 0.1) return;
            }
            SY_INFOF("[CoreOperations] Edit_Chamfer: distance=%f", distance);
            Eg::FilletChamfer::applyChamfer(*editService, distance);
        }));

    // ---- 视图操作 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomFit, [] {
            SY_INFO("[CoreOperations] View_ZoomFit");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomIn, [] {
            SY_INFO("[CoreOperations] View_ZoomIn");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomOut, [] {
            SY_INFO("[CoreOperations] View_ZoomOut");
        }));

    SY_INFO("[ApplicationCompositionRoot] Registered core edit operations on OperationBus");
}

static Fio::FileFormat operationIdToImportFormat(OperationId id)
{
    switch (id)
    {
        case OperationId::File_ImportDXF:  return Fio::FileFormat::DXF;
        case OperationId::File_ImportSVG:  return Fio::FileFormat::SVG;
        case OperationId::File_ImportPLT:  return Fio::FileFormat::PLT;
        case OperationId::File_ImportStep: return Fio::FileFormat::STEP;
        case OperationId::File_ImportPDF:  return Fio::FileFormat::PDF;
        default:                          return Fio::FileFormat::Unknown;
    }
}

static Fio::FileFormat operationIdToExportFormat(OperationId id)
{
    switch (id)
    {
        case OperationId::File_ExportDXF: return Fio::FileFormat::DXF;
        case OperationId::File_ExportSVG: return Fio::FileFormat::SVG;
        case OperationId::File_ExportPLT: return Fio::FileFormat::PLT;
        case OperationId::File_ExportBMP: return Fio::FileFormat::BMP;
        case OperationId::File_ExportPNG: return Fio::FileFormat::PNG;
        default:                         return Fio::FileFormat::Unknown;
    }
}

void ApplicationCompositionRoot::registerFileOperations()
{
    if (!m_operationBus || !m_sceneManager)
        return;

    auto& reg = m_operationBus->registry();
    auto* scene = m_sceneManager.get();
    auto* fileDlg = m_fileDialogService.get();
    auto* recentFiles = m_recentFileService.get();
    auto* helpDlg = m_helpDialogService.get();

    QWidget* parentWidget = m_shellHost ? m_shellHost->mainWindow() : nullptr;
    auto currentFilePath = std::make_shared<std::string>();

    auto saveDocumentRecord = [this](const std::string& filePath, int entityCount) {
        auto* pService = persistenceService();
        if (!pService || !pService->documents())
            return;
        auto existing = pService->documents()->loadByPath(filePath);
        QFileInfo fi(QString::fromStdString(filePath));
        QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
        DocumentRecord rec;
        rec.filePath = filePath;
        rec.title = fi.fileName().toStdString();
        rec.format = fi.suffix().toUpper().toStdString();
        rec.entityCount = entityCount;
        rec.lastSavedAt = now.toStdString();
        rec.lastOpenedAt = existing.id > 0 ? existing.lastOpenedAt : now.toStdString();
        rec.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();
        pService->documents()->save(rec);
        };

    auto doExport = [this, parentWidget, currentFilePath, saveDocumentRecord, recentFiles](
        const std::string& filePath) -> bool {
            if (!m_exportService)
                return false;
            ExportResult result = m_exportService->exportFile(QString::fromStdString(filePath));
            if (!result.success)
            {
                SY_ERRORF("[FileOperations] Export failed: %s", result.message.toUtf8().constData());
                HelpDialogService::showWarning(parentWidget, QObject::tr("Save Error"), result.message);
                return false;
            }
            SY_INFOF("[FileOperations] Exported %d entities to: %s", result.exportedEntityCount, filePath.c_str());
            *currentFilePath = filePath;
            saveDocumentRecord(filePath, result.exportedEntityCount);
            if (m_stateCenter)
            {
                m_stateCenter->setDirty(false);
                m_stateCenter->setCurrentDocumentId(QString::fromStdString(filePath));
            }
            if (m_layerPersistenceBridge)
                m_layerPersistenceBridge->setDocumentId(filePath);
            if (recentFiles)
                recentFiles->addRecentFile(QString::fromStdString(filePath));
            return true;
        };

    // ---- 新建文件 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_New, [this, scene, parentWidget] {
            SY_INFO("[FileOperations] File_New");
            bool needsSave = (m_stateCenter && m_stateCenter->dirty());
            if (needsSave)
            {
                auto result = HelpDialogService::showQuestion(
                    parentWidget, QObject::tr("Unsaved Changes"),
                    QObject::tr("Do you want to save the current file?"));
                if (result == QMessageBox::Cancel) return;
                if (result == QMessageBox::Yes)
                    m_operationBus->run(OperationId::File_Save, {});
            }
            scene->clearScene();
            if (m_stateCenter) m_stateCenter->setDirty(false);
            SY_INFO("[FileOperations] Scene cleared");
        }));

    auto doOpenFile = [this, parentWidget, currentFilePath, recentFiles](
        const QString& filePath) -> bool {
            if (filePath.isEmpty())
                return false;

            QFileInfo fi(filePath);
            SY_INFOF("[FileOperations] Opening: %s", filePath.toUtf8().constData());

            if (!m_importService)
                return false;

            ImportOptions opts;
            opts.importAsNewDocument = true;
            opts.autoFit = true;
            opts.autoSwitchWorkbench = false;
            ImportResult result = m_importService->importFile(filePath, opts);

            if (!result.success)
            {
                SY_ERRORF("[FileOperations] Import failed: %s", result.message.toUtf8().constData());
                HelpDialogService::showWarning(parentWidget, QObject::tr("Import Error"), result.message);
                return false;
            }

            SY_INFOF("[FileOperations] Imported %d entities from %s", result.entityCount, filePath.toUtf8().constData());

            if (m_persistenceService && m_persistenceService->documents())
            {
                auto existing = m_persistenceService->documents()->loadByPath(filePath.toStdString());
                QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
                DocumentRecord rec;
                rec.filePath = filePath.toStdString();
                rec.title = fi.fileName().toStdString();
                rec.format = fi.suffix().toUpper().toStdString();
                rec.entityCount = result.entityCount;
                rec.lastOpenedAt = now.toStdString();
                rec.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();
                m_persistenceService->documents()->save(rec);
            }

            if (recentFiles)
                recentFiles->addRecentFile(filePath);

            *currentFilePath = filePath.toStdString();

            if (m_stateCenter)
            {
                m_stateCenter->setDirty(false);
                m_stateCenter->setCurrentDocumentId(filePath);
            }

            if (m_layerPersistenceBridge)
                m_layerPersistenceBridge->setDocumentId(filePath.toStdString());

            return true;
        };

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Open, [parentWidget, doOpenFile] {
            SY_INFO("[FileOperations] File_Open");
            QString filePath = FileDialogService::getOpenFileName(
                parentWidget, QObject::tr("Open File"), FileDialogService::openFileFilter());
            doOpenFile(filePath);
        }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::File_OpenRecent, [doOpenFile](const QVariantMap& params) {
            SY_INFO("[FileOperations] File_OpenRecent");
            doOpenFile(params.value(QStringLiteral("filePath")).toString());
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Save, [scene, parentWidget, currentFilePath, doExport] {
            SY_INFO("[FileOperations] File_Save");
            if (currentFilePath->empty())
            {
                QString filePath = FileDialogService::getSaveFileName(
                    parentWidget, QObject::tr("Save"), FileDialogService::saveFileFilter());
                if (filePath.isEmpty()) return;
                doExport(filePath.toStdString());
            }
            else
            {
                doExport(*currentFilePath);
            }
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_SaveAs, [scene, parentWidget, currentFilePath, doExport] {
            SY_INFO("[FileOperations] File_SaveAs");
            QString filePath = FileDialogService::getSaveFileName(
                parentWidget, QObject::tr("Save As"), FileDialogService::saveFileFilter());
            if (!filePath.isEmpty()) doExport(filePath.toStdString());
        }));

    const OperationId importOps[] = {
        OperationId::File_ImportDXF, OperationId::File_ImportSVG,
        OperationId::File_ImportPLT, OperationId::File_ImportStep, OperationId::File_ImportPDF,
    };
    for (const auto& opId : importOps)
    {
        reg.registerOperation(std::make_unique<LambdaOperation>(
            opId, [this, parentWidget, opId] {
                auto fmt = operationIdToImportFormat(opId);
                QString filePath = FileDialogService::getOpenFileName(
                    parentWidget, QObject::tr("Import File"), FileDialogService::importFilterForFormat(fmt));
                if (filePath.isEmpty()) return;
                if (!m_importService) return;
                ImportOptions opts;
                opts.importAsNewDocument = false;
                opts.autoFit = true;
                ImportResult result = m_importService->importFile(filePath, opts);
                if (!result.success)
                {
                    HelpDialogService::showWarning(parentWidget, QObject::tr("Import Error"), result.message);
                    return;
                }
                SY_INFOF("[FileOperations] Imported %d entities", result.entityCount);
            }));
    }

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_ImportImage, [parentWidget] {
            SY_INFO("[FileOperations] File_ImportImage");
            QString filePath = FileDialogService::getOpenFileName(
                parentWidget, QObject::tr("Import Image"), FileDialogService::imageImportFilter());
            if (!filePath.isEmpty())
                SY_INFOF("[FileOperations] Image import path: %s", filePath.toUtf8().constData());
        }));

    const OperationId exportOps[] = {
        OperationId::File_ExportDXF, OperationId::File_ExportSVG,
        OperationId::File_ExportPLT, OperationId::File_ExportBMP, OperationId::File_ExportPNG,
    };
    for (const auto& opId : exportOps)
    {
        reg.registerOperation(std::make_unique<LambdaOperation>(
            opId, [this, parentWidget, opId] {
                auto fmt = operationIdToExportFormat(opId);
                QString filePath = FileDialogService::getSaveFileName(
                    parentWidget, QObject::tr("Export File"), FileDialogService::exportFilterForFormat(fmt));
                if (filePath.isEmpty()) return;
                if (!m_exportService) return;
                ExportResult result = m_exportService->exportFile(filePath);
                if (!result.success)
                {
                    HelpDialogService::showWarning(parentWidget, QObject::tr("Export Error"), result.message);
                    return;
                }
                SY_INFOF("[FileOperations] Exported %d entities to %s", result.exportedEntityCount, filePath.toUtf8().constData());
            }));
    }

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Exit, [parentWidget] {
            SY_INFO("[FileOperations] File_Exit");
            if (parentWidget) parentWidget->close();
        }));
}

void ApplicationCompositionRoot::registerHelpOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();
    QWidget* parentWidget = m_shellHost ? m_shellHost->mainWindow() : nullptr;

    // ---- Help: About ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_About, [parentWidget] {
            HelpDialogService::showAboutDialog(parentWidget);
        }));

    // ---- Help: Settings ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Settings, [parentWidget] {
            HelpDialogService::showSettingsDialog(parentWidget);
        }));

    // ---- Help: Documentation ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Docs, [parentWidget] {
            HelpDialogService::showDocumentationDialog(parentWidget);
        }));

    // ---- Help: Keyboard Shortcuts ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Shortcut, [parentWidget] {
            HelpDialogService::showShortcutsDialog(parentWidget);
        }));
}