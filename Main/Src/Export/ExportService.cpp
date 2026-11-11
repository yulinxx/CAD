#include <ctime>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <utility>

/**
 * @file ExportService.cpp
 * @brief 导出服务实现
 *
 * 管理文件导出流程，协调各格式写入器。
 */
#include "ExportService.h"
#include "ExportDispatcher.h"

#include "Log/AprLogger.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Models/DocumentRecord.h"
#include "Persistence/Repositories/DocumentRepository.h"
#include "FileIO/FileFormat.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>

namespace
{
    /// worker 退出时置完成位（所有返回路径都会析构到它）。
    /// 用 promise 而不是固定时长 sleep：只有线程真正结束才置位，
    /// 等待方据此立即返回，避免每次导出都白等一段固定时间。
    class WorkerCompletionGuard
    {
    public:
        explicit WorkerCompletionGuard(std::shared_ptr<std::promise<void>> done)
            : m_done(std::move(done))
        {
        }

        ~WorkerCompletionGuard()
        {
            if (!m_done)
            {
                return;
            }
            // set_value 只在重复置位时抛异常，单次守卫不会走到；仍兜底避免析构抛
            try
            {
                m_done->set_value();
            }
            catch (...)
            {
            }
        }

        WorkerCompletionGuard(const WorkerCompletionGuard&) = delete;
        WorkerCompletionGuard& operator=(const WorkerCompletionGuard&) = delete;

    private:
        std::shared_ptr<std::promise<void>> m_done;
    };
}  // namespace

void ExportService::setPersistenceService(PersistenceService* service)
{
    m_persistenceService = service;
}

void ExportService::setNativeMachiningSnapshotProvider(std::function<NativeDocumentSnapshot()> provider)
{
    m_machiningSnapshotProvider = std::move(provider);
}

void ExportService::captureMachiningSnapshot(ExportContext& context) const
{
    // 只在主线程调用：provider 会读 LayerManager / HardwareProfileManager，
    // 两者都不是线程安全的。采集结果是纯 POD 值，可以安全地交给后台 writer。
    if (!m_machiningSnapshotProvider)
    {
        context.hasMachiningSnapshot = false;
        return;
    }
    try
    {
        context.machiningSnapshot = m_machiningSnapshotProvider();
        context.hasMachiningSnapshot = true;
    }
    catch (const std::exception& e)
    {
        // 快照失败不能把整次导出变成「已保存全部参数」：保持 false，
        // NativeExportWriter 会给出显式 warning，操作员不会误判。
        context.machiningSnapshot = NativeDocumentSnapshot();
        context.hasMachiningSnapshot = false;
        APR_ERRORF("[ExportService] Machining snapshot failed: %s", e.what());
    }
}

ExportService::ExportService(QObject* parent)
    : QObject(parent)
{
}

ExportService::~ExportService()
{
    // 析构时收掉可能仍在跑的异步导出 worker，避免后台线程访问已析构的宿主
    shutdownAsyncThread();
}

void ExportService::shutdownAsyncThread()
{
    if (!m_asyncThread.joinable())
    {
        return;
    }
    // 通知 worker 尽快退出（dispatch 内部无 cancel 检查点）
    m_asyncCancelFlag.store(true);
    APR_INFO("[ExportService] shutdownAsyncThread: cancel flag set, waiting worker...");

    // 等 worker 真正结束的完成信号：上一次导出早已收尾时信号已置位，这里立即返回，
    // 不再像旧实现那样每次操作都无条件 sleep 3 秒。3 秒只作为诊断阈值——超时说明
    // worker 卡在磁盘 I/O 上，此时仍必须 join：joinable 的 std::thread 析构会
    // 触发 std::terminate，而写盘无法中断，只能等它自己返回。
    if (m_asyncDone.valid() &&
        m_asyncDone.wait_for(std::chrono::milliseconds(3000)) != std::future_status::ready)
    {
        APR_WARN("[ExportService] shutdownAsyncThread: worker still running after 3s, waiting for it to finish");
    }
    m_asyncThread.join();
    APR_INFO("[ExportService] shutdownAsyncThread: worker joined");
}

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

