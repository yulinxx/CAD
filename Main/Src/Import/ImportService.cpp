#include "ImportService.h"
#include "ImportDispatcher.h"
#include "ImportResult.h"

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
    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ImportDispatcher not set");
        SY_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg);
    }

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

    // 通过分发器执行导入
    Fio::VecSyEntityPtr importedEntities;
    ImportResult result = m_dispatcher->dispatch(context, importedEntities);

    if (result.success && !importedEntities.empty())
    {
        // ---- 实体合法性过滤：拒绝坏数据进入 SceneManager / RenderWorld ----
        Fio::VecSyEntityPtr validEntities;
        validEntities.reserve(importedEntities.size());
        for (auto& entity : importedEntities)
        {
            if (entity && entity->isValid())
                validEntities.push_back(std::move(entity));
            else
            {
                SY_WARNF("[ImportService] Skipping invalid entity (null or degenerate geometry)");
                result.warnings.append(QStringLiteral("Skipped an invalid entity"));
            }
        }
        importedEntities = std::move(validEntities);

        result.entityCount = static_cast<int>(importedEntities.size());

        if (importedEntities.empty())
        {
            SY_WARN("[ImportService] All entities were invalid — nothing to import");
            result.message = QStringLiteral("No valid entities to import");
            result.success = false;
            if (m_stateCenter) m_stateCenter->setBusy(false);
            return result;
        }

        SY_INFOF("[ImportService] Preparing scene update: path=%s entities=%d newDocument=%d sceneManager=%p",
            context.sourcePath.toUtf8().constData(),
            result.entityCount,
            options.importAsNewDocument ? 1 : 0,
            m_sceneManager);

        // 如果作为新文档导入，先清空场景
        if (options.importAsNewDocument && m_sceneManager)
        {
            SY_INFO("[ImportService] Clearing scene before import");
            m_sceneManager->clearScene();
        }

        // 将导入的实体添加到场景（通过 SceneEditService 确保 Undo/图层分配/ID 冲突处理）
        int addedCount = static_cast<int>(importedEntities.size());
        if (m_editService)
        {
            m_editService->addEntities(std::move(importedEntities), "Import " + context.sourcePath.toStdString());
        }
        else if (m_sceneManager)
        {
            // 回退：无 SceneEditService 时直写 SceneManager（无 Undo）
            for (auto& entity : importedEntities)
            {
                if (entity)
                    m_sceneManager->addEntity(entity.release());
            }
        }

        SY_INFOF("[ImportService] Imported %d entities from: %s (added=%d)",
            result.entityCount, context.sourcePath.toUtf8().constData(), addedCount);

        result.message = QStringLiteral("Imported %1 entities successfully")
            .arg(result.entityCount);

        // 导入完成后统一 UI 刷新
        postImportRefresh(result, options);
    }
    else if (!result.success)
    {
        SY_ERRORF("[ImportService] Import failed: %s",
            result.message.toUtf8().constData());
    }

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

void ImportService::postImportRefresh(const ImportResult& result,
    const ImportOptions& options)
{
    // 工作台切换
    if (options.autoSwitchWorkbench && m_workbenchSwitchCallback)
    {
        QString targetId = result.usedWorkbenchId;
        if (targetId.isEmpty())
        {
            // 根据导入实体类型推断：3D 实体切到 3D，否则保持 2D
            targetId = QStringLiteral("2D");
        }
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

    SY_INFO("[ImportService] Post-import refresh completed");
}
