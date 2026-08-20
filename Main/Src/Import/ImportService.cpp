#include "ImportService.h"
#include "ImportDispatcher.h"
#include "ImportResult.h"

#include <cstdint>
#include <unordered_map>

#include <QCoreApplication>
#include <QMetaObject>
#include <thread>

#include "Log/SyLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "UI3D/Edit/SceneEditService3D.h"

#include "Color/Color.hpp"

#include <QFileInfo>

// ==================== RAII 导入守卫 ====================
// 替代 goto cleanup 模式：构造时设忙状态，析构时清除忙状态 + 写最终状态
// 解决五阶段流程中 goto cleanup 的代码可读性和维护性问题
namespace
{
    class ScopedImportGuard
    {
    public:
        ScopedImportGuard(ImportService* svc, const QString& sourcePath)
            : m_svc(svc)
            , m_sourcePath(sourcePath)
        {
            if (m_svc)
            {
                // 通知忙状态开始
                const auto& cb = m_svc->busyStateCallback();
                if (cb)
                {
                    cb(true);
                }
            }
        }

        ~ScopedImportGuard()
        {
            if (!m_svc)
            {
                return;
            }

            // 通知忙状态结束
            const auto& busyCb = m_svc->busyStateCallback();
            if (busyCb)
            {
                busyCb(false);
            }

            // 最终状态提示
            const auto& promptCb = m_svc->statusPromptCallback();
            if (promptCb)
            {
                if (m_result.success)
                {
                    promptCb(QStringLiteral("Import completed: %1 entities").arg(m_result.entityCount));
                }
                else
                {
                    promptCb(QStringLiteral("Import failed: %1").arg(m_result.message));
                }
            }
        }

        void setResult(const ImportResult& result)
        {
            m_result = result;
        }

    private:
        ImportService* m_svc;
        QString m_sourcePath;
        ImportResult m_result;
    };
}  // anonymous namespace

// ==================== ImportService 实现 ====================

ImportService::ImportService(QObject* parent)
    : QObject(parent)
{
}

ImportService::~ImportService() = default;

void ImportService::setDispatcher(ImportDispatcher* dispatcher)
{
    m_dispatcher = dispatcher;
}

void ImportService::setSceneManager(Eg::SceneManager* sceneManager)
{
    m_sceneManager = sceneManager;
}

void ImportService::setSceneManager3D(Eg::SceneManager3D* sceneManager3D)
{
    m_sceneManager3D = sceneManager3D;
}

void ImportService::setSceneEditService3D(SceneEditService3D* sceneEditService3D)
{
    m_sceneEditService3D = sceneEditService3D;
}

void ImportService::setEditService(SceneEditService* editService)
{
    m_editService = editService;
}

void ImportService::setLayerManager(LayerManager* layerManager)
{
    m_layerManager = layerManager;
    SY_INFOF("[ImportService] setLayerManager: %p", m_layerManager);
}

void ImportService::setBusyStateCallback(std::function<void(bool)> cb)
{
    m_busyStateCallback = std::move(cb);
}

void ImportService::setStatusPromptCallback(std::function<void(const QString&)> cb)
{
    m_statusPromptCallback = std::move(cb);
}

void ImportService::setViewportFitCallback(std::function<void()> cb)
{
    m_viewportFitCallback = std::move(cb);
}

void ImportService::setTreeRebuildCallback(std::function<void()> cb)
{
    m_treeRebuildCallback = std::move(cb);
}

void ImportService::setPropertyRefreshCallback(std::function<void()> cb)
{
    m_propertyRefreshCallback = std::move(cb);
}

void ImportService::setWorkbenchSwitchCallback(std::function<void(const QString&)> cb)
{
    m_workbenchSwitchCallback = std::move(cb);
}

void ImportService::setStatusBarUpdateCallback(std::function<void(const QString&)> cb)
{
    m_statusBarUpdateCallback = std::move(cb);
}

void ImportService::setRecentFileAddCallback(std::function<void(const QString&)> cb)
{
    m_recentFileAddCallback = std::move(cb);
}

void ImportService::setCurrentDocumentPathCallback(std::function<void(const QString&)> cb)
{
    m_currentDocumentPathCallback = std::move(cb);
}