void ExportService::exportAsync(
    const ExportContext& context, const ExportOptions& options, std::function<void(const ExportResult&)> onComplete)
{
    APR_DEBUGF("[ExportService] exportAsync START: path=%s", context.targetPath.toUtf8().constData());

    if (!m_dispatcher)
    {
        QString msg = tr("ExportDispatcher not set");
        APR_ERRORF("[ExportService] exportAsync aborted: %s", msg.toUtf8().constData());
        if (onComplete)
        {
            onComplete(ExportResult::fail(msg));
        }
        return;
    }

    // 同 service 不允许并发导出：收掉上一条异步 worker，且互斥锁保证
    shutdownAsyncThread();
    if (m_exportBusy.exchange(true))
    {
        // 已经有导出在跑，拒绝新请求
        QString msg = tr("Another export is in progress");
        APR_WARNF("[ExportService] exportAsync rejected: %s", msg.toUtf8().constData());
        if (onComplete)
        {
            onComplete(ExportResult::fail(msg));
        }
        return;
    }
    // 重置取消标志：新的 exportAsync 开始，旧的 cancel 信号作废
    m_asyncCancelFlag.store(false);

    // 忙状态与开始通知都在调用线程（主线程）发出：
    // 这些回调最终会动 UI，放到后台线程里发是跨线程访问
    if (m_busyStateCallback)
    {
        m_busyStateCallback(true);
    }
    if (m_statusPromptCallback)
    {
        m_statusPromptCallback(tr("Exporting: %1").arg(context.targetPath));
    }
    emit exportStarted(context.targetPath);

    // 构造完整上下文：注入 sceneManager 指针先用于 collectAllEntities，
    // 之后在 dispatch 之前置 nullptr 切断 NativeExportWriter 的"借用实体"路径
    ExportContext fullCtx = context;
    if (!fullCtx.sceneManager)
    {
        fullCtx.sceneManager = m_sceneManager;
    }
    if (!fullCtx.sceneManager3D)
    {
        fullCtx.sceneManager3D = m_sceneManager3D;
    }

    // 注入进度回调，桥接 Qt 信号。后台线程 emit 跨线程信号用 Qt AutoConnection
    // 自动升级为 QueuedConnection，是线程安全的
    if (!fullCtx.progressCallback)
    {
        fullCtx.progressCallback = [this](float progress, const char* /*stage*/) {
            emit exportProgress(progress);
        };
    }

    // 注入取消标志指针：让后台线程 writer 内部能定期检查
    fullCtx.cancelFlag = &m_asyncCancelFlag;

    // Phase 1 主线程：采集 .apr 加工语义快照（图层表 + Profile 值集）。
    // 必须在启动 worker 之前完成：后台线程只拿到值，不碰 Manager 指针。
    captureMachiningSnapshot(fullCtx);

    // Phase 1 主线程：clone 场景图元（SceneManager 非线程安全，必须在主线程访问）
    Fio::VecAprEntityPtr entities = collectAllEntities();

    // 收集完之后切断场景指针，让 NativeExportWriter 走 clone 路径，
    // 避免后台线程访问主线程持有的 SceneManager
    fullCtx.sceneManager = nullptr;
    fullCtx.sceneManager3D = nullptr;

    if (entities.empty())
    {
        // 无图元直接走主线程收尾：不走后台线程，省一次线程启动 + 析构
        QString msg = tr("No entities to export");
        APR_WARNF("[ExportService] exportAsync: %s", msg.toUtf8().constData());
        ExportResult emptyResult = ExportResult::fail(msg);
        if (m_busyStateCallback) m_busyStateCallback(false);
        if (m_statusPromptCallback) m_statusPromptCallback(tr("Export: no entities"));
        emit exportFinished(emptyResult);
        m_exportBusy.store(false);
        if (onComplete) onComplete(emptyResult);
        return;
    }

    // 用 shared_ptr 包装 entities（vector<unique_ptr> 不可拷贝，不能按值捕获进 lambda）
    auto sharedEntities = std::make_shared<Fio::VecAprEntityPtr>(std::move(entities));

    // 使用 QPointer 安全持有 this：若 ExportService 被销毁，
    // QPointer 自动置空，后续所有路径安全返回，避免悬空指针访问
    QPointer<ExportService> safeSelf = this;

    // 统一收尾：清忙状态 + 最终提示 + 完成信号 + 回调，保证每条退出路径都恰好走一次，
    // 且始终在主线程执行（后台线程失败时也会经此回到主线程）
    auto finishOnMainThread = [safeSelf, onComplete](const ExportResult& r, const ExportContext& ctx) {
        QMetaObject::invokeMethod(
            qApp,
            [safeSelf, onComplete, r, ctx]() {
                if (safeSelf.isNull())
                    return;
                if (safeSelf->m_busyStateCallback)
                {
                    safeSelf->m_busyStateCallback(false);
                }
                if (safeSelf->m_statusPromptCallback)
                {
                    QString prompt = r.success
                        ? ExportService::tr("Export completed: %1 entities").arg(r.exportedEntityCount)
                        : ExportService::tr("Export failed: %1").arg(r.message);
                    if (!r.warnings.isEmpty())
                    {
                        prompt += QStringLiteral(" (") + r.warnings.join(QStringLiteral("; ")) + QStringLiteral(")");
                    }
                    safeSelf->m_statusPromptCallback(prompt);
                }
                // postExportRecord 必须在主线程调（PersistenceService/SQLite 非线程安全）
                if (r.success)
                {
                    if (!safeSelf->postExportRecord(r, ctx))
                    {
                        ExportResult warnResult = r;
                        warnResult.addWarning(QCoreApplication::translate("WorkbenchWindow", "History save failed"));
                        emit safeSelf->exportFinished(warnResult);
                        if (onComplete) onComplete(warnResult);
                    }
                    else
                    {
                        emit safeSelf->exportFinished(r);
                        if (onComplete) onComplete(r);
                    }
                }
                else
                {
                    emit safeSelf->exportFinished(r);
                    if (onComplete) onComplete(r);
                }
                safeSelf->m_exportBusy.store(false);
                APR_DEBUGF("[ExportService] exportAsync END: success=%d, entities=%d, message=%s",
                    r.success ? 1 : 0,
                    r.exportedEntityCount,
                    r.message.toUtf8().constData());
            },
            Qt::QueuedConnection);
    };

    // 为本次 worker 建立完成信号：worker 尾部置位，shutdownAsyncThread 靠它判断
    // 线程是否真的结束，不再用固定时长 sleep 推测
    auto donePromise = std::make_shared<std::promise<void>>();
    m_asyncDone = donePromise->get_future().share();

    // 启动后台线程执行 Phase 2（纯序列化 + 文件 I/O，不碰场景与 UI）
    m_asyncThread = std::thread([this, safeSelf, fullCtx, sharedEntities, finishOnMainThread, donePromise]() {
        WorkerCompletionGuard doneGuard(donePromise);
        if (safeSelf.isNull())
            return;

        // 外部请求取消（析构或新 exportAsync 启动时置 true）
        if (m_asyncCancelFlag.load())
        {
            APR_INFO("[ExportService] Async export canceled before dispatch");
            ExportResult cancelResult = ExportResult::fail(ExportService::tr("Export canceled"));
            finishOnMainThread(cancelResult, fullCtx);
            return;
        }

        // Phase 2 后台线程：dispatch 序列化 + 文件写入（纯 CPU + I/O，不碰场景与 UI）
        ExportResult result = safeSelf->m_dispatcher->dispatch(fullCtx, *sharedEntities);

        if (result.success)
        {
            result.exportedEntityCount = static_cast<int>(sharedEntities->size());
            result.message = tr("Exported %1 entities to: %2").arg(result.exportedEntityCount).arg(fullCtx.targetPath);
        }
        else
        {
            APR_ERRORF("[ExportService] Async export failed: %s", result.message.toUtf8().constData());
        }

        // Phase 3 主线程收尾：QMetaObject::invokeMethod + Qt::QueuedConnection
        finishOnMainThread(result, fullCtx);
    });
}

