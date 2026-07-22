#include "ExportService.h"
#include "ExportDispatcher.h"

#include "Log/SyLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "../UI/UiStateCenter.h"

ExportService::ExportService(QObject* parent)
    : QObject(parent)
{
}

ExportService::~ExportService() = default;

void ExportService::setDispatcher(ExportDispatcher* dispatcher)
{
    m_dispatcher = dispatcher;
}

void ExportService::setSceneManager(Eg::SceneManager* sceneManager)
{
    m_sceneManager = sceneManager;
}

void ExportService::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

ExportResult ExportService::exportFile(const QString& filePath,
    const ExportOptions& options)
{
    ExportContext context;
    context.targetPath = filePath;
    return exportWithContext(context, options);
}

ExportResult ExportService::exportWithContext(const ExportContext& context,
    const ExportOptions& options)
{
    if (!m_dispatcher)
    {
        QString msg = QStringLiteral("ExportDispatcher not set");
        SY_ERRORF("[ExportService] %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    // 状态中心：标记繁忙
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(true);
        m_stateCenter->setMetadata({
            { QStringLiteral("statusPrompt"),
              QStringLiteral("Exporting: %1").arg(context.targetPath) }
        });
    }

    emit exportStarted(context.targetPath);

    // 收集场景实体（如果场景管理器可用）
    Fio::VecSyEntityPtr entities;
    if (m_sceneManager)
    {
        auto allEntities = m_sceneManager->getAllEntities();
        entities.reserve(allEntities.size());
        for (auto* e : allEntities)
            entities.push_back(e->clone());
    }

    if (entities.empty())
    {
        QString msg = QStringLiteral("No entities to export");
        SY_WARNF("[ExportService] %s", msg.toUtf8().constData());

        ExportResult emptyResult = ExportResult::fail(msg);
        if (m_stateCenter)
        {
            m_stateCenter->setBusy(false);
            m_stateCenter->setMetadata({
                { QStringLiteral("statusPrompt"), QStringLiteral("Export: no entities") }
            });
        }
        emit exportFinished(emptyResult);
        return emptyResult;
    }

    // 通过分发器执行导出
    ExportResult result = m_dispatcher->dispatch(context, entities);

    if (result.success)
    {
        result.exportedEntityCount = static_cast<int>(entities.size());
        result.message = QStringLiteral("Exported %1 entities to: %2")
            .arg(result.exportedEntityCount)
            .arg(context.targetPath);

        SY_INFOF("[ExportService] %s", result.message.toUtf8().constData());

        // 导出完成后状态回写
        postExportRecord(result, context);
    }
    else
    {
        SY_ERRORF("[ExportService] Export failed: %s",
            result.message.toUtf8().constData());
    }

    // 状态中心：清除繁忙
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        QString prompt = result.success
            ? QStringLiteral("Export completed: %1 entities")
                .arg(result.exportedEntityCount)
            : QStringLiteral("Export failed: %1").arg(result.message);
        m_stateCenter->setMetadata({
            { QStringLiteral("statusPrompt"), prompt }
        });
    }

    emit exportFinished(result);
    return result;
}

bool ExportService::canExport(const QString& filePath) const
{
    return m_dispatcher && m_dispatcher->canExport(filePath);
}

QStringList ExportService::supportedExtensions() const
{
    return m_dispatcher ? m_dispatcher->supportedExtensions() : QStringList();
}

Fio::VecSyEntityPtr ExportService::collectAllEntities() const
{
    Fio::VecSyEntityPtr entities;
    if (m_sceneManager)
    {
        auto allEntities = m_sceneManager->getAllEntities();
        entities.reserve(allEntities.size());
        for (auto* e : allEntities)
            entities.push_back(e->clone());
    }
    return entities;
}

void ExportService::postExportRecord(const ExportResult& result,
    const ExportContext& context)
{
    SY_INFOF("[ExportService] Export recorded: format=%d, entities=%d, path=%s",
        static_cast<int>(context.format),
        result.exportedEntityCount,
        context.targetPath.toUtf8().constData());
}
