/**
 * @file ImportExportServiceSetup.cpp
 * @brief 导入/导出服务装配实现（从 ApplicationCompositionRoot 提取）
 */
#include "ImportExportServiceSetup.h"
#include "DocumentPersistenceHelper.h"

#include <QCoreApplication>

#include "UI/Services/UiServices.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Service/ViewCaptureService.h"

#include "Import/ImportService.h"
#include "Import/ImportDispatcher.h"
#include "Import/Readers/DxfImportReader.h"
#include "Import/Readers/SvgImportReader.h"
#include "Import/Readers/PdfImportReader.h"
#include "Import/Readers/StepImportReader.h"
#include "Import/Readers/ObjImportReader.h"
#include "Import/Readers/StlImportReader.h"
#include "Import/Readers/PltImportReader.h"
#include "Import/Readers/AiImportReader.h"
#include "Import/Readers/NativeImportReader.h"
#include "Import/Readers/UgImportReader.h"

#include "Export/ExportService.h"
#include "Export/ExportDispatcher.h"
#include "Export/Writers/DxfExportWriter.h"
#include "Export/Writers/SvgExportWriter.h"
#include "Export/Writers/PdfExportWriter.h"
#include "Export/Writers/BmpExportWriter.h"
#include "Export/Writers/PngExportWriter.h"
#include "Export/Writers/ObjExportWriter.h"
#include "Export/Writers/StepExportWriter.h"
#include "Export/Writers/NativeExportWriter.h"

#include "Engine2D/Import/Fio2DEntityConverter.h"
#include "Engine3D/Import/FioEntityConverter.h"

#include "Log/SyLogger.h"

namespace
{
    // 共享连接：开始信号 → setBusy(true)
    template<typename ServiceT>
    void connectStartToBusy(UiStateCenter* stateCenter, ServiceT* service, void (ServiceT::*started)(const QString&))
    {
        QObject::connect(service, started, stateCenter, [stateCenter]() {
            if (stateCenter)
            {
                stateCenter->pushBusy();
            }
        });
    }

    // 共享回调：busy 状态 + 状态提示 → UiStateCenter（适用于 ImportService/ExportService）
    template<typename ServiceT>
    void wireCommonCallbacks(UiStateCenter* stateCenter, ServiceT* service)
    {
        service->setBusyStateCallback([stateCenter](bool busy) {
            if (stateCenter)
            {
                if (busy)
                {
                    stateCenter->pushBusy();
                }
                else
                {
                    stateCenter->popBusy();
                }
            }
        });
        service->setStatusPromptCallback([stateCenter](const QString& prompt) {
            if (stateCenter)
            {
                stateCenter->setStatusPrompt(prompt);
            }
        });
    }

    // 共享连接：完成信号 → setBusy(false) + 状态提示
    template<typename ServiceT, typename ResultT, typename CountT>
    void connectFinishToBusy(UiStateCenter* stateCenter,
        ServiceT* service,
        void (ServiceT::*finished)(const ResultT&),
        CountT count,
        const QString& completeText,
        const QString& failText)
    {
        QObject::connect(service, finished, stateCenter, [stateCenter, count, completeText, failText](
                                                             const ResultT& result) {
            if (!stateCenter)
            {
                return;
            }
            stateCenter->popBusy();
            QString text = result.success ? completeText.arg(count(result)) : failText.arg(result.message);
            if (!result.warnings.isEmpty())
            {
                text += QStringLiteral(" (") + result.warnings.join(QStringLiteral("; ")) + QStringLiteral(")");
            }
            stateCenter->setStatusPrompt(text);
            QVariantMap meta = stateCenter->metadata();
            meta["statusPrompt"] = text;
            meta["notificationType"] = result.success ? "info" : "error";
            stateCenter->setMetadata(meta);
        });
    }
}  // namespace

ImportExportServiceSetup::ImportExportServiceSetup(UiStateCenter* stateCenter,
    ImportService* importService,
    ImportDispatcher* importDispatcher,
    ExportService* exportService,
    ExportDispatcher* exportDispatcher,
    Eg::SceneManager* sceneManager,
    Eg::SceneManager3D* sceneManager3D,
    SceneEditService* editService,
    LayerManager* layerManager,
    PersistenceService* persistence,
    Ui::ViewCaptureService* captureService)
    : m_stateCenter(stateCenter)
    , m_importService(importService)
    , m_importDispatcher(importDispatcher)
    , m_exportService(exportService)
    , m_exportDispatcher(exportDispatcher)
    , m_sceneManager(sceneManager)
    , m_sceneManager3D(sceneManager3D)
    , m_editService(editService)
    , m_layerManager(layerManager)
    , m_persistence(persistence)
    , m_captureService(captureService)
{
}