ExportResult ExportService::exportWithContext(const ExportContext& context, const ExportOptions& options)
{
    if (!m_dispatcher)
    {
        QString msg = tr("ExportDispatcher not set");
        APR_ERRORF("[ExportService] %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    // 互斥锁保护：同一时刻只允许一个导出在跑（与 exportAsync 共享 m_exportBusy）
    if (m_exportBusy.exchange(true))
    {
        QString msg = tr("Another export is in progress");
        APR_WARNF("[ExportService] exportWithContext rejected: %s", msg.toUtf8().constData());
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

    // 构造完整上下文：注入 sceneManager 指针先用于 collectAllEntities
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

    // 采集 .apr 加工语义快照：与异步路径一致，取的是主线程上的值快照
    captureMachiningSnapshot(fullCtx);

    // 收集场景图元
    Fio::VecAprEntityPtr entities = collectAllEntities();

    // 收集完之后切断场景指针，与 exportAsync 保持一致：
    // NativeExportWriter 的借用路径会访问原始 SceneManager，此处统一走 clone 路径
    fullCtx.sceneManager = nullptr;
    fullCtx.sceneManager3D = nullptr;

    ExportResult result;

    if (entities.empty())
    {
        QString msg = tr("No entities to export");
        APR_WARNF("[ExportService] %s", msg.toUtf8().constData());

        result = ExportResult::fail(msg);
    }
    else
    {
        // 通过分发器执行导出（主线程）
        result = m_dispatcher->dispatch(fullCtx, entities);

        if (result.success)
        {
            result.exportedEntityCount = static_cast<int>(entities.size());
            result.message = tr("Exported %1 entities to: %2").arg(result.exportedEntityCount).arg(context.targetPath);

            // 导出完成，写入记录。文件已成功导出，但历史记录写入失败时
            // 必须在结果里带 warning，避免上层状态栏只报"导出完成"。
            if (!postExportRecord(result, context))
            {
                result.addWarning(QCoreApplication::translate("WorkbenchWindow", "History save failed"));
            }
        }
        else
        {
            APR_ERRORF("[ExportService] Export failed: %s", result.message.toUtf8().constData());
        }
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
        if (!result.warnings.isEmpty())
        {
            prompt += QStringLiteral(" (") + result.warnings.join(QStringLiteral("; ")) + QStringLiteral(")");
        }
        m_statusPromptCallback(prompt);
    }

    emit exportFinished(result);
    m_exportBusy.store(false);
    return result;
}

bool ExportService::exportRecoverySnapshot(const QString& filePath, QString& error)
{
    if (!m_dispatcher)
    {
        error = tr("Export service not initialized (dispatcher not set)");
        return false;
    }
    if (m_exportBusy.exchange(true))
    {
        error = tr("Another export is in progress");
        return false;
    }

    // 与正式导出完全同源：同一套加工语义快照 + 图元克隆，
    // 但不写导出记录、不推忙状态/状态栏、不发信号、不弹错误框。
    ExportContext fullCtx;
    fullCtx.targetPath = filePath;
    fullCtx.sceneManager = m_sceneManager;
    fullCtx.sceneManager3D = m_sceneManager3D;
    captureMachiningSnapshot(fullCtx);

    Fio::VecAprEntityPtr entities = collectAllEntities();
    fullCtx.sceneManager = nullptr;
    fullCtx.sceneManager3D = nullptr;

    const ExportResult result = m_dispatcher->dispatch(fullCtx, entities);
    m_exportBusy.store(false);

    if (!result.success)
    {
        error = result.message;
        APR_WARNF("[ExportService] Recovery snapshot failed: %s", error.toUtf8().constData());
        return false;
    }
    return true;
}

bool ExportService::canExport(const QString& filePath, QString* reasonOut) const
{
    if (!m_dispatcher)
    {
        const QString reason =
            QStringLiteral("Export service not initialized (dispatcher not set): %1").arg(filePath);
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        APR_DEBUGF("[ExportService] canExport=false: %s", reason.toUtf8().constData());
        return false;
    }
    return m_dispatcher->canExport(filePath, reasonOut);
}

QStringList ExportService::supportedExtensions() const
{
    return m_dispatcher ? m_dispatcher->supportedExtensions() : QStringList();
}

Fio::VecAprEntityPtr ExportService::collectAllEntities() const
{
    Fio::VecAprEntityPtr entities;

    // 收集 2D 图元
    if (m_sceneManager)
    {
        auto allEntities = m_sceneManager->getAllEntities();
        entities.reserve(allEntities.size());
        for (auto* e : allEntities)
        {
            // ABI: clone 在 Engine2D 分配，跨 DLL delete 安全（需保证所有模块使用相同堆）
            auto* copy = e->clone();
            // clone() 会分配新 ID，但导出必须保留场景中的持久 ID：
            // 否则 FillRegion.source_ids、群组/图层引用在重新打开后会指向不存在的实体。
            // 合成 ID（渲染/临时用途）为负数或 0，不写回；持久 ID 从 1 开始且场景内唯一。
            if (copy && e->id > 0)
            {
                copy->id = e->id;
            }
            entities.push_back(std::unique_ptr<Eg::AprEntity>(copy));
        }
    }

    // 收集 3D 网格图元
    if (m_sceneManager3D)
    {
        // 使用 C 风格回调收集 3D 图元（SceneManager3D 接口约束）
        struct CollectCtx
        {
            Fio::VecAprEntityPtr* entities;
            size_t count;
        };

        CollectCtx ctx = { &entities, 0 };

        m_sceneManager3D->forEachEntity(
            [](Eg::AprMeshEntity* entity, void* userData) {
                if (entity)
                {
                    auto* ctx = static_cast<CollectCtx*>(userData);
                    // AprMeshEntity 继承 AprEntity，clone() 返回 AprEntity*
                    // 包装为 unique_ptr<AprEntity> 保持多态正确释放
                    auto* copy = entity->clone();
                    // 与 2D 一致：保留持久 ID，避免每次保存后 ID 漂移
                    if (copy && entity->id > 0)
                    {
                        copy->id = entity->id;
                    }
                    ctx->entities->push_back(std::unique_ptr<Eg::AprEntity>(copy));
                    ++ctx->count;
                }
            },
            &ctx);
    }

    return entities;
}

bool ExportService::postExportRecord(const ExportResult& result, const ExportContext& context)
{
    if (!result.success || !m_persistenceService || !m_persistenceService->isOpen())
    {
        return true;
    }
    auto* repo = m_persistenceService->documents();
    if (!repo)
    {
        return true;
    }

    // 菜单导出路径的 context.format 可能是 Unknown（由 ExportDispatcher 在
    // dispatch() 内部才检测），不能直接把 Unknown 写进记录。这里按目标路径
    // 再检测一次；title 为空时用文件名兜底。
    Fio::FileFormat resolvedFormat = context.format;
    if (resolvedFormat == Fio::FileFormat::Unknown)
    {
        resolvedFormat = ExportDispatcher::detectFormat(context.targetPath);
    }
    const QFileInfo targetInfo(context.targetPath);
    const QString fallbackTitle = targetInfo.fileName();

    // 文档历史按工作台分桶（与 recent_files 一致）：3D 格式归 "3D"，其余归 "2D"。
    // ExportService 同时能收 2D/3D 图元，导出的是哪种格式就归哪个工作台。
    const auto is3DFormat = [](Fio::FileFormat fmt) {
        switch (fmt)
        {
        case Fio::FileFormat::STL:
        case Fio::FileFormat::OBJ:
        case Fio::FileFormat::STEP:
        case Fio::FileFormat::Native3D:
            return true;
        default:
            return false;
        }
    };
    const QString workbenchId = is3DFormat(resolvedFormat) || targetInfo.suffix().toLower() == QStringLiteral("apr3d")
        ? QStringLiteral("3D")
        : QStringLiteral("2D");

    DocumentRecord record;
    record.filePath = context.targetPath.toStdString();
    record.title = (!context.sourceDocumentId.isEmpty() ? context.sourceDocumentId : fallbackTitle).toStdString();
    record.workbench = workbenchId.toStdString();
    record.entityCount = static_cast<int>(result.exportedEntityCount);
    record.format = [&targetInfo](Fio::FileFormat fmt) -> std::string {
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
            // 兜底用扩展名，避免 PLT/AI/PNG 等格式被记成 Unknown
            {
                const QString suffix = targetInfo.suffix().toUpper();
                return suffix.isEmpty() ? std::string("Unknown") : suffix.toStdString();
            }
        }
    }(resolvedFormat);
    record.lastSavedAt = std::to_string(std::time(nullptr));

    if (!repo->save(record))
    {
        APR_WARNF("[ExportService] Failed to save export record: %s (%d entities)",
            context.targetPath.toUtf8().constData(),
            result.exportedEntityCount);
        return false;
    }
    APR_DEBUGF("[ExportService] Export record saved: %s (%d entities)",
        context.targetPath.toUtf8().constData(),
        result.exportedEntityCount);
    return true;
}