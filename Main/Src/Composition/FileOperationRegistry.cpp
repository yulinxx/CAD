/**
 * @file FileOperationRegistry.cpp
 * @brief 文件操作注册实现
 *
 * 注册和管理文件相关的操作命令。
 */
#include "FileOperationRegistry.h"
#include "OpRegistryTypes.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include "UI/FileOperationUtils.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/FileDialogService.h"
#include "UI/Services/IRecentFileService.h"
#include "UI/Services/HelpDialogService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine/Edit/IUndoRedoManager.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine/EntityIdGenerator.h"
#include "Import/ImportService.h"
#include "Import/ImportProgressRunner.h"
#include "Export/ExportService.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/LayerPersistenceBridge.h"
#include "FileIO/FileIOManager.h"
#include "FileIO/ImageUtils.h"
#include "Log/SyLogger.h"

#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QImage>
#include <QVariantMap>
#include <array>
#include <vector>

namespace
{
    // 统一格式映射表：OperationId → FileFormat
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
            Ops::declare(reg, entry.opId, [handler, fmt = entry.format] {
                handler(fmt);
            });
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
    , m_undoRedoManager(config.undoRedoManager)
{
    m_currentFilePath = std::make_shared<std::string>();
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
            QObject::tr("%1 failed: %2").arg(QString::fromLatin1(operationName), e.what()));
    }
    catch (...)
    {
        SY_ERRORF("[FileOperation] %s unknown exception", operationName);
        showFileError(QObject::tr("%1 Error").arg(QString::fromLatin1(operationName)),
            QObject::tr("%1 failed with unknown exception").arg(QString::fromLatin1(operationName)));
    }
}

bool FileOperationRegistry::confirmDiscardCurrentDocument()
{
    // 只问当前工作台的脏标记：另一工作台的未存改动不该在打开/新建时被顺手保存
    // （关闭提示才用 dirty=任一，见 WorkbenchWindow::closeEvent）
    if (!m_stateCenter || !m_stateCenter->activeDocDirty())
    {
        return true;
    }

    const int answer = HelpDialogService::showQuestion(m_parentWidget,
        QObject::tr("Unsaved Changes"),
        QObject::tr("Do you want to save the current file?"));
    if (answer == QMessageBox::Cancel)
    {
        return false;
    }
    if (answer == QMessageBox::No)
    {
        return true;  // 用户明确丢弃
    }

    // 保存必须校验真实结果：取消另存对话框 / 保存失败时绝不能继续清空文档
    const OperationResult saveResult = m_bus->run(OperationId::File_Save, {});
    if (!saveResult.success)
    {
        return false;
    }
    // 保存后复查脏标记（doExport 成功路径会 refreshDirty）：
    // 仍脏说明没真存上，按失败处理，杜绝数据丢失
    return !m_stateCenter->activeDocDirty();
}

// ==================== 公共模板方法 ====================

void FileOperationRegistry::resetDocumentState()
{
    // 当前文件路径：新建之后必须回到「未命名」，否则 File_Save 会直接覆盖上一个文件
    if (m_currentFilePath)
    {
        m_currentFilePath->clear();
    }

    // 撤销栈：跨文档的撤销命令持有的是上一份文档的图元与 id，
    // 在新文档上重放会把旧图元按 id 插回来（渲染层因为没有图层指针而画不出来，
    // 只能靠场景树才看得见），必须在这里清空。
    if (m_undoRedoManager)
    {
        m_undoRedoManager->clear();
    }

    // 引擎层的脏标记：clearScene() 内部会 notifySceneChanged()，那会把场景标脏。
    // 新文档是干净的，这里必须显式清掉，否则「未保存」提示与引擎状态会打架。
    if (m_sceneManager)
    {
        m_sceneManager->clearDirty();
    }

    if (m_stateCenter)
    {
        m_stateCenter->setCurrentDocumentId(QString());
    }
}

