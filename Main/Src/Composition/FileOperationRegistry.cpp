#include "FileOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "UI/Services/UiStateCenter.h"
#include "Engine2D/Core/SceneManager.h"
#include "UI/Services/FileDialogService.h"
#include "UI/Services/RecentFileService.h"
#include "UI/Services/HelpDialogService.h"
#include "Import/ImportService.h"
#include "Export/ExportService.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/LayerPersistenceBridge.h"
#include "Persistence/Models/DocumentRecord.h"
#include "Persistence/Repositories/DocumentRepository.h"
#include "FileIO/FileIOManager.h"

#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>

namespace
{
    Fio::FileFormat operationIdToImportFormat(OperationId id)
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

    Fio::FileFormat operationIdToExportFormat(OperationId id)
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
}

FileOperationRegistry::FileOperationRegistry(OperationBus* bus,
                                             Eg::SceneManager* sceneManager,
                                             ImportService* importService,
                                             ExportService* exportService,
                                             FileDialogService* fileDialog,
                                             RecentFileService* recentFiles,
                                             HelpDialogService* helpDialog,
                                             UiStateCenter* stateCenter,
                                             LayerPersistenceBridge* layerPersistence,
                                             PersistenceService* persistence,
                                             QWidget* parentWidget)
    : m_bus(bus)
    , m_sceneManager(sceneManager)
    , m_importService(importService)
    , m_exportService(exportService)
    , m_fileDialog(fileDialog)
    , m_recentFiles(recentFiles)
    , m_helpDialog(helpDialog)
    , m_stateCenter(stateCenter)
    , m_layerPersistence(layerPersistence)
    , m_persistence(persistence)
    , m_parentWidget(parentWidget)
{
}

