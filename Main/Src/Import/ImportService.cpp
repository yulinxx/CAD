#include "ImportService.h"
#include "ImportDispatcher.h"
#include "ImportResult.h"

#include <QFileInfo>

#include "Log/SyLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "../UI/UiStateCenter.h"

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

void ImportService::setEditService(SceneEditService* editService)
{
    m_editService = editService;
}

void ImportService::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void ImportService::setViewportFitCallback(std::function<void()> callback)
{
    m_viewportFitCallback = std::move(callback);
}

void ImportService::setTreeRebuildCallback(std::function<void()> callback)
{
    m_treeRebuildCallback = std::move(callback);
}

void ImportService::setPropertyRefreshCallback(std::function<void()> callback)
{
    m_propertyRefreshCallback = std::move(callback);
}

void ImportService::setWorkbenchSwitchCallback(
    std::function<void(const QString&)> callback)
{
    m_workbenchSwitchCallback = std::move(callback);
}

void ImportService::setStatusBarUpdateCallback(
    std::function<void(const QString&)> callback)
{
    m_statusBarUpdateCallback = std::move(callback);
}

void ImportService::setRecentFileAddCallback(
    std::function<void(const QString&)> callback)
{
    m_recentFileAddCallback = std::move(callback);
}

void ImportService::setCurrentDocumentPathCallback(
    std::function<void(const QString&)> callback)
{
    m_currentDocumentPathCallback = std::move(callback);
}

void ImportService::setDocumentPersistenceCallback(
    std::function<void(const QString&, int)> callback)
{
    m_documentPersistenceCallback = std::move(callback);
}

ImportResult ImportService::importFile(const QString& filePath,
    const ImportOptions& options)
{
    ImportContext context;
    context.sourcePath = filePath;
    return importWithContext(context, options);
}

ImportResult ImportService::importWithContext(const ImportContext& context,
    const ImportOptions& options)
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

    SY_INFOF("[ImportService] stateCenter=%p, sceneManager=%p, editService=%p",
        m_stateCenter, m_sceneManager, m_editService);

    // 状态中心：标记繁忙
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(true);
        m_stateCenter->setMetadata({
            { QStringLiteral("statusPrompt"),
              QStringLiteral("Importing: %1").arg(context.sourcePath) }
            });
    }

    emit importStarted(context.sourcePath);

    // 创建可修改的上下文副本（用于注入检测到的格式）
    ImportContext mutableCtx = context;

    // 提前声明，避免 goto 跳过初始化
    Fio::VecSyEntityPtr importedEntities;

    // ===== 阶段1：识别文件格式 =====
    SY_INFO("[ImportService] Phase 1: Detect format");
    ImportResult result = phaseDetectFormat(mutableCtx);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 1 failed: %s", result.message.toUtf8().constData());
        goto cleanup;
    }
    SY_INFOF("[ImportService] Phase 1 completed: format=%d", static_cast<int>(mutableCtx.format));

    if (isCanceled(mutableCtx))
    {
        result = ImportResult::fail(QStringLiteral("Import canceled"),
            ImportErrorType::Canceled);
        goto cleanup;
    }

    // ===== 阶段2：解析文件 =====
    SY_INFO("[ImportService] Phase 2: Parse file");
    result = phaseParse(mutableCtx, importedEntities);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 2 failed: %s", result.message.toUtf8().constData());
        goto cleanup;
    }
    SY_INFOF("[ImportService] Phase 2 completed: entities=%zu", importedEntities.size());

    if (isCanceled(mutableCtx))
    {
        result = ImportResult::fail(QStringLiteral("Import canceled"),
            ImportErrorType::Canceled);
        goto cleanup;
    }

    // ===== 阶段3：构建文档 =====
    SY_INFO("[ImportService] Phase 3: Build document");
    result = phaseBuildDocument(mutableCtx, importedEntities, options);
    if (!result.success)
    {
        SY_ERRORF("[ImportService] Phase 3 failed: %s", result.message.toUtf8().constData());
        goto cleanup;
    }
    SY_INFOF("[ImportService] Phase 3 completed: entityCount=%d", result.entityCount);

    if (isCanceled(mutableCtx))
    {
        result = ImportResult::fail(QStringLiteral("Import canceled"),
            ImportErrorType::Canceled);
        goto cleanup;
    }

    // ===== 阶段4：刷新显示 =====
    SY_INFO("[ImportService] Phase 4: Refresh display");
    phaseRefreshDisplay(result, options);
    SY_INFO("[ImportService] Phase 4 completed");

    // ===== 阶段5：回写状态 =====
    SY_INFO("[ImportService] Phase 5: Write back state");
    phaseWriteBackState(mutableCtx, result);
    SY_INFO("[ImportService] Phase 5 completed");