void ImportService::setDocumentPersistenceCallback(std::function<void(const QString&, int)> cb)
{
    m_documentPersistenceCallback = std::move(cb);
}

ImportResult ImportService::importFile(const QString& filePath, const ImportOptions& options)
{
    ImportContext context;
    context.sourcePath = filePath;
    return importWithContext(context, options);
}

ImportResult ImportService::importWithContext(const ImportContext& context, const ImportOptions& options)
{
    SY_INFOF("[ImportService] importWithContext START: path=%s, newDoc=%d, autoFit=%d",
        context.sourcePath.toUtf8().constData(),
        options.importAsNewDocument ? 1 : 0,
        options.autoFit ? 1 : 0);

    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ImportDispatcher not set");
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::Unknown);
    }

    SY_INFOF("[ImportService] sceneManager=%p, editService=%p", m_sceneManager, m_editService);

    // RAII 守卫：自动管理忙状态和最终状态提示
    ScopedImportGuard guard(this, context.sourcePath);

    // P5 收口: 统一错误/取消处理 lambda，消除 6 处重复的 guard.setResult + emit + return 模式
    auto fail = [&](ImportResult r) {
        guard.setResult(r);
        emit importFinished(r);
        return r;
    };

    if (m_statusPromptCallback)
    {
        m_statusPromptCallback(QStringLiteral("Importing: %1").arg(context.sourcePath));
    }

    emit importStarted(context.sourcePath);

    // 创建可修改的上下文副本（用于注入检测到的格式）
    ImportContext mutableCtx = context;

    // 提前声明，避免 goto 跳过初始化 → RAII 守卫下无需此约束
    Fio::VecSyEntityPtr importedEntities;

    // ===== 1：识别文件格式 =====
    SY_INFO("[ImportService] Phase 1: Detect format");
    ImportResult result = phaseDetectFormat(mutableCtx);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 1 failed: %s", result.message.toUtf8().constData());
        return fail(result);
    }
    SY_INFOF("[ImportService] Phase 1 completed: format=%d", static_cast<int>(mutableCtx.format));

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 2：解析文件 =====
    SY_INFO("[ImportService] Phase 2: Parse file");
    ImportResult parseResult = phaseParse(mutableCtx, importedEntities);
    if (!parseResult.success)
    {
        SY_ERRORF("[ImportService] Phase 2 failed: %s", parseResult.message.toUtf8().constData());
        return fail(parseResult);
    }
    SY_INFOF("[ImportService] Phase 2 completed: entities=%zu, layers=%zu",
        importedEntities.size(),
        parseResult.importedLayers.size());

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 3：构建文档 =====
    SY_INFO("[ImportService] Phase 3: Build document");
    result = phaseBuildDocument(mutableCtx, importedEntities, options, parseResult);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 3 failed: %s", result.message.toUtf8().constData());
        return fail(result);
    }
    SY_INFOF("[ImportService] Phase 3 completed: entityCount=%d", result.entityCount);

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 4：刷新显示 =====
    SY_INFO("[ImportService] Phase 4: Refresh display");
    phaseRefreshDisplay(result, options);
    SY_INFO("[ImportService] Phase 4 completed");

    // ===== 5：回写状态 =====
    SY_INFO("[ImportService] Phase 5: Write back state");
    phaseWriteBackState(mutableCtx, result);
    SY_INFO("[ImportService] Phase 5 completed");

    // RAII 守卫在析构时自动清除忙状态并写最终提示
    guard.setResult(result);

    emit importFinished(result);

    SY_INFOF("[ImportService] importWithContext END: success=%d, message=%s",
        result.success ? 1 : 0,
        result.message.toUtf8().constData());

    return result;
}

