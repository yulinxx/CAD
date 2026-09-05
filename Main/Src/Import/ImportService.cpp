#include "ImportService.h"
#include "ImportDispatcher.h"
#include "ImportResult.h"

#include <cstdint>
#include <unordered_map>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
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
    SY_DEBUGF("[ImportService] setLayerManager: %p", m_layerManager);
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
    // Import entry point

    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ImportDispatcher not set");
        SY_ERRORF("[ImportService] importWithContext aborted: %s", msg.toUtf8().constData());
        const ImportResult r = ImportResult::fail(msg, ImportErrorType::Unknown);
        emit importFinished(r);
        return r;
    }

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
    ImportResult result = phaseDetectFormat(mutableCtx);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 1 failed: %s", result.message.toUtf8().constData());
        return fail(result);
    }

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 2：解析文件 =====
    ImportResult parseResult = phaseParse(mutableCtx, importedEntities);
    if (!parseResult.success)
    {
        SY_ERRORF("[ImportService] Phase 2 failed: %s", parseResult.message.toUtf8().constData());
        return fail(parseResult);
    }

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 3：构建文档 =====
    result = phaseBuildDocument(mutableCtx, importedEntities, options, parseResult);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 3 failed: %s", result.message.toUtf8().constData());
        return fail(result);
    }

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 4：刷新显示 =====
    phaseRefreshDisplay(result, options);

    // ===== 5：回写状态 =====
    phaseWriteBackState(mutableCtx, result);

    // RAII 守卫在析构时自动清除忙状态并写最终提示
    guard.setResult(result);

    emit importFinished(result);

    return result;
}