cleanup:
    // 状态中心：清除繁忙
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        if (!result.success)
        {
            m_stateCenter->setMetadata({
                { QStringLiteral("statusPrompt"),
                  QStringLiteral("Import failed: %1").arg(result.message) }
                });
        }
        else
        {
            m_stateCenter->setMetadata({
                { QStringLiteral("statusPrompt"),
                  QStringLiteral("Import completed: %1 entities")
                      .arg(result.entityCount) }
                });
        }
    }

    emit importFinished(result);

    SY_INFOF("[ImportService] importWithContext END: success=%d, message=%s",
        result.success ? 1 : 0, result.message.toUtf8().constData());

    return result;
}

bool ImportService::canImport(const QString& filePath) const
{
    return m_dispatcher && m_dispatcher->canImport(filePath);
}

QStringList ImportService::supportedExtensions() const
{
    return m_dispatcher ? m_dispatcher->supportedExtensions() : QStringList();
}

// ===== 阶段1：识别文件格式 =====
ImportResult ImportService::phaseDetectFormat(ImportContext& context)
{
    updateProgress(ImportPhase::DetectFormat, 0.0f);
    emit importPhaseChanged(ImportPhase::DetectFormat);

    SY_INFOF("[ImportService] Phase 1: Detecting format for: %s",
        context.sourcePath.toUtf8().constData());

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
        context.format = ImportDispatcher::detectFormat(context.sourcePath);

    if (context.format == Fio::FileFormat::Unknown)
    {
        QString msg = QStringLiteral("Unsupported file format: %1")
            .arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 检查是否有对应的读取器
    if (!m_dispatcher->canImport(context.sourcePath))
    {
        QString msg = QStringLiteral("No reader registered for format: %1")
            .arg(fi.suffix().toUpper());
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    SY_INFOF("[ImportService] Format detected: %d", static_cast<int>(context.format));
    updateProgress(ImportPhase::DetectFormat, 1.0f);

    return ImportResult::ok();
}

// ===== 阶段2：解析文件 =====
ImportResult ImportService::phaseParse(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    updateProgress(ImportPhase::Parse, 0.0f);
    emit importPhaseChanged(ImportPhase::Parse);

    SY_INFOF("[ImportService] Phase 2: Parsing file: %s",
        context.sourcePath.toUtf8().constData());

    // 通过分发器执行解析
    ImportResult result = m_dispatcher->dispatch(context, outEntities);

    if (!result.success)
    {
        // 将通用失败转换为解析失败
        if (result.errorType == ImportErrorType::None)
            result.errorType = ImportErrorType::ParseFailed;
        SY_ERRORF("[ImportService] Parse failed: %s",
            result.message.toUtf8().constData());
        return result;
    }

    SY_INFOF("[ImportService] Parsed %d entities",
        static_cast<int>(outEntities.size()));
    updateProgress(ImportPhase::Parse, 1.0f);

    return result;
}

// ===== 阶段3：构建文档 =====
ImportResult ImportService::phaseBuildDocument(const ImportContext& context,
    Fio::VecSyEntityPtr& entities, const ImportOptions& options)
{
    updateProgress(ImportPhase::BuildDocument, 0.0f);
    emit importPhaseChanged(ImportPhase::BuildDocument);

    SY_INFOF("[ImportService] Phase 3: Building document, entities=%d, newDoc=%d",
        static_cast<int>(entities.size()),
        options.importAsNewDocument ? 1 : 0);

    // 实体合法性过滤：拒绝坏数据进入 SceneManager / RenderWorld
    Fio::VecSyEntityPtr validEntities;
    validEntities.reserve(entities.size());
    for (auto& entity : entities)
    {
        if (entity && entity->isValid())
            validEntities.push_back(std::move(entity));
        else
        {
            SY_WARNF("[ImportService] Skipping invalid entity");
        }
    }
    entities = std::move(validEntities);

    if (entities.empty())
    {
        SY_WARN("[ImportService] All entities were invalid — nothing to import");
        return ImportResult::fail(QStringLiteral("No valid entities to import"),
            ImportErrorType::ParseFailed);
    }

    int entityCount = static_cast<int>(entities.size());

    // 如果作为新文档导入，先清空场景
    if (options.importAsNewDocument && m_sceneManager)
    {
        SY_INFO("[ImportService] Clearing scene before import");
        m_sceneManager->clearScene();
    }

    // 将导入的实体添加到场景（通过 SceneEditService 确保 Undo/图层分配/ID 冲突处理）
    if (m_editService)
    {
        SY_INFOF("[ImportService] Calling addEntities: editService=%p, entities=%d", 
            m_editService, static_cast<int>(entities.size()));
        m_editService->addEntities(std::move(entities),
            "Import " + context.sourcePath.toStdString());
        SY_INFO("[ImportService] addEntities completed successfully");
    }
    else if (m_sceneManager)
    {
        // 回退：无 SceneEditService 时直写 SceneManager（无 Undo）
        SY_INFOF("[ImportService] Direct adding to sceneManager=%p, entities=%d", 
            m_sceneManager, static_cast<int>(entities.size()));
        for (auto& entity : entities)
        {
            if (entity)
                m_sceneManager->addEntity(entity.release());
        }
        SY_INFO("[ImportService] Direct add completed");
    }
    else
    {
        SY_ERROR("[ImportService] Neither SceneEditService nor SceneManager available");
        return ImportResult::fail(QStringLiteral("No scene manager available"),
            ImportErrorType::Unknown);
    }

    SY_INFOF("[ImportService] Document built: %d entities", entityCount);
    updateProgress(ImportPhase::BuildDocument, 1.0f);

    return ImportResult::ok(
        QStringLiteral("Imported %1 entities successfully").arg(entityCount),
        entityCount);
}

// ===== 阶段4：刷新显示 =====
void ImportService::phaseRefreshDisplay(const ImportResult& result,
    const ImportOptions& options)
{
    updateProgress(ImportPhase::RefreshDisplay, 0.0f);
    emit importPhaseChanged(ImportPhase::RefreshDisplay);

    SY_INFO("[ImportService] Phase 4: Refreshing display");

    // 工作台切换
    if (options.autoSwitchWorkbench && m_workbenchSwitchCallback)
    {
        QString targetId = result.usedWorkbenchId;
        if (targetId.isEmpty())
            targetId = QStringLiteral("2D");
        m_workbenchSwitchCallback(targetId);
    }

    // 视口适配
    if (options.autoFit && m_viewportFitCallback)
        m_viewportFitCallback();

    // 树结构刷新
    if (m_treeRebuildCallback)
        m_treeRebuildCallback();

    // 属性面板刷新
    if (m_propertyRefreshCallback)
        m_propertyRefreshCallback();

    SY_INFO("[ImportService] Display refreshed");
    updateProgress(ImportPhase::RefreshDisplay, 1.0f);
}

// ===== 阶段5：回写状态 =====
void ImportService::phaseWriteBackState(const ImportContext& context,
    const ImportResult& result)
{
    updateProgress(ImportPhase::WriteBackState, 0.0f);
    emit importPhaseChanged(ImportPhase::WriteBackState);

    SY_INFO("[ImportService] Phase 5: Writing back state");

    // 更新当前文档路径（优先使用 context 中的回调）
    if (context.currentDocumentPathCallback)
        context.currentDocumentPathCallback(context.sourcePath);
    else if (m_currentDocumentPathCallback)
        m_currentDocumentPathCallback(context.sourcePath);

    // 添加到最近文件（优先使用 context 中的回调）
    if (context.recentFileAddCallback)
        context.recentFileAddCallback(context.sourcePath);
    else if (m_recentFileAddCallback)
        m_recentFileAddCallback(context.sourcePath);

    // 文档持久化（使用成员变量回调，全局配置）
    if (m_documentPersistenceCallback)
        m_documentPersistenceCallback(context.sourcePath, result.entityCount);

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

void ImportService::updateProgress(ImportPhase phase, float progress)
{
    emit importProgress(progress);
}

bool ImportService::isCanceled(const ImportContext& context) const
{
    if (context.cancelCallback)
        return context.cancelCallback();
    return false;
}