void ImportService::importAsync(
    const ImportContext& context, const ImportOptions& options, std::function<void(const ImportResult&)> onComplete)
{
    SY_INFOF("[ImportService] importAsync START: path=%s", context.sourcePath.toUtf8().constData());

    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ImportDispatcher not set");
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        if (onComplete)
        {
            onComplete(ImportResult::fail(msg, ImportErrorType::Unknown));
        }
        return;
    }

    // 获取 shared_ptr 用于跨线程安全传递
    // 使用 std::shared_ptr<ImportService> 需要 enable_shared_from_this
    // 这里使用简单的裸指针 + 标志位，调用方需确保 ImportService 在回调前存活

    auto* self = this;
    auto sourcePath = context.sourcePath;

    // 启动后台线程执行 Phase 1-2
    std::thread([self, context, options, onComplete, sourcePath]() {
        ImportContext mutableCtx = context;
        auto importedEntities = std::make_shared<Fio::VecSyEntityPtr>();

        emit self->importStarted(sourcePath);

        if (self->m_statusPromptCallback)
        {
            self->m_statusPromptCallback(QStringLiteral("Importing: %1").arg(sourcePath));
        }

        // Phase 1: 识别格式
        SY_INFO("[ImportService] Async Phase 1: Detect format");
        ImportResult result = self->phaseDetectFormat(mutableCtx);
        if (!result.success)
        {
            SY_ERRORF("[ImportService] Async Phase 1 failed: %s", result.message.toUtf8().constData());
            if (onComplete)
            {
                onComplete(result);
            }
            return;
        }

        // Phase 2: 解析文件
        SY_INFO("[ImportService] Async Phase 2: Parse file");
        ImportResult parseResult = self->phaseParse(mutableCtx, *importedEntities);
        if (!parseResult.success)
        {
            SY_ERRORF("[ImportService] Async Phase 2 failed: %s", parseResult.message.toUtf8().constData());
            if (onComplete)
            {
                onComplete(parseResult);
            }
            return;
        }

        SY_INFOF("[ImportService] Async parse completed: entities=%zu, layers=%zu",
            importedEntities->size(),
            parseResult.importedLayers.size());

        // Phase 3-5 必须在主线程执行（UI 操作）
        auto entities = importedEntities;
        QMetaObject::invokeMethod(
            qApp,
            [self, mutableCtx, entities, parseResult, options, onComplete, sourcePath]() mutable {
                ImportContext mainCtx = mutableCtx;

                SY_INFO("[ImportService] Async Phase 3: Build document (main thread)");
                ImportResult result = self->phaseBuildDocument(mainCtx, *entities, options, parseResult);
                if (!result.success)
                {
                    SY_ERRORF("[ImportService] Async Phase 3 failed: %s", result.message.toUtf8().constData());
                    if (onComplete)
                    {
                        onComplete(result);
                    }
                    return;
                }

                SY_INFO("[ImportService] Async Phase 4: Refresh display");
                self->phaseRefreshDisplay(result, options);

                SY_INFO("[ImportService] Async Phase 5: Write back state");
                self->phaseWriteBackState(mainCtx, result);

                if (onComplete)
                {
                    onComplete(result);
                }

                emit self->importFinished(result);

                SY_INFOF("[ImportService] importAsync END: success=%d", result.success ? 1 : 0);
            },
            Qt::QueuedConnection);
    }).detach();
}

bool ImportService::canImport(const QString& filePath) const
{
    return m_dispatcher && m_dispatcher->canImport(filePath);
}

QStringList ImportService::supportedExtensions() const
{
    return m_dispatcher ? m_dispatcher->supportedExtensions() : QStringList();
}

