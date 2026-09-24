#include <ctime>
/**
 * @file ExportService.cpp
 * @brief 导出服务实现
 *
 * 管理文件导出流程，协调各格式写入器。
 */
#include "ExportService.h"
#include "ExportDispatcher.h"

#include "Log/SyLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Models/DocumentRecord.h"
#include "Persistence/Repositories/DocumentRepository.h"
#include "FileIO/FileFormat.h"

void ExportService::setPersistenceService(PersistenceService* service)
{
    m_persistenceService = service;
}

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

void ExportService::setSceneManager3D(Eg::SceneManager3D* sceneManager3D)
{
    m_sceneManager3D = sceneManager3D;
}

void ExportService::setBusyStateCallback(std::function<void(bool)> callback)
{
    m_busyStateCallback = std::move(callback);
}

void ExportService::setStatusPromptCallback(std::function<void(const QString&)> callback)
{
    m_statusPromptCallback = std::move(callback);
}

ExportResult ExportService::exportFile(const QString& filePath, const ExportOptions& options)
{
    ExportContext context;
    context.targetPath = filePath;
    return exportWithContext(context, options);
}

ExportResult ExportService::exportWithContext(const ExportContext& context, const ExportOptions& options)
{
    if (!m_dispatcher)
    {
        QString msg = tr("ExportDispatcher not set");
        SY_ERRORF("[ExportService] %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    // 通过回调通知忙状态（替代旧的 UiStateCenter 直接依赖）
    if (m_busyStateCallback)
    {
        m_busyStateCallback(true);
    }
    if (m_statusPromptCallback)
    {
        m_statusPromptCallback(tr("Exporting: %1").arg(context.targetPath));
    }

    emit exportStarted(context.targetPath);

    // 构造完整上下文，注入 sceneManager 指针供 Native 格式借用实体
    ExportContext fullCtx = context;
    if (!fullCtx.sceneManager)
    {
        fullCtx.sceneManager = m_sceneManager;
    }
    if (!fullCtx.sceneManager3D)
    {
        fullCtx.sceneManager3D = m_sceneManager3D;
    }

    // 注入进度回调，桥接 Qt 信号
    if (!fullCtx.progressCallback)
    {
        fullCtx.progressCallback = [this](float progress, const char* /*stage*/) {
            emit exportProgress(progress);
        };
    }

    // 收集场景图元（P5 收口: 复用 collectAllEntities 消除重复）
    Fio::VecSyEntityPtr entities = collectAllEntities();

    if (entities.empty())
    {
        QString msg = tr("No entities to export");
        SY_WARNF("[ExportService] %s", msg.toUtf8().constData());

        ExportResult emptyResult = ExportResult::fail(msg);
        if (m_busyStateCallback)
        {
            m_busyStateCallback(false);
        }
        if (m_statusPromptCallback)
        {
            m_statusPromptCallback(tr("Export: no entities"));
        }
        emit exportFinished(emptyResult);
        return emptyResult;
    }

    // 通过分发器执行导出
    ExportResult result = m_dispatcher->dispatch(fullCtx, entities);

    if (result.success)
    {
        result.exportedEntityCount = static_cast<int>(entities.size());
        result.message = tr("Exported %1 entities to: %2").arg(result.exportedEntityCount).arg(context.targetPath);

        // Export completed
        postExportRecord(result, context);
    }
    else
    {
        SY_ERRORF("[ExportService] Export failed: %s", result.message.toUtf8().constData());
    }

    // 通过回调清除忙状态（替代旧的 UiStateCenter 直接依赖）
    if (m_busyStateCallback)
    {
        m_busyStateCallback(false);
    }
    if (m_statusPromptCallback)
    {
        QString prompt = result.success ? tr("Export completed: %1 entities").arg(result.exportedEntityCount)
                                        : tr("Export failed: %1").arg(result.message);
        m_statusPromptCallback(prompt);
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

    // 收集 2D 图元
    if (m_sceneManager)
    {
        auto allEntities = m_sceneManager->getAllEntities();
        entities.reserve(allEntities.size());
        for (auto* e : allEntities)
        {
            // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
            entities.push_back(std::unique_ptr<Eg::SyEntity>(e->clone()));
        }
    }

    // 收集 3D 网格图元
    if (m_sceneManager3D)
    {
        // 使用 C 风格回调收集 3D 图元（SceneManager3D 接口约束）
        struct CollectCtx
        {
            Fio::VecSyEntityPtr* entities;
            size_t count;
        };

        CollectCtx ctx = { &entities, 0 };

        m_sceneManager3D->forEachEntity(
            [](Eg::SyMeshEntity* entity, void* userData) {
                if (entity)
                {
                    auto* ctx = static_cast<CollectCtx*>(userData);
                    // SyMeshEntity 继承 SyEntity，clone() 返回 SyEntity*
                    // 包装为 unique_ptr<SyEntity> 保持多态正确释放
                    ctx->entities->push_back(std::unique_ptr<Eg::SyEntity>(entity->clone()));
                    ++ctx->count;
                }
            },
            &ctx);
    }

    return entities;
}

void ExportService::postExportRecord(const ExportResult& result, const ExportContext& context)
{
    if (!result.success || !m_persistenceService || !m_persistenceService->isOpen())
    {
        return;
    }
    auto* repo = m_persistenceService->documents();
    if (!repo)
    {
        return;
    }

    DocumentRecord record;
    record.filePath = context.targetPath.toStdString();
    record.title = context.sourceDocumentId.toStdString();
    record.entityCount = static_cast<int>(result.exportedEntityCount);
    record.format = [](Fio::FileFormat fmt) -> std::string {
        switch (fmt)
        {
        case Fio::FileFormat::DXF:
            return "DXF";
        case Fio::FileFormat::SVG:
            return "SVG";
        case Fio::FileFormat::STEP:
            return "STEP";
        case Fio::FileFormat::OBJ:
            return "OBJ";
        case Fio::FileFormat::STL:
            return "STL";
        case Fio::FileFormat::PDF:
            return "PDF";
        case Fio::FileFormat::Native:
            return "SY";
        case Fio::FileFormat::Native3D:
            return "SYX";
        default:
            return "Unknown";
        }
    }(context.format);
    record.lastSavedAt = std::to_string(std::time(nullptr));

    repo->save(record);
    SY_DEBUGF("[ExportService] Export record saved: %s (%d entities)",
        context.targetPath.toUtf8().constData(),
        result.exportedEntityCount);
}