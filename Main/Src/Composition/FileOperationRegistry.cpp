#include "FileOperationRegistry.h"
#include "DocumentPersistenceHelper.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "UI/FileOperationUtils.h"
#include "UI/Services/UiStateCenter.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine/EntityIdGenerator.h"
#include "UI/Services/FileDialogService.h"
#include "UI/Services/RecentFileService.h"
#include "UI/Services/HelpDialogService.h"
#include "Import/ImportService.h"
#include "Export/ExportService.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/LayerPersistenceBridge.h"
#include "FileIO/FileIOManager.h"
#include "FileIO/ImageUtils.h"
#include "Log/SyLogger.h"

#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QImage>
#include <QVariantMap>
#include <array>
#include <vector>

namespace
{
    // 统一格式映射表：OperationId → FileFormat，替代旧的 switch 分支
    struct FormatMappingEntry
    {
        OperationId opId;
        Fio::FileFormat format;
    };

    constexpr std::array<FormatMappingEntry, 7> kImportFormatMap = { {
        { OperationId::File_ImportDXF, Fio::FileFormat::DXF },
        { OperationId::File_ImportSVG, Fio::FileFormat::SVG },
        { OperationId::File_ImportPLT, Fio::FileFormat::PLT },
        { OperationId::File_ImportStep, Fio::FileFormat::STEP },
        { OperationId::File_ImportPDF, Fio::FileFormat::PDF },
        { OperationId::File_ImportAI, Fio::FileFormat::AI },
        { OperationId::File_ImportUG, Fio::FileFormat::UG },
    } };

    constexpr std::array<FormatMappingEntry, 5> kExportFormatMap = { {
        { OperationId::File_ExportDXF, Fio::FileFormat::DXF },
        { OperationId::File_ExportSVG, Fio::FileFormat::SVG },
        { OperationId::File_ExportPLT, Fio::FileFormat::PLT },
        { OperationId::File_ExportBMP, Fio::FileFormat::BMP },
        { OperationId::File_ExportPNG, Fio::FileFormat::PNG },
    } };

    // P5 收口: 统一按格式映射批量注册操作，消除 registerImportOps/registerExportOps 的重复循环
    // 模板化以兼容不同数量的格式映射（导入 7 种 / 导出 5 种）
    template<std::size_t N>
    void registerFromFormatMap(OperationRegistry& reg,
        const std::array<FormatMappingEntry, N>& map,
        std::function<void(Fio::FileFormat)> handler)
    {
        for (const auto& entry : map)
        {
            reg.registerOperation(std::make_unique<LambdaOperation>(entry.opId, [handler, fmt = entry.format] {
                handler(fmt);
            }));
        }
    }
}  // namespace

FileOperationRegistry::FileOperationRegistry(const FileOperationConfig& config)
    : m_bus(config.bus)
    , m_sceneManager(config.sceneManager)
    , m_layerManager(config.layerManager)
    , m_importService(config.importService)
    , m_exportService(config.exportService)
    , m_recentFiles(config.recentFiles)
    , m_helpDialog(config.helpDialog)
    , m_stateCenter(config.stateCenter)
    , m_layerPersistence(config.layerPersistence)
    , m_persistence(config.persistence)
    , m_parentWidget(config.parentWidget)
{
}

// ==================== 公共辅助方法 ====================

void FileOperationRegistry::showFileError(const QString& title, const QString& message)
{
    HelpDialogService::showWarning(m_parentWidget, title, message);
}

void FileOperationRegistry::executeWithExceptionGuard(const char* operationName, std::function<void()> action)
{
    try
    {
        action();
    }
    catch (const std::exception& e)
    {
        SY_ERRORF("[FileOperation] %s exception: %s", operationName, e.what());
        showFileError(QObject::tr("%1 Error").arg(QString::fromLatin1(operationName)),
            QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(operationName), e.what()));
    }
    catch (...)
    {
        SY_ERRORF("[FileOperation] %s unknown exception", operationName);
        showFileError(QObject::tr("%1 Error").arg(QString::fromLatin1(operationName)),
            QStringLiteral("%1 failed with unknown exception").arg(QString::fromLatin1(operationName)));
    }
}

// ==================== 公共模板方法 ====================

void FileOperationRegistry::saveDocumentRecord(const std::string& filePath, int entityCount)
{
    DocumentPersistenceHelper::recordExport(m_persistence, filePath, entityCount);
}

bool FileOperationRegistry::doExport(const std::string& filePath)
{
    if (!m_exportService)
    {
        return false;
    }

    ExportResult result = m_exportService->exportFile(QString::fromStdString(filePath));
    if (!result.success)
    {
        showFileError(QObject::tr("Save Error"), result.message);
        return false;
    }

    *m_currentFilePath = filePath;
    saveDocumentRecord(filePath, result.exportedEntityCount);

    if (m_stateCenter)
    {
        m_stateCenter->setDirty(false);
        m_stateCenter->setCurrentDocumentId(QString::fromStdString(filePath));
    }
    if (m_layerPersistence)
    {
        m_layerPersistence->setDocumentId(filePath);
    }
    if (m_recentFiles)
    {
        m_recentFiles->addRecentFile(QString::fromStdString(filePath));
    }

    return true;
}