void FileOperationRegistry::registerAll()
{
    if (!m_bus || !m_sceneManager)
        return;

    auto& reg = m_bus->registry();
    auto* scene = m_sceneManager;
    auto* fileDlg = m_fileDialog;
    auto* recentFiles = m_recentFiles;
    auto* helpDlg = m_helpDialog;

    auto currentFilePath = std::make_shared<std::string>();

    auto saveDocumentRecord = [this](const std::string& filePath, int entityCount) {
        if (!m_persistence || !m_persistence->documents())
            return;
        auto existing = m_persistence->documents()->loadByPath(filePath);
        QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
        DocumentRecord dr;
        dr.filePath = filePath;
        dr.title = QFileInfo(QString::fromStdString(filePath)).fileName().toStdString();
        dr.format = QFileInfo(QString::fromStdString(filePath)).suffix().toUpper().toStdString();
        dr.entityCount = entityCount;
        dr.lastSavedAt = now.toStdString();
        dr.lastOpenedAt = existing.id > 0 ? existing.lastOpenedAt : now.toStdString();
        dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();
        m_persistence->documents()->save(dr);
        };

    auto doExport = [this, currentFilePath, saveDocumentRecord, recentFiles](
        const std::string& filePath) -> bool {
            if (!m_exportService)
                return false;
            ExportResult result = m_exportService->exportFile(QString::fromStdString(filePath));
            if (!result.success)
            {
                HelpDialogService::showWarning(m_parentWidget, QObject::tr("Save Error"), result.message);
                return false;
            }
            *currentFilePath = filePath;
            saveDocumentRecord(filePath, result.exportedEntityCount);
            if (m_stateCenter)
            {
                m_stateCenter->setDirty(false);
                m_stateCenter->setCurrentDocumentId(QString::fromStdString(filePath));
            }
            if (m_layerPersistence)
                m_layerPersistence->setDocumentId(filePath);
            if (recentFiles)
                recentFiles->addRecentFile(QString::fromStdString(filePath));
            return true;
        };

    auto doOpenFile = [this, currentFilePath, recentFiles](
        const QString& filePath) -> bool {
            if (filePath.isEmpty())
                return false;

            if (!m_importService)
                return false;

            ImportOptions opts;
            opts.importAsNewDocument = true;
            opts.autoFit = true;
            opts.autoSwitchWorkbench = false;

            ImportContext context;
            context.sourcePath = filePath;
            context.recentFileAddCallback = [recentFiles](const QString& path) {
                if (recentFiles)
                    recentFiles->addRecentFile(path);
                };
            context.currentDocumentPathCallback = [currentFilePath](const QString& path) {
                *currentFilePath = path.toStdString();
                };

            ImportResult result = m_importService->importWithContext(context, opts);

            if (!result.success)
            {
                HelpDialogService::showWarning(m_parentWidget, QObject::tr("Import Error"), result.message);
                return false;
            }

            if (m_layerPersistence)
                m_layerPersistence->setDocumentId(filePath.toStdString());

            return true;
        };

    // ---- 新建文件 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_New, [this, scene] {
            bool needsSave = (m_stateCenter && m_stateCenter->dirty());
            if (needsSave)
            {
                auto result = HelpDialogService::showQuestion(
                    m_parentWidget, QObject::tr("Unsaved Changes"),
                    QObject::tr("Do you want to save the current file?"));
                if (result == QMessageBox::Cancel) return;
                if (result == QMessageBox::Yes)
                    m_bus->run(OperationId::File_Save, {});
            }
            scene->clearScene();
            if (m_stateCenter) m_stateCenter->setDirty(false);
        }));

    // ---- 打开文件 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Open, [this, doOpenFile] {
            QString filePath = FileDialogService::getOpenFileName(
                m_parentWidget, QObject::tr("Open File"), FileDialogService::openFileFilter());
            doOpenFile(filePath);
        }));

    // ---- 打开最近文件 ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::File_OpenRecent, [doOpenFile](const QVariantMap& params) {
            doOpenFile(params.value(QStringLiteral("filePath")).toString());
        }));

    // ---- 保存 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Save, [this, scene, currentFilePath, doExport] {
            if (currentFilePath->empty())
            {
                QString filePath = FileDialogService::getSaveFileName(
                    m_parentWidget, QObject::tr("Save"), FileDialogService::saveFileFilter());
                if (filePath.isEmpty()) return;
                doExport(filePath.toStdString());
            }
            else
            {
                doExport(*currentFilePath);
            }
        }));

    // ---- 另存为 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_SaveAs, [this, scene, currentFilePath, doExport] {
            QString filePath = FileDialogService::getSaveFileName(
                m_parentWidget, QObject::tr("Save As"), FileDialogService::saveFileFilter());
            if (!filePath.isEmpty()) doExport(filePath.toStdString());
        }));

    // ---- 导入操作 ----
    const OperationId importOps[] = {
        OperationId::File_ImportDXF, OperationId::File_ImportSVG,
        OperationId::File_ImportPLT, OperationId::File_ImportStep, OperationId::File_ImportPDF,
    };
    for (const auto& opId : importOps)
    {
        reg.registerOperation(std::make_unique<LambdaOperation>(
            opId, [this, opId] {
                try
                {
                    SY_INFOF("[FileOperation] Import triggered: op=%d", static_cast<int>(opId));
                    auto fmt = operationIdToImportFormat(opId);
                    SY_INFOF("[FileOperation] Format determined: format=%d", static_cast<int>(fmt));
                    QString filePath = FileDialogService::getOpenFileName(
                        m_parentWidget, QObject::tr("Import File"), FileDialogService::importFilterForFormat(fmt));
                    SY_INFOF("[FileOperation] File dialog result: filePath=%s", filePath.toUtf8().constData());
                    if (filePath.isEmpty())
                    {
                        SY_INFO("[FileOperation] Import canceled - empty path");
                        return;
                    }
                    if (!m_importService)
                    {
                        SY_ERROR("[FileOperation] ImportService is null");
                        return;
                    }
                    SY_INFOF("[FileOperation] ImportService=%p, SceneManager=%p", m_importService, m_sceneManager);
                    ImportOptions opts;
                    opts.importAsNewDocument = false;
                    opts.autoFit = true;

                    ImportContext context;
                    context.sourcePath = filePath;
                    SY_INFOF("[FileOperation] Calling importWithContext: path=%s", filePath.toUtf8().constData());
                    ImportResult result = m_importService->importWithContext(context, opts);
                    SY_INFOF("[FileOperation] Import completed: success=%d, message=%s",
                        result.success ? 1 : 0, result.message.toUtf8().constData());

                    if (!result.success)
                    {
                        HelpDialogService::showWarning(m_parentWidget, QObject::tr("Import Error"), result.message);
                        return;
                    }
                }
                catch (const std::exception& e)
                {
                    SY_ERRORF("[FileOperation] Import exception: %s", e.what());
                    HelpDialogService::showWarning(m_parentWidget, QObject::tr("Import Error"), 
                        QStringLiteral("Import failed with exception: %1").arg(e.what()));
                }
                catch (...)
                {
                    SY_ERROR("[FileOperation] Import unknown exception");
                    HelpDialogService::showWarning(m_parentWidget, QObject::tr("Import Error"), 
                        QStringLiteral("Import failed with unknown exception"));
                }
            }));
    }

    // ---- 导入图片 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_ImportImage, [this] {
            QString filePath = FileDialogService::getOpenFileName(
                m_parentWidget, QObject::tr("Import Image"), FileDialogService::imageImportFilter());
        }));

    // ---- 导出操作 ----
    const OperationId exportOps[] = {
        OperationId::File_ExportDXF, OperationId::File_ExportSVG,
        OperationId::File_ExportPLT, OperationId::File_ExportBMP, OperationId::File_ExportPNG,
    };

    for (const auto& opId : exportOps)
    {
        reg.registerOperation(std::make_unique<LambdaOperation>(
            opId, [this, opId] {
                auto fmt = operationIdToExportFormat(opId);
                QString filePath = FileDialogService::getSaveFileName(
                    m_parentWidget, QObject::tr("Export File"), FileDialogService::exportFilterForFormat(fmt));

                if (filePath.isEmpty()) return;
                if (!m_exportService) return;

                ExportResult result = m_exportService->exportFile(filePath);
                if (!result.success)
                {
                    HelpDialogService::showWarning(m_parentWidget, QObject::tr("Export Error"), result.message);
                    return;
                }
            }));
    }

    // ---- 退出 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::File_Exit, [this] {
            if (m_parentWidget) m_parentWidget->close();
        }));
}