bool FileOperationRegistry::doExport(const std::string& filePath)
{
    if (!m_exportService)
    {
        SY_ERROR("[FileOperation] doExport failed: exportService is null");
        return false;
    }

    ExportResult result = m_exportService->exportFile(QString::fromStdString(filePath));
    if (!result.success)
    {
        showFileError(QObject::tr("Save Error"), result.message);
        return false;
    }

    *m_currentFilePath = filePath;
    // 文档记录由 ExportService::postExportRecord 统一写入；
    // 这里不再重复写一次，避免同一行被两条连接同时写造成 database is locked。

    // 存盘成功 → 文档不再脏（引擎层脏标记是事实源，UI 状态是它的镜像）
    if (m_sceneManager)
    {
        m_sceneManager->clearDirty();
    }

    if (m_stateCenter)
    {
        // 引擎脏标记已清，探针重算：2D 变干净 → 标题 "*" 消失；
        // 若另一工作台仍有未存文档，dirty(任一) 保持 true，关闭提示不丢
        m_stateCenter->refreshDirty();
        m_stateCenter->setCurrentDocumentId(QString::fromStdString(filePath));
    }
    if (m_layerPersistence)
    {
        m_layerPersistence->setDocumentId(filePath);
    }
    if (m_recentFiles)
    {
        m_recentFiles->addRecentFile(QString::fromStdString(filePath), QStringLiteral("2D"));
    }

    return true;
}

bool FileOperationRegistry::doOpenFile(const QString& filePath)
{
    if (filePath.isEmpty() || !m_importService)
    {
        SY_WARNF("[FileOperation] doOpenFile failed: filePath=%s importService=%s",
            filePath.isEmpty() ? "(empty)" : filePath.toUtf8().constData(),
            m_importService ? "ok" : "null");
        return false;
    }

    // 打开会替换当前文档（importAsNewDocument=true → clearScene）：
    // 未保存处置不通过就绝不往下走，否则未存更改被静默清掉
    if (!confirmDiscardCurrentDocument())
    {
        SY_DEBUG("[FileOperation] doOpenFile aborted: user cancelled unsaved-changes prompt");
        return false;
    }

    ImportOptions opts;
    opts.importAsNewDocument = true;
    opts.autoFit = true;
    opts.autoSwitchWorkbench = false;

    ImportContext context;
    context.sourcePath = filePath;
    context.recentFileAddCallback = [this](const QString& path, const QString& workbenchId) {
        if (m_recentFiles)
        {
            m_recentFiles->addRecentFile(path, workbenchId);
        }
    };
    context.currentDocumentPathCallback = [this](const QString& path) {
        *m_currentFilePath = path.toStdString();
    };

    // 使用异步导入 + 进度对话框，避免大文件阻塞主界面
    const ImportResult result = ImportProgressRunner::run(m_importService, context, opts, m_parentWidget);
    if (!result.success)
    {
        showFileError(QObject::tr("Import Error"), result.message);
        return false;
    }

    if (m_layerPersistence)
    {
        m_layerPersistence->setDocumentId(filePath.toStdString());
    }

    // 打开即换文档：上一份文档残留在撤销栈里的命令必须清掉。
    // 导入阶段走 CommitMode::Direct 不入栈，此处清空不会丢掉「本次导入」的撤销能力。
    if (m_undoRedoManager)
    {
        m_undoRedoManager->clear();
    }

    // 打开后文档是「已保存」状态：导入过程会 notifySceneChanged 把场景标脏，这里清掉
    if (m_sceneManager)
    {
        m_sceneManager->clearDirty();
    }

    // 引擎脏标记清完之后再写状态中心（顺序不能反，探针读的是引擎脏标记）：
    // 不写 docId，标题文件名会停在上一个文件名（Save/SaveAs 已由 doExport 写入）；
    // 不 refreshDirty，打开干净文件后标题会挂着上一份文档的 "*"
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentDocumentId(filePath);
        m_stateCenter->refreshDirty();
    }

    return true;
}

void FileOperationRegistry::doImportByFormat(Fio::FileFormat fmt)
{
    executeWithExceptionGuard("Import", [this, fmt] {
        QString filePath = FileDialogService::getOpenFileName(
            m_parentWidget, QObject::tr("Import File"), FileDialogService::importFilterForFormat(fmt));
        if (filePath.isEmpty())
        {
            return;
        }

        SY_DEBUGF("[FileOperation] Importing via ImportService: format=%d, path=%s",
            static_cast<int>(fmt),
            filePath.toUtf8().constData());

        doImportFilePath(filePath);
    });
}