bool FileOperationRegistry::doOpenFile(const QString& filePath)
{
    if (filePath.isEmpty() || !m_importService)
    {
        return false;
    }

    ImportOptions opts;
    opts.importAsNewDocument = true;
    opts.autoFit = true;
    opts.autoSwitchWorkbench = false;

    ImportContext context;
    context.sourcePath = filePath;
    context.recentFileAddCallback = [this](const QString& path) {
        if (m_recentFiles)
        {
            m_recentFiles->addRecentFile(path);
        }
    };
    context.currentDocumentPathCallback = [this](const QString& path) {
        *m_currentFilePath = path.toStdString();
    };

    ImportResult result = m_importService->importWithContext(context, opts);
    if (!result.success)
    {
        showFileError(QObject::tr("Import Error"), result.message);
        return false;
    }

    if (m_layerPersistence)
    {
        m_layerPersistence->setDocumentId(filePath.toStdString());
    }

    return true;
}

void FileOperationRegistry::doImportByFormat(Fio::FileFormat fmt)
{
    executeWithExceptionGuard("Import", [this, fmt] {
        QString filePath = FileDialogService::getOpenFileName(
            m_parentWidget, QObject::tr("Import File"), FileDialogService::importFilterForFormat(fmt));
        if (filePath.isEmpty() || !m_importService)
        {
            return;
        }

        SY_INFOF("[FileOperation] Importing via ImportService: format=%d, path=%s",
            static_cast<int>(fmt),
            filePath.toUtf8().constData());

        ImportOptions opts;
        opts.importAsNewDocument = false;
        opts.autoFit = true;

        ImportContext context;
        context.sourcePath = filePath;

        ImportResult result = m_importService->importWithContext(context, opts);
        if (!result.success)
        {
            showFileError(QObject::tr("Import Error"), result.message);
            SY_ERRORF("[FileOperation] Import failed: %s", result.message.toUtf8().constData());
        }
    });
}

void FileOperationRegistry::doImportImage(const QString& filePath)
{
    executeWithExceptionGuard("ImportImage", [this, filePath] {
        QString path = filePath;
        if (path.isEmpty())
        {
            path = FileDialogService::getOpenFileName(
                m_parentWidget, QObject::tr("Import Image"), FileDialogService::imageImportFilter());
            if (path.isEmpty())
            {
                return;
            }
        }

        QImage rgba;
        QImage image(path);
        if (!image.isNull())
        {
            rgba = image.convertToFormat(QImage::Format_RGBA8888);
        }
        else
        {
            // QImage 无法解码（如缺少 Qt imageformats 插件）时回退：
            // libwebp 解 webp、libtiff 解 tiff、stb_image 解 tga 等，保证菜单导入支持全部声明格式。
            std::vector<unsigned char> bytes;
            int w = 0, h = 0;
            if (!Fio::loadImageToRgba(path.toUtf8().constData(), bytes, w, h) || w <= 0 || h <= 0)
            {
                QMessageBox::warning(
                    m_parentWidget, QObject::tr("Import Image"), QObject::tr("Failed to load image: %1").arg(path));
                return;
            }
            rgba = QImage(bytes.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
        }

        Fio::ImageInfo info = Fio::readImageInfo(path.toUtf8().constData());
        const float worldW = Fio::pixelsToUnit(rgba.width(), info.dpiX, Fio::UnitType::Millimeter);
        const float worldH = Fio::pixelsToUnit(rgba.height(), info.dpiY, Fio::UnitType::Millimeter);

        auto* imgEntity = new Eg::SyImage();
        imgEntity->nWidth = rgba.width();
        imgEntity->nHeight = rgba.height();
        imgEntity->ePixelFormat = Eg::SyPixelFormat::RGBA32;
        imgEntity->setPixelData(rgba.constBits(), static_cast<size_t>(rgba.sizeInBytes()));

        const double halfW = worldW / 2.0;
        const double halfH = worldH / 2.0;
        imgEntity->basePoint = Ut::Vec2d(0, 0);
        imgEntity->topLeft = Ut::Vec2d(-halfW, halfH);
        imgEntity->topRight = Ut::Vec2d(halfW, halfH);
        imgEntity->bottomLeft = Ut::Vec2d(-halfW, -halfH);
        imgEntity->bottomRight = Ut::Vec2d(halfW, -halfH);

        // 显式获取持久 ID，避免 insertEntityPreserveId 替换临时 ID 导致查找失败
        const Eg::EntityId persistentId = Eg::EntityIdGenerator::instance().getNextPersistentId();
        auto snap = std::unique_ptr<Eg::SyEntity>(imgEntity->clone());
        snap->id = persistentId;
        m_sceneManager->insertEntityPreserveId(std::move(snap));

        // 分配到位图图层
        if (m_layerManager && m_sceneManager)
        {
            Eg::SyEntity* added = m_sceneManager->findSyEntityById(persistentId);
            if (added)
            {
                int bitmapLayerId = m_layerManager->findOrCreateLayerByType(Eg::LayerType::BITMAP);
                if (bitmapLayerId >= 0)
                {
                    m_layerManager->assignEntityToLayer(added, bitmapLayerId);
                    m_layerManager->setCurrentLayer(bitmapLayerId);
                }
            }
        }

        delete imgEntity;

        SY_INFOF("[FileOperation] Imported image: %s (%dx%d)", path.toUtf8().constData(), rgba.width(), rgba.height());
    });
}

void FileOperationRegistry::doExportByFormat(Fio::FileFormat fmt)
{
    executeWithExceptionGuard("Export", [this, fmt] {
        QString filePath = FileDialogService::getSaveFileName(
            m_parentWidget, QObject::tr("Export File"), FileDialogService::exportFilterForFormat(fmt));
        if (filePath.isEmpty() || !m_exportService)
        {
            return;
        }

        ExportResult result = m_exportService->exportFile(filePath);
        if (!result.success)
        {
            showFileError(QObject::tr("Export Error"), result.message);
            SY_ERRORF("[FileOperation] Export failed: %s", result.message.toUtf8().constData());
        }
    });
}

// ==================== 操作注册子方法 ====================

void FileOperationRegistry::registerFileNewOps()
{
    auto& reg = m_bus->registry();
    auto* scene = m_sceneManager;

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::File_New, [this, scene] {
        bool needsSave = (m_stateCenter && m_stateCenter->dirty());
        if (needsSave)
        {
            auto result = HelpDialogService::showQuestion(
                m_parentWidget, QObject::tr("Unsaved Changes"), QObject::tr("Do you want to save the current file?"));
            if (result == QMessageBox::Cancel)
            {
                return;
            }
            if (result == QMessageBox::Yes)
            {
                m_bus->run(OperationId::File_Save, {});
            }
        }
        scene->clearScene();
        if (m_stateCenter)
        {
            m_stateCenter->setDirty(false);
        }
    }));
}