void ImportService::importAsync(
    const ImportContext& context, const ImportOptions& options, std::function<void(const ImportResult&)> onComplete)
{
    SY_DEBUGF("[ImportService] importAsync START: path=%s", context.sourcePath.toUtf8().constData());

    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ImportDispatcher not set");
        SY_ERRORF("[ImportService] importAsync aborted: %s", msg.toUtf8().constData());
        if (onComplete)
        {
            onComplete(ImportResult::fail(msg, ImportErrorType::Unknown));
        }
        return;
    }

    // 忙状态与开始通知都在调用线程（主线程）发出：
    // 这些回调最终会动 UI，放到后台线程里发是跨线程访问
    if (m_busyStateCallback)
    {
        m_busyStateCallback(true);
    }
    if (m_statusPromptCallback)
    {
        m_statusPromptCallback(QStringLiteral("Importing: %1").arg(context.sourcePath));
    }
    emit importStarted(context.sourcePath);

    // 使用 QPointer 安全持有 this：若 ImportService 被销毁，
    // QPointer 自动置空，后续所有路径安全返回，避免悬空指针访问
    QPointer<ImportService> safeSelf = this;

    // 统一收尾：清忙状态 + 最终提示 + 完成信号 + 回调，保证每条退出路径都恰好走一次，
    // 且始终在主线程执行（后台线程失败时也会经此回到主线程）
    auto finishOnMainThread = [safeSelf, onComplete](const ImportResult& r) {
        QMetaObject::invokeMethod(
            qApp,
            [safeSelf, onComplete, r]() {
                if (safeSelf.isNull()) return;
                if (safeSelf->m_busyStateCallback)
                {
                    safeSelf->m_busyStateCallback(false);
                }
                if (safeSelf->m_statusPromptCallback)
                {
                    safeSelf->m_statusPromptCallback(r.success
                            ? QStringLiteral("Import completed: %1 entities").arg(r.entityCount)
                            : QStringLiteral("Import failed: %1").arg(r.message));
                }
                emit safeSelf->importFinished(r);
                if (onComplete)
                {
                    onComplete(r);
                }
                SY_DEBUGF("[ImportService] importAsync END: success=%d, entities=%d, message=%s",
                    r.success ? 1 : 0,
                    r.entityCount,
                    r.message.toUtf8().constData());
            },
            Qt::QueuedConnection);
    };

    // 启动后台线程执行 Phase 1-2（纯解析，不碰 UI 与场景）
    std::thread([safeSelf, context, options, finishOnMainThread]() {
        if (safeSelf.isNull()) return;
        ImportContext mutableCtx = context;
        auto importedEntities = std::make_shared<Fio::VecSyEntityPtr>();

        // ===== Async Phase 1: Detect format =====
        ImportResult result = safeSelf->phaseDetectFormat(mutableCtx);
        if (!result.success)
        {
            SY_ERRORF("[ImportService] Async Phase 1 failed: %s", result.message.toUtf8().constData());
            finishOnMainThread(result);
            return;
        }

        if (safeSelf->isCanceled(mutableCtx))
        {
            SY_WARN("[ImportService] Async import canceled after Phase 1");
            finishOnMainThread(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
            return;
        }

        // ===== Async Phase 2: Parse file =====
        ImportResult parseResult = safeSelf->phaseParse(mutableCtx, *importedEntities);
        if (!parseResult.success)
        {
            SY_ERRORF("[ImportService] Async Phase 2 failed: %s", parseResult.message.toUtf8().constData());
            finishOnMainThread(parseResult);
            return;
        }

        if (safeSelf->isCanceled(mutableCtx))
        {
            SY_WARN("[ImportService] Async import canceled after Phase 2");
            finishOnMainThread(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
            return;
        }

        // Phase 3-5 必须回主线程：要动场景、视口与状态栏
        auto entities = importedEntities;
        QMetaObject::invokeMethod(
            qApp,
            [safeSelf, mutableCtx, entities, parseResult, options, finishOnMainThread]() mutable {
                if (safeSelf.isNull()) return;
                ImportContext mainCtx = mutableCtx;

                if (safeSelf->isCanceled(mainCtx))
                {
                    SY_WARN("[ImportService] Async import canceled before Phase 3");
                    finishOnMainThread(
                        ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));
                    return;
                }

                // Build document on main thread
                ImportResult result = safeSelf->phaseBuildDocument(mainCtx, *entities, options, parseResult);
                if (!result.success)
                {
                    SY_ERRORF("[ImportService] Async Phase 3 failed: %s", result.message.toUtf8().constData());
                    finishOnMainThread(result);
                    return;
                }

                safeSelf->phaseRefreshDisplay(result, options);
                safeSelf->phaseWriteBackState(mainCtx, result);

                finishOnMainThread(result);
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

    // Detect format phase

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

    updateProgress(ImportPhase::DetectFormat, 1.0f);

    return ImportResult::ok();
}

// ===== Parse file =====
ImportResult ImportService::phaseParse(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    updateProgress(ImportPhase::Parse, 0.0f);
    emit importPhaseChanged(ImportPhase::Parse);

    // Parse file phase

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

    SY_DEBUGF("[ImportService] Parsed %d entities", static_cast<int>(outEntities.size()));
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

    // Build document phase

    // 图元合法性过滤：拒绝坏数据进入 SceneManager / RenderWorld
    Fio::VecSyEntityPtr validEntities;
    validEntities.reserve(entities.size());
    int nullCount = 0;
    int invalidCount = 0;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        auto& entity = entities[i];
        if (!entity)
        {
            ++nullCount;
            continue;
        }
        if (!entity->isValid())
        {
            ++invalidCount;
            continue;
        }
        validEntities.push_back(std::move(entity));
    }

    entities = std::move(validEntities);

    if (nullCount > 0 || invalidCount > 0)
    {
        SY_WARNF("[ImportService] Entity validation dropped %d null + %d invalid, %zu remaining",
            nullCount,
            invalidCount,
            entities.size());
    }

    if (entities.empty())
    {
        SY_ERRORF("[ImportService] All %d parsed entity(ies) were rejected by validation, nothing to import",
            nullCount + invalidCount);
        return ImportResult::fail(QStringLiteral("No valid entities to import"), ImportErrorType::ParseFailed);
    }


    // 如果作为新文档导入，先清空场景
    if (options.importAsNewDocument && m_sceneManager)
    {
        SY_DEBUG("[ImportService] Clearing 2D scene before import");
        m_sceneManager->clearScene();
    }

    // ===== 3.1 按图元类型分拣：网格进 3D 场景，其余进 2D 场景 =====
    // 混合文件（例如同一个 OBJ 里既有网格又有线框标注）两侧都必须落地。
    std::vector<std::unique_ptr<Eg::SyMeshEntity>> meshEntities;
    Fio::VecSyEntityPtr flatEntities;
    for (auto& entity : entities)
    {
        if (!entity)
        {
            continue;
        }
        if (entity->eType == Eg::EType::MESH)
        {
            meshEntities.push_back(std::unique_ptr<Eg::SyMeshEntity>(static_cast<Eg::SyMeshEntity*>(entity.release())));
        }
        else
        {
            flatEntities.push_back(std::move(entity));
        }
    }
    entities.clear();

    // 没有 3D 场景可落地时，网格退回 2D 通道（沿用既有行为），但必须留痕便于排查
    if (!meshEntities.empty() && !m_sceneManager3D)
    {
        SY_WARNF("[ImportService] SceneManager3D not set, %zu mesh entity(ies) fall back to the 2D scene",
            meshEntities.size());
        for (auto& mesh : meshEntities)
        {
            flatEntities.push_back(std::unique_ptr<Eg::SyEntity>(mesh.release()));
        }
        meshEntities.clear();
    }

    const bool hasMeshEntities = !meshEntities.empty();
    SY_DEBUGF("[ImportService] Entity split: %lld mesh -> 3D scene, %lld flat -> 2D scene",
        static_cast<long long>(meshEntities.size()),
        static_cast<long long>(flatEntities.size()));

    // ===== 3.2 网格图元落地到 3D 场景 =====
    int meshAdded = 0;
    if (hasMeshEntities)
    {
        if (options.importAsNewDocument)
        {
            SY_DEBUG("[ImportService] Clearing 3D scene before import");
            m_sceneManager3D->clearScene();
        }

        meshAdded = static_cast<int>(meshEntities.size());
        if (m_sceneEditService3D)
        {
            SY_DEBUGF("[ImportService] Adding %d mesh entity(ies) via SceneEditService3D (undoable)", meshAdded);
            m_sceneEditService3D->addEntities(std::move(meshEntities), "Import 3D mesh");
        }
        else
        {
            // 回退：直写 SceneManager3D，无 Undo 支持
            SY_DEBUGF("[ImportService] SceneEditService3D not set, adding %d mesh entity(ies) without undo support",
                meshAdded);
            for (auto& mesh : meshEntities)
            {
                m_sceneManager3D->addEntity(mesh.release());
            }
        }

        m_sceneManager3D->markDataChanged();
        SY_DEBUGF("[ImportService] %d mesh entity(ies) added to SceneManager3D", meshAdded);
    }

    // ===== 3.3 其余图元落地到 2D 场景 =====
    int flatAdded = 0;
    if (!flatEntities.empty())
    {
        flatAdded = static_cast<int>(flatEntities.size());
        if (m_editService)
        {
            SY_DEBUGF("[ImportService] Adding %d entity(ies) via SceneEditService (undoable)", flatAdded);
            m_editService->addEntities(std::move(flatEntities), "Import " + context.sourcePath.toStdString());
        }
        else if (m_sceneManager)
        {
            // 回退：无 SceneEditService 时直写 SceneManager，无 Undo 支持
            SY_WARNF("[ImportService] SceneEditService not set, adding %d entity(ies) without undo support", flatAdded);
            for (auto& entity : flatEntities)
            {
                if (entity)
                {
                    m_sceneManager->addEntity(entity.release());
                }
            }
        }
        else
        {
            SY_ERRORF("[ImportService] No 2D scene available, %d entity(ies) cannot be imported", flatAdded);
            return ImportResult::fail(QStringLiteral("No scene manager available"), ImportErrorType::Unknown);
        }
        SY_DEBUGF("[ImportService] %d entity(ies) added to the 2D scene", flatAdded);
    }

    const int entityCount = meshAdded + flatAdded;
    if (entityCount == 0)
    {
        SY_ERROR("[ImportService] No entity landed in any scene");
        return ImportResult::fail(QStringLiteral("No scene manager available"), ImportErrorType::Unknown);
    }

    // ===== 3.4 还原源文件的图层与群组结构 =====
    // 只对 2D 通道有意义：3D 网格不参与图层体系与 SyGroup 树。
    // 时序约束：必须在图元进入场景、拿到运行时 EntityId 之后，才能按映射表反查归属。
    if (flatAdded > 0)
    {
        const int createdLayers = restoreImportedLayers(context, parseResult);
        SY_DEBUGF("[ImportService] Layer restore: %d created from %lld source layer(s), %lld entity mapping(s)",
            createdLayers,
            static_cast<long long>(parseResult.importedLayers.size()),
            static_cast<long long>(parseResult.entityLayerMap.size()));

        const int createdGroups = restoreImportedGroups(parseResult);
        SY_DEBUGF("[ImportService] Group restore: %d created from %lld source group(s), %lld entity mapping(s)",
            createdGroups,
            static_cast<long long>(parseResult.importedGroups.size()),
            static_cast<long long>(parseResult.entityGroupMap.size()));
    }
    else if (!parseResult.importedLayers.empty() || !parseResult.importedGroups.empty())
    {
        SY_WARNF("[ImportService] Skipping layer/group restore: no 2D entity imported (%zu layer(s), %zu group(s) "
                 "carried by IR are dropped)",
            parseResult.importedLayers.size(),
            parseResult.importedGroups.size());
    }

    SY_DEBUGF("[ImportService] Document built: %d entities (%d mesh + %d flat)", entityCount, meshAdded, flatAdded);
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

    // Refresh display phase

    // 工作台切换
    if (options.autoSwitchWorkbench)
    {
        const QString targetId =
            result.usedWorkbenchId.isEmpty() ? QStringLiteral("2D") : result.usedWorkbenchId;
        if (m_workbenchSwitchCallback)
        {
            SY_DEBUGF("[ImportService] Switching workbench to '%s'", targetId.toUtf8().constData());
            m_workbenchSwitchCallback(targetId);
        }
        else
        {
            SY_DEBUGF("[ImportService] Workbench switch to '%s' skipped: no callback registered",
                targetId.toUtf8().constData());
        }
    }

    // 视口适配
    if (options.autoFit)
    {
        if (m_viewportFitCallback)
        {
            m_viewportFitCallback();
        }
        else
        {
            SY_DEBUG("[ImportService] Viewport fit skipped: no callback registered");
        }
    }

    // 树结构刷新
    if (m_treeRebuildCallback)
    {
        m_treeRebuildCallback();
    }
    else
    {
        SY_DEBUG("[ImportService] Scene tree rebuild skipped: no callback registered");
    }

    // 属性面板刷新
    if (m_propertyRefreshCallback)
    {
        m_propertyRefreshCallback();
    }
    else
    {
        SY_DEBUG("[ImportService] Property panel refresh skipped: no callback registered");
    }

    updateProgress(ImportPhase::RefreshDisplay, 1.0f);
}

// ===== Write back state =====
void ImportService::phaseWriteBackState(const ImportContext& context, const ImportResult& result)
{
    updateProgress(ImportPhase::WriteBackState, 0.0f);
    emit importPhaseChanged(ImportPhase::WriteBackState);

    // Write back state phase

    // 更新当前文档路径（优先使用 context 中的回调）
    if (context.currentDocumentPathCallback)
    {
        context.currentDocumentPathCallback(context.sourcePath);
    }
    else if (m_currentDocumentPathCallback)
    {
        m_currentDocumentPathCallback(context.sourcePath);
    }
    else
    {
        SY_WARN("[ImportService] Current document path not updated: no callback registered");
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
    else
    {
        SY_WARN("[ImportService] Recent file list not updated: no callback registered");
    }

    // 文档持久化（使用成员变量回调，全局配置）
    if (m_documentPersistenceCallback)
    {
        m_documentPersistenceCallback(context.sourcePath, result.entityCount);
    }
    else
    {
        SY_DEBUG("[ImportService] Document persistence skipped: no callback registered");
    }

    // 更新状态栏（使用成员变量回调，全局配置）
    if (m_statusBarUpdateCallback)
    {
        QString statusMsg = QStringLiteral("Imported %1 entities from %2")
                                .arg(result.entityCount)
                                .arg(QFileInfo(context.sourcePath).fileName());
        m_statusBarUpdateCallback(statusMsg);
    }

    updateProgress(ImportPhase::WriteBackState, 1.0f);
}

int ImportService::restoreImportedLayers(const ImportContext& context, const ImportResult& parseResult)
{
    if (parseResult.importedLayers.empty())
    {
        // 该格式本身不带图层信息（OBJ / STL / PLT 等），无需还原
        return 0;
    }
    if (!m_layerManager)
    {
        SY_WARNF("[ImportService] Layer restore skipped: LayerManager not set, %zu source layer(s) dropped",
            parseResult.importedLayers.size());
        return 0;
    }
    if (!context.preserveLayers)
    {
        SY_DEBUGF("[ImportService] Layer restore skipped: preserveLayers=false, %lld source layer(s) ignored",
            static_cast<long long>(parseResult.importedLayers.size()));
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
        SY_DEBUGF("[ImportService] Layer restore: source='%s' color=0x%08X, findByName=%d",
            src.name, src.color, layerId);
        if (layerId < 0)
        {
            // 2) 名称不匹配时按颜色去重：同一颜色的源图层共用同一运行时图层
            layerId = m_layerManager->findLayerByColor(color);
            SY_DEBUGF("[ImportService] Layer restore: source='%s' findByColor=%d", src.name, layerId);
        }
        if (layerId < 0)
        {
            // 3) 名称与颜色都不匹配才新建图层，并应用源图层的可见性/锁定属性
            layerId = m_layerManager->createLayer(src.name);
            SY_DEBUGF("[ImportService] Layer restore: source='%s' created new layerId=%d", src.name, layerId);
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
        else
        {
            // 找到现有图层后，确保颜色与源图层匹配（避免找到颜色不同的现有图层）
            // 直接设置颜色，这样复用图层时也能保证颜色正确
            m_layerManager->setLayerColor(layerId, color);
            SY_DEBUGF("[ImportService] Layer restore: source='%s' reused existing layerId=%d (color set)", src.name, layerId);
        }
        sourceToLayerId[src.sourceId] = layerId;
    }

    // 将图元归属到对应图层
    // 逻辑：以实体的原始颜色为准，找到或创建对应颜色的图层，然后将实体关联到该图层
    // 注意：对于SVG导入，实体有overrideColor表示原始颜色，我们需要根据这个颜色来关联图层
    if (m_sceneManager && m_layerManager)
    {
        std::unordered_map<int64_t, int> entityToLayerId;
        int assignedCount = 0;
        
        for (auto* entity : m_sceneManager->getAllEntities())
        {
            if (!entity)
            {
                continue;
            }
            
            // 获取实体的原始颜色（overrideColor就是原始颜色）
            // 注意：在清除overrideColor之前获取颜色
            const Ut::Color& entityColor = entity->getColor();
            SY_DEBUGF("[ImportService] Entity %llu color: (%f,%f,%f)", 
                static_cast<unsigned long long>(entity->id),
                entityColor.r(), entityColor.g(), entityColor.b());
            uint8_t r = static_cast<uint8_t>(entityColor.r() * 255);
            uint8_t g = static_cast<uint8_t>(entityColor.g() * 255);
            uint8_t b = static_cast<uint8_t>(entityColor.b() * 255);
            Ut::Color color = Ut::Color::fromRGB255(r, g, b);
            
            // 查找或创建对应颜色的图层
            int layerId = m_layerManager->findOrCreateLayerByColor(color);
            if (layerId >= 0)
            {
                entityToLayerId[entity->id] = layerId;
                ++assignedCount;
            }
        }
        
        if (!entityToLayerId.empty())
        {
            m_layerManager->applyEntityLayerMap(entityToLayerId);
            SY_DEBUGF("[ImportService] Assigned %d entities to color-based layers", assignedCount);
        }
    }

    SY_DEBUGF("[ImportService] Restored %d/%lld source layer(s)", createdCount, static_cast<long long>(parseResult.importedLayers.size()));
    return createdCount;
}

int ImportService::restoreImportedGroups(const ImportResult& parseResult)
{
    if (parseResult.importedGroups.empty())
    {
        // 该格式本身不带群组信息，或解析层未产出群组表
        return 0;
    }
    if (!m_sceneManager)
    {
        SY_WARNF("[ImportService] Group restore skipped: SceneManager not set, %zu source group(s) dropped",
            parseResult.importedGroups.size());
        return 0;
    }

    // 群组 id 不复用 IR 的 sourceId：IR 的 sourceId 是 1-based 密集编号，与运行时 EntityId
    // 空间会撞车，所以这里统一用 createGroup 分配新 id，再靠 sourceId → SyGroup* 映射表衔接。
    Eg::GroupManager& groupManager = m_sceneManager->groupManager();

    std::unordered_map<uint64_t, Eg::SyGroup*> sourceToGroup;
    sourceToGroup.reserve(parseResult.importedGroups.size());

    // 第一遍：只建群组，不接父子。先全建出来可以免除对拓扑排序的依赖。
    int createdCount = 0;
    for (const auto& src : parseResult.importedGroups)
    {
        Eg::SyGroup* group = groupManager.createGroup(src.name);
        if (!group)
        {
            SY_WARNF("[ImportService] Failed to create group for source group %llu ('%s')",
                static_cast<unsigned long long>(src.sourceId),
                src.name);
            continue;
        }
        sourceToGroup[src.sourceId] = group;
        ++createdCount;
    }

    // 第二遍：重建父子层级。IR 侧已做过环检测，这里再用 wouldCreateCycle 兜底，
    // 保证即使上游契约被破坏也不会把场景挂成环导致 flatten 无限递归。
    for (const auto& src : parseResult.importedGroups)
    {
        if (src.parentSourceId == 0)
        {
            continue;
        }
        auto childIt = sourceToGroup.find(src.sourceId);
        auto parentIt = sourceToGroup.find(src.parentSourceId);
        if (childIt == sourceToGroup.end() || parentIt == sourceToGroup.end())
        {
            SY_WARNF("[ImportService] Group %llu references missing parent %llu, keeping it top-level",
                static_cast<unsigned long long>(src.sourceId),
                static_cast<unsigned long long>(src.parentSourceId));
            continue;
        }
        if (parentIt->second->wouldCreateCycle(childIt->second))
        {
            SY_WARNF("[ImportService] Group %llu would create a cycle under parent %llu, keeping it top-level",
                static_cast<unsigned long long>(src.sourceId),
                static_cast<unsigned long long>(src.parentSourceId));
            continue;
        }
        parentIt->second->addSubGroup(childIt->second);
    }

    // 第三遍：把图元挂到所属群组。EntityId → 源群组 sourceId → 运行时 SyGroup*
    size_t assignedCount = 0;
    size_t missingEntities = 0;
    for (const auto& [entityId, sourceId] : parseResult.entityGroupMap)
    {
        auto groupIt = sourceToGroup.find(sourceId);
        if (groupIt == sourceToGroup.end())
        {
            SY_WARNF("[ImportService] Entity %lld references unknown source group %llu, leaving it ungrouped",
                static_cast<long long>(entityId),
                static_cast<unsigned long long>(sourceId));
            continue;
        }

        Eg::SyEntity* entity = m_sceneManager->findEntityById(entityId);
        if (!entity)
        {
            // 图元可能因为几何非法在加入场景前就被丢弃，这属于正常损耗，只统计不逐条告警
            ++missingEntities;
            continue;
        }
        groupIt->second->addEntity(entity);
        ++assignedCount;
    }

    if (missingEntities > 0)
    {
        SY_WARNF("[ImportService] %zu group member(s) not found in scene, skipped", missingEntities);
    }
    SY_DEBUGF("[ImportService] Restored %d/%lld source group(s), %lld member assignment(s)",
        createdCount,
        static_cast<long long>(parseResult.importedGroups.size()),
        static_cast<long long>(assignedCount));
    return createdCount;
}


void ImportService::updateProgress(ImportPhase phase, float progress)
{
    // 阶段号只落 Debug 级：Info 级里已有每个 phase 的 START/END，这里避免重复噪音，
    // 但排查"卡在哪个阶段"时把日志级别降到 Debug 就能看到进度推进
    SY_DEBUGF("[ImportService] Progress: phase=%d, value=%.2f", static_cast<int>(phase), progress);
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