void FileOperationRegistry::doImportFilePath(const QString& filePath)
{
    if (filePath.isEmpty() || !m_importService)
    {
        return;
    }

    // 追加语义：导入不替换当前文档（与 doOpenFile 的替换语义相对）
    ImportOptions opts;
    opts.importAsNewDocument = false;
    opts.autoFit = true;

    ImportContext context;
    context.sourcePath = filePath;

    // 使用异步导入 + 进度对话框，避免大文件阻塞主界面
    ImportResult result = ImportProgressRunner::run(m_importService, context, opts, m_parentWidget);
    if (!result.success)
    {
        showFileError(QObject::tr("Import Error"), result.message);
        SY_ERRORF("[FileOperation] Import failed: %s", result.message.toUtf8().constData());
    }
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

        // 直接交出所有权：SyImage::clone() 会把像素缓冲再拷一份，导入大图时是纯浪费
        auto owned = std::unique_ptr<Eg::SyEntity>(imgEntity);

        if (!m_sceneManager)
        {
            SY_ERROR("[FileOperation] Import image failed: sceneManager is null");
            return;
        }

        // 取一个场景里没用过的持久 id：撞号时 insertEntityPreserveId 会静默拒绝，
        // 表现是「导入成功但图上什么也没有」，且下方图层分配会查到同 id 的旧图元。
        Eg::EntityId persistentId = 0;
        for (size_t attempt = 0; attempt < 1000000 && persistentId == 0; ++attempt)
        {
            const Eg::EntityId candidate = Eg::EntityIdGenerator::instance().getNextPersistentId();
            if (!m_sceneManager->findSyEntityById(candidate))
            {
                persistentId = candidate;
            }
        }
        owned->id = persistentId;

        if (persistentId == 0 || !m_sceneManager->insertEntityPreserveId(std::move(owned)))
        {
            SY_ERRORF("[FileOperation] Import image failed: insert rejected (id=%lld path=%s)",
                static_cast<long long>(persistentId),
                path.toUtf8().constData());
            QMessageBox::warning(
                m_parentWidget, QObject::tr("Import Image"), QObject::tr("Failed to add the image to the scene."));
            return;
        }

        // 分配到位图图层
        if (m_layerManager && m_sceneManager)
        {
            Eg::SyEntity* added = m_sceneManager->findSyEntityById(persistentId);
            if (added)
            {
                const int curId = m_layerManager->currentLayerId();
                Eg::LayerType curType = m_layerManager->layerType(curId);
                int bitmapLayerId;
                if (curType == Eg::LayerType::BITMAP)
                {
                    bitmapLayerId = curId;
                }
                else
                {
                    bitmapLayerId = m_layerManager->findOrCreateLayerByType(Eg::LayerType::BITMAP);
                }
                if (bitmapLayerId >= 0)
                {
                    m_layerManager->assignEntityToLayer(added, bitmapLayerId);
                }
            }
        }

        SY_DEBUGF("[FileOperation] Imported image: %s (%dx%d)", path.toUtf8().constData(), rgba.width(), rgba.height());
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

    Ops::declare(reg, OperationId::File_New, [this, scene] {
        // 新建只替换当前工作台的文档：提示/保存/取消全部由 confirmDiscardCurrentDocument 收口。
        // 返回 false（用户取消、保存失败或取消另存、保存后仍脏）时必须中止 ——
        // 原实现跑完 File_Save 就无条件 clearScene，会把没存上的内容直接抹掉
        if (!confirmDiscardCurrentDocument())
        {
            return;
        }
        scene->clearScene();

        // 换文档：清路径 + 清撤销栈（缺一项就会串文档，见 resetDocumentState 注释）
        resetDocumentState();
        if (scene)
        {
            // 新文档即为干净态（与打开/保存一致）：不清的话探针会一直读到上一份文档的脏，
            // 标题 "*" 不消失
            scene->clearDirty();
        }
        if (m_stateCenter)
        {
            m_stateCenter->refreshDirty();
        }
    });
}

void FileOperationRegistry::registerFileOpenOps()
{
    auto& reg = m_bus->registry();

    Ops::declare(reg, OperationId::File_Open, [this] {
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
    });

    Ops::declareParam(reg, OperationId::File_OpenRecent, [this](const QVariantMap& params) {
        doOpenFile(params.value(QStringLiteral("filePath")).toString());
    });
}