void ImportExportServiceSetup::setup(UiServices& uiServices)
{
    registerReaders();
    registerWriters();
    wireCallbacks();

    // 注入到 UI 服务集合。导出服务不进 UiServices：唯一的消费者
    // FileOperationRegistry 走 FileOperationConfig::exportService 单独注入。
    uiServices.importService = m_importService;
    // 视图导出服务：2D 与 3D 的「导出视图」命令都要用同一个实例，
    // 保证两次导出的落盘目录与命名规则一致。
    uiServices.captureService = m_captureService;

    connectProgressSignals();
}

void ImportExportServiceSetup::registerReaders()
{
    m_importDispatcher->registerReader(std::make_unique<DxfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<SvgImportReader>());
    m_importDispatcher->registerReader(std::make_unique<PdfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<StepImportReader>());
    m_importDispatcher->registerReader(std::make_unique<ObjImportReader>());
    m_importDispatcher->registerReader(std::make_unique<StlImportReader>());
    m_importDispatcher->registerReader(std::make_unique<PltImportReader>());
    // 新增导入格式：Adobe Illustrator (AI) 与 Unigraphics/NX (UG, 经 IGES)
    m_importDispatcher->registerReader(std::make_unique<AiImportReader>());
    m_importDispatcher->registerReader(std::make_unique<NativeImportReader>());
    m_importDispatcher->registerReader(std::make_unique<UgImportReader>());
    SY_DEBUGF("[ImportDispatcher] Registered %lld reader(s): %s",
        static_cast<long long>(m_importDispatcher->supportedExtensions().size()),
        m_importDispatcher->supportedExtensions().join(QLatin1String(", ")).toUtf8().constData());
}

void ImportExportServiceSetup::registerWriters()
{
    m_exportDispatcher->registerWriter(std::make_unique<DxfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<SvgExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PdfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<BmpExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PngExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<ObjExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<StepExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<NativeExportWriter>());
    SY_DEBUGF("[ExportDispatcher] Registered %lld writer(s): %s",
        static_cast<long long>(m_exportDispatcher->supportedExtensions().size()),
        m_exportDispatcher->supportedExtensions().join(QLatin1String(", ")).toUtf8().constData());
}

void ImportExportServiceSetup::wireCallbacks()
{
    // 配置导入服务
    m_importService->setDispatcher(m_importDispatcher);
    m_importService->setSceneManager(m_sceneManager);
    m_importService->setSceneManager3D(m_sceneManager3D);
    m_importService->setEditService(m_editService);

    // 注册 2D 图元工厂：Engine3D 的 FioEntityConverter 只构造 Mesh3D，
    // 2D 图元（Line/Arc/Circle/... ）委托给 Engine2D。组合根同时能看到两侧，
    // 这是打破 Engine3D → Engine2D 依赖的唯一注册点，必须在任何导入之前完成。
    Eg::FioEntityConverter::set2DEntityFactory(&Eg::convertFio2DEntity);

    // 注入图层管理器：DXF 等导入后按源图层表还原图层结构
    m_importService->setLayerManager(m_layerManager);
    wireCommonCallbacks(m_stateCenter, m_importService);

    m_importService->setDocumentPersistenceCallback(
        [persistence = m_persistence](const QString& filePath, int entityCount, const QString& workbenchId) {
            return DocumentPersistenceHelper::recordImport(persistence, filePath, entityCount, workbenchId);
        });

    // 配置导出服务
    m_exportService->setDispatcher(m_exportDispatcher);
    m_exportService->setSceneManager(m_sceneManager);
    m_exportService->setSceneManager3D(m_sceneManager3D);
    m_exportService->setPersistenceService(m_persistence);
    wireCommonCallbacks(m_stateCenter, m_exportService);
}

void ImportExportServiceSetup::connectProgressSignals()
{
    // 导入进度 → 状态中心
    connectStartToBusy(m_stateCenter, m_importService, &ImportService::importStarted);
    connectFinishToBusy(
        m_stateCenter,
        m_importService,
        &ImportService::importFinished,
        [](const ImportResult& r) {
            return r.entityCount;
        },
        QCoreApplication::translate("ImportService", "Import completed: %1 entities"),
        QCoreApplication::translate("ImportService", "Import failed: %1"));

    // 导出进度 → 状态中心
    connectStartToBusy(m_stateCenter, m_exportService, &ExportService::exportStarted);
    connectFinishToBusy(
        m_stateCenter,
        m_exportService,
        &ExportService::exportFinished,
        [](const ExportResult& r) {
            return r.exportedEntityCount;
        },
        QCoreApplication::translate("ExportService", "Export completed: %1 entities"),
        QCoreApplication::translate("ExportService", "Export failed: %1"));
}