void FileOperationRegistry::registerFileOpenOps()
{
    auto& reg = m_bus->registry();

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::File_Open, [this] {
        QString filePath = FileDialogService::getOpenFileName(
            m_parentWidget, QObject::tr("Open File"), FileDialogService::openFileFilter());
        if (filePath.isEmpty())
        {
            return;
        }
        // 2D 的“打开”仅允许自定义 2D 文档（.sy），其它格式走“导入”
        if (!filePath.toLower().endsWith(QStringLiteral(".sy")))
        {
            QMessageBox::warning(m_parentWidget,
                QObject::tr("Open Error"),
                QObject::tr("Only SanYi 2D files (*.sy) can be opened here.\n"
                            "Use File ▸ Import for other formats."));
            return;
        }
        doOpenFile(filePath);
    }));

    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::File_OpenRecent, [this](const QVariantMap& params) {
            doOpenFile(params.value(QStringLiteral("filePath")).toString());
        }));
}

void FileOperationRegistry::registerFileSaveOps()
{
    auto& reg = m_bus->registry();

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::File_Save, [this] {
        if (m_currentFilePath->empty())
        {
            QString filePath = FileDialogService::getSaveFileName(
                m_parentWidget, QObject::tr("Save"), FileDialogService::saveFileFilter());
            if (filePath.isEmpty())
            {
                return;
            }
            doExport(filePath.toStdString());
        }
        else
        {
            doExport(*m_currentFilePath);
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::File_SaveAs, [this] {
        QString filePath = FileDialogService::getSaveFileName(
            m_parentWidget, QObject::tr("Save As"), FileDialogService::saveFileFilter());
        if (!filePath.isEmpty())
        {
            doExport(filePath.toStdString());
        }
    }));
}

void FileOperationRegistry::registerImportOps()
{
    auto& reg = m_bus->registry();

    // P5 收口: 统一使用 registerFromFormatMap 批量注册
    registerFromFormatMap(reg, kImportFormatMap, [this](Fio::FileFormat fmt) {
        doImportByFormat(fmt);
    });

    // 导入图片（特殊处理，不走格式映射；支持拖放 filePath 参数或弹文件对话框）
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::File_ImportImage, [this](const QVariantMap& params) {
            doImportImage(params.value(QStringLiteral("filePath")).toString());
        }));
}

void FileOperationRegistry::registerExportOps()
{
    auto& reg = m_bus->registry();

    // P5 收口: 统一使用 registerFromFormatMap 批量注册
    registerFromFormatMap(reg, kExportFormatMap, [this](Fio::FileFormat fmt) {
        doExportByFormat(fmt);
    });
}

void FileOperationRegistry::registerAll()
{
    if (!m_bus || !m_sceneManager)
    {
        return;
    }

    // 初始化共享文件路径状态
    m_currentFilePath = std::make_shared<std::string>();

    registerFileNewOps();
    registerFileOpenOps();
    registerFileSaveOps();
    registerImportOps();
    registerExportOps();

    // 退出操作
    m_bus->registry().registerOperation(std::make_unique<LambdaOperation>(OperationId::File_Exit, [this] {
        FileOperationUtils::exitApplication(m_parentWidget);
    }));
}