void FileOperationRegistry::registerFileSaveOps()
{
    auto& reg = m_bus->registry();

    // 保存类命令必须如实上报结果：closeEvent/新建/打开的后续动作都靠
    // success 与 OpFlagCanceled 判分支。经 void lambda 包装会恒 success=true，
    // 用户取消另存对话框也会被当成"保存成功"继续关窗口（数据丢失）。
    Ops::declareWithResult(reg, OperationId::File_Save, [this]() -> OperationResult {
        OperationResult result;
        QString filePath = QString::fromStdString(*m_currentFilePath);
        const bool canSaveInPlace = !filePath.isEmpty() &&
            filePath.endsWith(QLatin1String(".sy"), Qt::CaseInsensitive);
        if (!canSaveInPlace)
        {
            // 未命名文档、或当前路径不是 .sy（历史遗留的非原生路径）：必须另存为
            filePath = FileDialogService::getSaveFileName(
                m_parentWidget, QObject::tr("Save"), FileDialogService::saveFileFilter());
            if (filePath.isEmpty())
            {
                result.message = QObject::tr("Save was cancelled.");
                result.flags |= Cmd::OpFlagCanceled;
                return result;
            }
        }

        if (doExport(filePath.toStdString()))
        {
            result.success = true;
        }
        else
        {
            // doExport 内部已弹错误对话框，这里只需带上文案给关闭流程展示
            result.message = QObject::tr("Failed to save the document.");
        }
        return result;
    });

    Ops::declareWithResult(reg, OperationId::File_SaveAs, [this]() -> OperationResult {
        OperationResult result;
        const QString filePath = FileDialogService::getSaveFileName(
            m_parentWidget, QObject::tr("Save As"), FileDialogService::saveFileFilter());
        if (filePath.isEmpty())
        {
            result.message = QObject::tr("Save was cancelled.");
            result.flags |= Cmd::OpFlagCanceled;
            return result;
        }
        if (doExport(filePath.toStdString()))
        {
            result.success = true;
        }
        else
        {
            result.message = QObject::tr("Failed to save the document.");
        }
        return result;
    });
}

void FileOperationRegistry::registerImportOps()
{
    auto& reg = m_bus->registry();

    // P5 收口: 统一使用 registerFromFormatMap 批量注册
    registerFromFormatMap(reg, kImportFormatMap, [this](Fio::FileFormat fmt) {
        doImportByFormat(fmt);
    });

    // 导入图片（特殊处理，不走格式映射；支持拖放 filePath 参数或弹文件对话框）
    Ops::declareParam(reg, OperationId::File_ImportImage, [this](const QVariantMap& params) {
        doImportImage(params.value(QStringLiteral("filePath")).toString());
    });

    // “All Supported...”：弹出所有支持格式的对话框，按扩展名自动路由到对应导入器。
    // 与其它 Import 一致为**追加**语义（importAsNewDocument=false）——
    // 原实现走 doOpenFile 会清空当前文档且不弹未保存提示，与注释和菜单语义都不符；
    // 要替换当前文档请用 File ▸ Open（仅 .sy）
    Ops::declare(reg, OperationId::File_ImportAll, [this] {
        QString filePath = FileDialogService::getOpenFileName(
            m_parentWidget, QObject::tr("Import File"), FileDialogService::allSupportedFilter());
        if (filePath.isEmpty())
        {
            return;
        }
        executeWithExceptionGuard("Import", [this, filePath] {
            doImportFilePath(filePath);
        });
    });
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
        SY_ERROR("[FileOperation] registerAll failed: bus or sceneManager is null");
        return;
    }

    // 共享文件路径状态在构造期已创建；这里不再重建，避免重新注册时把当前文档路径丢掉
    if (!m_currentFilePath)
    {
        m_currentFilePath = std::make_shared<std::string>();
    }

    registerFileNewOps();
    registerFileOpenOps();
    registerFileSaveOps();
    registerImportOps();
    registerExportOps();

    // 退出操作
    Ops::declare(m_bus->registry(), OperationId::File_Exit, [this] {
        FileOperationUtils::exitApplication(m_parentWidget);
    });

    // 启动时恢复上次打开的文档：读最近文件列表 → 找第一个仍存在的 → 延迟调 File_OpenRecent
    // 用 QTimer::singleShot(0) 让事件循环先起来，避免 UI 未就绪就加载文件。
    // 只在 2D 工作台的最近列表里找：3D 文件由 3D 工作台自己负责。
    // 先禁用
    if (m_recentFiles && false)
    {
        const QStringList recentFiles = m_recentFiles->loadRecentFiles(QStringLiteral("2D"));
        for (const QString& path : recentFiles)
        {
            if (!path.isEmpty() && QFile::exists(path))
            {
                SY_INFOF("[FileOperation] Auto-restoring last document: %s", qPrintable(path));
                // capture by value 安全：operationBus 比 FileOperationRegistry 长寿（全局单例）
                auto* bus = m_bus;
                QTimer::singleShot(0, [bus, path]() {
                    if (bus)
                    {
                        QVariantMap params;
                        params.insert(QStringLiteral("filePath"), path);
                        bus->run(OperationId::File_OpenRecent, params);
                    }
                });
                break;  // 只恢复第一个存在的
            }
        }
    }
}