// ===== 1：识别文件格式 =====
ImportResult ImportService::phaseDetectFormat(ImportContext& context)
{
    updateProgress(ImportPhase::DetectFormat, 0.0f);
    emit importPhaseChanged(ImportPhase::DetectFormat);

    SY_INFOF("[ImportService] Phase 1: Detecting format for: %s", context.sourcePath.toUtf8().constData());

    // 文件存在性检查
    QFileInfo fi(context.sourcePath);
    if (!fi.exists())
    {
        QString msg = QStringLiteral("File not found: %1").arg(context.sourcePath);
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FileNotFound);
    }

    // 格式检测
    if (context.format == Fio::FileFormat::Unknown)
    {
        context.format = ImportDispatcher::detectFormat(context.sourcePath);
    }

    if (context.format == Fio::FileFormat::Unknown)
    {
        QString msg = QStringLiteral("Unsupported file format: %1").arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 检查是否有对应的读取器
    if (!m_dispatcher->canImport(context.sourcePath))
    {
        QString msg = QStringLiteral("No reader registered for format: %1").arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    SY_INFOF("[ImportService] Format detected: %d", static_cast<int>(context.format));
    updateProgress(ImportPhase::DetectFormat, 1.0f);

    return ImportResult::ok();
}

// ===== 2：解析文件 =====
ImportResult ImportService::phaseParse(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    updateProgress(ImportPhase::Parse, 0.0f);
    emit importPhaseChanged(ImportPhase::Parse);

    SY_INFOF("[ImportService] Phase 2: Parsing file: %s", context.sourcePath.toUtf8().constData());

    // 通过分发器执行解析
    ImportResult result = m_dispatcher->dispatch(context, outEntities);

    if (!result.success)
    {
        // 将通用失败转换为解析失败
        if (result.errorType == ImportErrorType::None)
        {
            result.errorType = ImportErrorType::ParseFailed;
        }
        SY_ERRORF("[ImportService] Parse failed: %s", result.message.toUtf8().constData());
        return result;
    }

    SY_INFOF("[ImportService] Parsed %d entities", static_cast<int>(outEntities.size()));
    updateProgress(ImportPhase::Parse, 1.0f);

    return result;
}

// ===== 3：构建文档 =====
ImportResult ImportService::phaseBuildDocument(const ImportContext& context,
    Fio::VecSyEntityPtr& entities,
    const ImportOptions& options,
    const ImportResult& parseResult)
{
    updateProgress(ImportPhase::BuildDocument, 0.0f);
    emit importPhaseChanged(ImportPhase::BuildDocument);

    SY_INFOF("[ImportService] Phase 3: Building document, entities=%d, newDoc=%d",
        static_cast<int>(entities.size()),
        options.importAsNewDocument ? 1 : 0);

    // 图元合法性过滤：拒绝坏数据进入 SceneManager / RenderWorld
    Fio::VecSyEntityPtr validEntities;
    validEntities.reserve(entities.size());
    for (auto& entity : entities)
    {
        if (entity && entity->isValid())
        {
            validEntities.push_back(std::move(entity));
        }
        else
        {
            SY_WARNF("[ImportService] Skipping invalid entity");
        }
    }

    entities = std::move(validEntities);

    if (entities.empty())
    {
        SY_WARN("[ImportService] All entities were invalid, nothing to import");
        return ImportResult::fail(QStringLiteral("No valid entities to import"), ImportErrorType::ParseFailed);
    }

    int entityCount = static_cast<int>(entities.size());

    // 如果作为新文档导入，先清空场景
    if (options.importAsNewDocument && m_sceneManager)
    {
        SY_INFO("[ImportService] Clearing scene before import");
        m_sceneManager->clearScene();
    }

    // 判断是否为 3D 网格图元
    bool hasMeshEntities = false;
    for (const auto& entity : entities)
    {
        if (entity && entity->eType == Eg::EType::MESH)
        {
            hasMeshEntities = true;
            break;
        }
    }

    // 将导入的图元添加到场景（通过 SceneEditService 确保 Undo/图层分配/ID 冲突处理）
    if (hasMeshEntities && m_sceneManager3D)
    {
        // 3D 网格图元添加到 3D 场景管理器
        SY_INFOF(
            "[ImportService] Adding mesh entities to SceneManager3D: %d entities", static_cast<int>(entities.size()));

        if (options.importAsNewDocument)
        {
            m_sceneManager3D->clearScene();
        }

        // 收集所有网格图元
        std::vector<std::unique_ptr<Eg::SyMeshEntity>> meshEntities;
        std::vector<std::unique_ptr<Eg::SyEntity>> remainingEntities;
        for (auto& entity : entities)
        {
            if (entity && entity->eType == Eg::EType::MESH)
            {
                meshEntities.push_back(
                    std::unique_ptr<Eg::SyMeshEntity>(static_cast<Eg::SyMeshEntity*>(entity.release())));
            }
            else
            {
                remainingEntities.push_back(std::move(entity));
            }
        }
        entities = std::move(remainingEntities);

        // 如果有 SceneEditService3D，通过它添加以支持 Undo
        if (m_sceneEditService3D && !meshEntities.empty())
        {
            SY_INFOF("[ImportService] Using SceneEditService3D for undoable import: %zu entities", meshEntities.size());
            m_sceneEditService3D->addEntities(std::move(meshEntities), "Import 3D mesh");
        }
        else
        {
            // 回退：直接添加到 SceneManager3D（无 Undo 支持）
            for (auto& mesh : meshEntities)
            {
                auto* meshPtr = mesh.release();
                m_sceneManager3D->addEntity(meshPtr);
            }
        }

        m_sceneManager3D->markDataChanged();
        SY_INFO("[ImportService] Mesh entities added to SceneManager3D successfully");
    }
    else if (m_editService)
    {
        SY_INFOF("[ImportService] Calling addEntities: editService=%p, entities=%d",
            m_editService,
            static_cast<int>(entities.size()));
        m_editService->addEntities(std::move(entities), "Import " + context.sourcePath.toStdString());
        SY_INFO("[ImportService] addEntities completed successfully");
    }
    else if (m_sceneManager)
    {
        // 回退：无 SceneEditService 时直写 SceneManager（无 Undo）
        SY_INFOF("[ImportService] Direct adding to sceneManager=%p, entities=%d",
            m_sceneManager,
            static_cast<int>(entities.size()));
        for (auto& entity : entities)
        {
            if (entity)
            {
                m_sceneManager->addEntity(entity.release());
            }
        }
        SY_INFO("[ImportService] Direct add completed");
    }
    else
    {
        SY_ERROR("[ImportService] Neither SceneEditService nor SceneManager available");
        return ImportResult::fail(QStringLiteral("No scene manager available"), ImportErrorType::Unknown);
    }

    // 还原源文件图层结构（DXF 等支持图层的格式）：
    // 图元在 SceneEditService::addEntities 中默认分配到当前图层，这里再按源图层表重新归属
    if (!hasMeshEntities)
    {
        const int createdLayers = restoreImportedLayers(context, parseResult);
        if (createdLayers > 0)
        {
            SY_INFOF("[ImportService] Restored %d layer(s) from source document", createdLayers);
        }
    }

    SY_INFOF("[ImportService] Document built: %d entities", entityCount);
    updateProgress(ImportPhase::BuildDocument, 1.0f);

    // 根据图元类型设置目标工作台 ID
    auto result = ImportResult::ok(QStringLiteral("Imported %1 entities successfully").arg(entityCount), entityCount);
    result.usedWorkbenchId = hasMeshEntities ? QStringLiteral("3D") : QStringLiteral("2D");
    return result;
}

// ===== 4：刷新显示 =====
void ImportService::phaseRefreshDisplay(const ImportResult& result, const ImportOptions& options)
{
    updateProgress(ImportPhase::RefreshDisplay, 0.0f);
    emit importPhaseChanged(ImportPhase::RefreshDisplay);

    SY_INFO("[ImportService] Phase 4: Refreshing display");

    // 工作台切换
    if (options.autoSwitchWorkbench && m_workbenchSwitchCallback)
    {
        QString targetId = result.usedWorkbenchId;
        if (targetId.isEmpty())
        {
            targetId = QStringLiteral("2D");
        }
        m_workbenchSwitchCallback(targetId);
    }

    // 视口适配
    if (options.autoFit && m_viewportFitCallback)
    {
        m_viewportFitCallback();
    }

    // 树结构刷新
    if (m_treeRebuildCallback)
    {
        m_treeRebuildCallback();
    }

    // 属性面板刷新
    if (m_propertyRefreshCallback)
    {
        m_propertyRefreshCallback();
    }

    SY_INFO("[ImportService] Display refreshed");
    updateProgress(ImportPhase::RefreshDisplay, 1.0f);
}

// ===== 5：回写状态 =====
void ImportService::phaseWriteBackState(const ImportContext& context, const ImportResult& result)
{
    updateProgress(ImportPhase::WriteBackState, 0.0f);
    emit importPhaseChanged(ImportPhase::WriteBackState);

    SY_INFO("[ImportService] Phase 5: Writing back state");

    // 更新当前文档路径（优先使用 context 中的回调）
    if (context.currentDocumentPathCallback)
    {
        context.currentDocumentPathCallback(context.sourcePath);
    }
    else if (m_currentDocumentPathCallback)
    {
        m_currentDocumentPathCallback(context.sourcePath);
    }

    // 添加到最近文件（优先使用 context 中的回调）
    if (context.recentFileAddCallback)
    {
        context.recentFileAddCallback(context.sourcePath);
    }
    else if (m_recentFileAddCallback)
    {
        m_recentFileAddCallback(context.sourcePath);
    }

    // 文档持久化（使用成员变量回调，全局配置）
    if (m_documentPersistenceCallback)
    {
        m_documentPersistenceCallback(context.sourcePath, result.entityCount);
    }

    // 更新状态栏（使用成员变量回调，全局配置）
    if (m_statusBarUpdateCallback)
    {
        QString statusMsg = QStringLiteral("Imported %1 entities from %2")
                                .arg(result.entityCount)
                                .arg(QFileInfo(context.sourcePath).fileName());
        m_statusBarUpdateCallback(statusMsg);
    }

    SY_INFO("[ImportService] State written back");
    updateProgress(ImportPhase::WriteBackState, 1.0f);
}

int ImportService::restoreImportedLayers(const ImportContext& context, const ImportResult& parseResult)
{
    if (!m_layerManager || parseResult.importedLayers.empty())
    {
        return 0;
    }
    if (!context.preserveLayers)
    {
        SY_INFO("[ImportService] restoreImportedLayers: preserveLayers=false, skipping layer restore");
        return 0;
    }

    // 源图层 sourceId → 运行时 LayerManager 图层 ID
    std::unordered_map<uint32_t, int> sourceToLayerId;
    int createdCount = 0;

    for (const auto& src : parseResult.importedLayers)
    {
        const uint8_t r = static_cast<uint8_t>((src.color >> 16) & 0xFF);
        const uint8_t g = static_cast<uint8_t>((src.color >> 8) & 0xFF);
        const uint8_t b = static_cast<uint8_t>(src.color & 0xFF);
        const Ut::Color color = Ut::Color::fromRGB255(r, g, b);

        // 1) 优先按名称复用已有图层（同名不同色的源图层也共用同一运行时图层，避免反复导入时图层无限累积）
        int layerId = m_layerManager->findLayerByName(src.name);
        if (layerId < 0)
        {
            // 2) 名称不匹配时按颜色去重：同一颜色的源图层共用同一运行时图层
            layerId = m_layerManager->findLayerByColor(color);
        }
        if (layerId < 0)
        {
            // 3) 名称与颜色都不匹配才新建图层，并应用源图层的可见性/锁定属性
            layerId = m_layerManager->createLayer(src.name);
            if (layerId >= 0)
            {
                ++createdCount;
                m_layerManager->setLayerColor(layerId, color);
                m_layerManager->setLayerVisible(layerId, src.visible);
                m_layerManager->setLayerLocked(layerId, src.locked);
            }
            else
            {
                // 图层数量已达上限：回退到默认图层，避免无限累积导致异常
                SY_WARNF("[ImportService] Layer count limit reached, falling back to default layer for '%s'", src.name);
                layerId = m_layerManager->findLayerByName("Layer 0");
                if (layerId < 0)
                {
                    layerId = 0;
                }
            }
        }
        sourceToLayerId[src.sourceId] = layerId;
    }

    // 将图元归属到对应图层：EntityId → 源图层 sourceId → 运行时图层 ID
    std::unordered_map<int64_t, int> entityToLayerId;
    entityToLayerId.reserve(parseResult.entityLayerMap.size());
    for (const auto& [entityId, sourceId] : parseResult.entityLayerMap)
    {
        auto it = sourceToLayerId.find(sourceId);
        if (it != sourceToLayerId.end())
        {
            entityToLayerId[entityId] = it->second;
        }
        else
        {
            SY_WARNF("[ImportService] Entity %lld references unknown source layer %u, keeping current layer",
                static_cast<long long>(entityId),
                sourceId);
        }
    }

    if (!entityToLayerId.empty())
    {
        m_layerManager->applyEntityLayerMap(entityToLayerId);
        SY_INFOF("[ImportService] Assigned %zu entities to restored layers", entityToLayerId.size());
    }

    SY_INFOF("[ImportService] Restored %d/%zu source layer(s)", createdCount, parseResult.importedLayers.size());
    return createdCount;
}

void ImportService::updateProgress(ImportPhase phase, float progress)
{
    emit importProgress(progress);
}

bool ImportService::isCanceled(const ImportContext& context) const
{
    if (context.cancelCallback)
    {
        return context.cancelCallback();
    }

    return false;
}