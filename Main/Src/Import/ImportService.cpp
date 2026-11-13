/**
 * @file ImportService.cpp
 * @brief 导入服务实现
 *
 * 管理文件导入流程，协调各格式解析器。
 */
#include "ImportService.h"
#include "ImportDispatcher.h"
#include "ImportResult.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <thread>

#include "Log/AprLogger.h"
#include "Engine/EntityIdGenerator.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Interaction/HardwareProfileManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"
#include "UI/Render/SceneRefreshCoordinator.h"
#if BUILD_UI3D
    #include "UI3D/Edit/SceneEditService3D.h"
#else
class SceneEditService3D;
#endif

#include "Color/Color.hpp"

// ==================== RAII 导入守卫 ====================
// 替代 goto cleanup 模式：构造时设忙状态，析构时清除忙状态 + 写最终状态
// 解决五阶段流程中 goto cleanup 的代码可读性和维护性问题
namespace
{
    /// worker 退出时置完成位（所有返回路径都会析构到它）。
    /// 用 promise 而不是固定时长 sleep：只有线程真正结束才置位，
    /// 等待方据此立即返回，避免每次导入都白等一段固定时间。
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
                    promptCb(QCoreApplication::translate("ImportService", "Import completed: %1 entities")
                            .arg(m_result.entityCount));
                }
                else
                {
                    promptCb(QCoreApplication::translate("ImportService", "Import failed: %1").arg(m_result.message));
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

    /// 导入 phase 3-5 的刷新抑制守卫：构造时挂起 2D 视口的定时刷新调度，
    /// 析构时（含失败/取消路径）恢复并让协调器一次性补刷。
    /// 协调器为空（3D 导入工作台未注入）时整体退化为空操作。
    class ScopedRefreshSuppression
    {
    public:
        explicit ScopedRefreshSuppression(SceneRefreshCoordinator* coordinator)
            : m_coordinator(coordinator)
        {
            if (m_coordinator)
            {
                m_coordinator->beginRefreshSuppression();
            }
        }

        ~ScopedRefreshSuppression()
        {
            if (m_coordinator)
            {
                m_coordinator->endRefreshSuppression();
            }
        }

        ScopedRefreshSuppression(const ScopedRefreshSuppression&) = delete;
        ScopedRefreshSuppression& operator=(const ScopedRefreshSuppression&) = delete;

    private:
        SceneRefreshCoordinator* m_coordinator;
    };

    // ---- 原生 .apr 加工语义还原 ----

    /// 把 .apr 的硬件 Profile **值集**整体还原进 HardwareProfileManager。
    ///
    /// 必须在图层绑定之前执行：LayerManager::setLayerHardwareProfileId 会校验
    /// id 是否存在，值集没落地时所有绑定都会被拒，表现仍是「参数全丢」。
    /// restoreProfiles 保留文件里的原始 id —— 重新分配 id 会让图层指向错误的
    /// 功率/速度，属加工事故而不是普通显示错误。
    ///
    /// @return false = 还原失败，调用方必须跳过图层绑定还原；true = 无需还原或已成功
    bool restoreNativeProfileValueSet(LayerManager* layerManager, const ImportResult& parseResult)
    {
        if (!parseResult.hasNativeMachiningSnapshot)
        {
            // 旧文件没有值集：保持运行时现有 Profile，绝不清空用户参数
            return true;
        }

        HardwareProfileManager* profiles =
            layerManager ? layerManager->hardwareProfileManager() : nullptr;
        if (!profiles)
        {
            APR_ERRORF("[ImportService] .apr carries %zu profile(s) but HardwareProfileManager is not "
                      "injected; profile bindings cannot be restored",
                parseResult.importedHardwareProfiles.size());
            return false;
        }

        std::vector<HardwareProfile> batch;
        batch.reserve(parseResult.importedHardwareProfiles.size());
        for (const auto& p : parseResult.importedHardwareProfiles)
        {
            HardwareProfile hp;
            hp.id = p.id;
            hp.name = p.name;            // char[128]，构造 std::string 时按 NUL 截断
            hp.power = p.power;
            hp.speed = p.speed;
            hp.frequency = p.frequency;  // kHz；Eg::ProcessParams 用 Hz，别混
            hp.focusOffset = p.focusOffset;
            hp.passes = p.passes;
            hp.kerfMm = p.kerfMm;
            hp.leadInLengthMm = p.leadInLengthMm;
            hp.leadInMode = p.leadInMode;
            hp.leadInAngleDeg = p.leadInAngleDeg;
            hp.contourOrdering = p.contourOrdering;
            batch.push_back(std::move(hp));
        }

        if (!profiles->restoreProfiles(batch))
        {
            APR_ERRORF("[ImportService] .apr profile value set rejected (%zu entr(y/ies)); keeping runtime "
                      "profiles and skipping layer bindings",
                batch.size());
            return false;
        }
        APR_DEBUGF("[ImportService] Restored %zu hardware profile(s) from .apr", batch.size());
        return true;
    }

    /// 把一条图层的加工语义落到运行时图层上。
    /// @param bindingsRestorable 值集是否成功还原；false 时不能套用 profile id
    ///        （旧文件可能只有悬空引用，套到运行时 Profile 上会串层）
    void applyLayerMachining(LayerManager* layerManager,
        int layerId,
        const ImportResult::LayerMachining& machining,
        bool bindingsRestorable)
    {
        layerManager->setLayerOutput(layerId, machining.output);
        if (machining.defaultProcessId != 0)
        {
            layerManager->setLayerDefaultProcessId(layerId, machining.defaultProcessId);
        }
        if (!bindingsRestorable)
        {
            return;
        }
        if (!layerManager->setLayerHardwareProfileId(layerId, machining.hardwareProfileId))
        {
            APR_WARNF("[ImportService] Layer %d: hardware profile id %d from .apr not found, binding skipped",
                layerId,
                machining.hardwareProfileId);
        }
        if (!layerManager->setLayerFillHardwareProfileId(layerId, machining.fillHardwareProfileId))
        {
            APR_WARNF("[ImportService] Layer %d: fill hardware profile id %d from .apr not found, binding skipped",
                layerId,
                machining.fillHardwareProfileId);
        }
    }

    /// 图层显示扩展（矢量/位图、填充开关、填充色）。IrLayerInfo 是冻结的跨 DLL
    /// POD 装不下这些字段，只有原生 .apr 走这条补充通道。
    void applyLayerDisplayExtension(
        LayerManager* layerManager, int layerId, const ImportResult::LayerMachining& machining)
    {
        if (!machining.hasDisplayExtension)
        {
            return;
        }
        layerManager->setLayerType(layerId, static_cast<Eg::LayerType>(machining.layerType));
        layerManager->setLayerFill(layerId, machining.fill);
        const uint32_t fillColor = machining.fillColor;
        const int alphaBits = static_cast<int>((fillColor >> 24) & 0xFF);
        // 历史零 alpha 位代表「旧版本没这个概念」，不是真的全透明
        const int alpha = (alphaBits == 0) ? 255 : alphaBits;
        layerManager->setLayerFillColor(layerId,
            Ut::Color::fromRGB255(static_cast<int>((fillColor >> 16) & 0xFF),
                static_cast<int>((fillColor >> 8) & 0xFF),
                static_cast<int>(fillColor & 0xFF),
                alpha));
    }

    // ---- 原生 .apr ProcessData → 图层工艺参数 ----

    /// FileIO 的工艺参数 POD → Eg::ProcessParams。
    /// 两边字段一一对应；focusOffsetMm 在文档 v2 的 ProcessParamsData 里没有对应字段，
    /// 保持默认 0（与旧档案读出来的语义一致）。
    Eg::ProcessParams aprProcessParamsToEg(const Fio::AprProcessParamsInfo& src)
    {
        Eg::ProcessParams p;
        p.enabled = src.enabled;
        p.powerPercent = src.powerPercent;
        p.speedMmPerSec = src.speedMmPerSec;
        p.frequencyHz = src.frequencyHz;
        p.pulseWidthUs = src.pulseWidthUs;
        p.waveformIndex = src.waveformIndex;
        p.passes = src.passes;
        p.fillEnabled = src.fillEnabled;
        p.fillIntervalMm = src.fillIntervalMm;
        p.fillAngleDeg = src.fillAngleDeg;
        p.fillMode = static_cast<Eg::FillMode>(src.fillMode);
        p.kerfMm = src.kerfMm;
        p.rapidSpeedMmPerSec = src.rapidSpeedMmPerSec;
        p.leadInEnabled = src.leadInEnabled;
        p.leadInLengthMm = src.leadInLengthMm;
        p.leadInMode = src.leadInMode;
        p.leadInAngleDeg = src.leadInAngleDeg;
        p.contourOrdering = src.contourOrdering;
        return p;
    }

    /// 把 .apr v2 的 ProcessData 按图层 ID 回灌到 LayerManager。
    ///
    /// 目标图层解析顺序与 proto 注释一致：
    ///   1. target_layer_ids 非空 → 只作用于这些源图层（显式优先）；
    ///   2. 否则 → 作用于 default_process_id == process.id 的源图层（图层反向绑定）。
    ///
    /// 幂等性：回灌前先清空本次落地图层的旧参数。否则「同名图层复用」会沿用上一份
    /// 文档的参数，而 `setLayerProcessParams` 是整体覆盖、不做字段累加，因此
    /// 连续打开同一文件、或「打开→保存→再打开」都不会让功率/速度漂移。
    ///
    /// 找不到对应图层的条目必须如实告警并计数，不能静默丢弃。
    /// 旧档案（processCount()==0）直接跳过：保持「同名图层沿用用户设置」的旧行为。
    /// @return 成功落到运行时图层上的（工艺, 图层）条目数
    int restoreNativeProcesses(LayerManager* layerManager,
        const ImportResult& parseResult,
        const std::unordered_map<uint32_t, int>& sourceToLayerId,
        QStringList* outWarnings)
    {
        if (parseResult.importedProcesses.empty())
        {
            return 0;
        }
        if (!layerManager)
        {
            const QString msg = QStringLiteral(
                "This .apr file carries %1 process object(s) but no layer manager is available; "
                "their machining parameters were not applied.")
                                    .arg(parseResult.importedProcesses.size());
            APR_WARNF("[ImportService] .apr carries %zu process object(s) but LayerManager is not set",
                parseResult.importedProcesses.size());
            if (outWarnings)
            {
                outWarnings->append(msg);
            }
            return 0;
        }

        // 先清后写：本次导入涉及的运行时图层一律从「无文档参数」开始
        for (const auto& entry : sourceToLayerId)
        {
            layerManager->clearLayerProcessParams(entry.second);
        }

        // 工艺对象按 order 稳定排序：多条工艺命中同一图层时结果确定，
        // 不会因为文件里的条目顺序变化而出现「同样的文件、不同的参数」
        std::vector<const ImportResult::ProcessBinding*> ordered;
        ordered.reserve(parseResult.importedProcesses.size());
        for (const auto& binding : parseResult.importedProcesses)
        {
            ordered.push_back(&binding);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const ImportResult::ProcessBinding* a, const ImportResult::ProcessBinding* b) {
                return a->info.order < b->info.order;
            });

        int applied = 0;
        int missingTargetLayerCount = 0;
        int emptyTargetProcessCount = 0;

        for (const ImportResult::ProcessBinding* binding : ordered)
        {
            std::vector<uint32_t> targets;
            if (!binding->targetLayerIds.empty())
            {
                targets = binding->targetLayerIds;
            }
            else
            {
                for (const auto& machining : parseResult.importedLayerMachining)
                {
                    if (machining.defaultProcessId != 0
                        && machining.defaultProcessId == binding->info.id)
                    {
                        targets.push_back(machining.sourceId);
                    }
                }
            }

            if (targets.empty())
            {
                ++emptyTargetProcessCount;
                APR_WARNF("[ImportService] Process #%u ('%s') targets no layer; its machining parameters were "
                          "not applied",
                    binding->info.id,
                    binding->info.name);
                continue;
            }

            const Eg::ProcessParams params = aprProcessParamsToEg(binding->info.params);
            int appliedForProcess = 0;
            for (uint32_t sourceLayerId : targets)
            {
                const auto it = sourceToLayerId.find(sourceLayerId);
                if (it == sourceToLayerId.end())
                {
                    ++missingTargetLayerCount;
                    continue;
                }
                if (layerManager->setLayerProcessParams(it->second, params))
                {
                    ++appliedForProcess;
                    ++applied;
                }
            }

            if (appliedForProcess == 0)
            {
                APR_WARNF("[ImportService] Process #%u ('%s') references %zu layer(s) but none exists in this "
                          "document; parameters dropped",
                    binding->info.id,
                    binding->info.name,
                    targets.size());
            }
        }

        if (outWarnings)
        {
            if (missingTargetLayerCount > 0)
            {
                outWarnings->append(QStringLiteral(
                    "This .apr file has %1 process target-layer reference(s) that do not exist in the "
                    "document; their machining parameters were kept from the file but not applied.")
                                        .arg(missingTargetLayerCount));
            }
            if (emptyTargetProcessCount > 0)
            {
                outWarnings->append(QStringLiteral(
                    "%1 process object(s) in this .apr file target no layer; their machining parameters "
                    "were not applied.")
                                        .arg(emptyTargetProcessCount));
            }
        }

        APR_DEBUGF("[ImportService] ProcessData reflow: %d applied, %d missing target layer(s), "
                   "%d process(es) without target",
            applied,
            missingTargetLayerCount,
            emptyTargetProcessCount);
        return applied;
    }
}  // anonymous namespace

// ==================== ImportService 实现 ====================

ImportService::ImportService(QObject* parent)
    : QObject(parent)
{
}

ImportService::~ImportService()
{
    // 析构时收掉可能仍在跑的异步导入 worker
    shutdownAsyncThread();
}

void ImportService::shutdownAsyncThread()
{
    if (!m_asyncThread.joinable())
    {
        return;
    }
    // 通知 worker 尽快退出（phase 之间会检查此 flag）
    m_asyncCancelFlag.store(true);
    APR_INFO("[ImportService] shutdownAsyncThread: cancel flag set, waiting worker...");

    // 等 worker 真正结束的完成信号：上一次导入早已收尾时信号已置位，这里立即返回，
    // 不再像旧实现那样每次操作都无条件 sleep 3 秒。3 秒只作为诊断阈值——超时说明
    // worker 卡在第三方 I/O 上，此时仍必须 join：joinable 的 std::thread 析构会
    // 触发 std::terminate，而停不下来的解析只能等它自己返回。
    if (m_asyncDone.valid() &&
        m_asyncDone.wait_for(std::chrono::milliseconds(3000)) != std::future_status::ready)
    {
        APR_WARN("[ImportService] shutdownAsyncThread: worker still running after 3s, waiting for it to finish");
    }
    m_asyncThread.join();
    APR_INFO("[ImportService] shutdownAsyncThread: worker joined");
}

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
    APR_DEBUGF("[ImportService] setLayerManager: %p", static_cast<void*>(m_layerManager));
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

void ImportService::setDisplayRefreshCallback(std::function<void()> cb)
{
    m_displayRefreshCallback = std::move(cb);
}

void ImportService::setWorkbenchSwitchCallback(std::function<void(const QString&)> cb)
{
    m_workbenchSwitchCallback = std::move(cb);
}

void ImportService::setRecentFileAddCallback(std::function<void(const QString&, const QString&)> cb)
{
    m_recentFileAddCallback = std::move(cb);
}

void ImportService::setCurrentDocumentPathCallback(std::function<void(const QString&)> cb)
{
    m_currentDocumentPathCallback = std::move(cb);
}

void ImportService::setDocumentPersistenceCallback(std::function<bool(const QString&, int, const QString&)> cb)
{
    m_documentPersistenceCallback = std::move(cb);
}

void ImportService::setRefreshCoordinator(SceneRefreshCoordinator* coordinator)
{
    m_refreshCoordinator = coordinator;
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
        QString msg = tr("ImportDispatcher not set");
        APR_ERRORF("[ImportService] importWithContext aborted: %s", msg.toUtf8().constData());
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
        m_statusPromptCallback(tr("Importing: %1").arg(context.sourcePath));
    }

    emit importStarted(context.sourcePath);

    // 创建可修改的上下文副本（用于注入检测到的格式）
    ImportContext mutableCtx = context;

    // 提前声明，避免 goto 跳过初始化 → RAII 守卫下无需此约束
    Fio::VecAprEntityPtr importedEntities;

    // ===== 1：识别文件格式 =====
    ImportResult result = phaseDetectFormat(mutableCtx);
    if (!result.success)
    {
        APR_ERRORF("[ImportService] Phase 1 failed: %s", result.message.toUtf8().constData());
        return fail(result);
    }

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(tr("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 2：解析文件 =====
    ImportResult parseResult = phaseParse(mutableCtx, importedEntities);
    if (!parseResult.success)
    {
        APR_ERRORF("[ImportService] Phase 2 failed: %s", parseResult.message.toUtf8().constData());
        return fail(parseResult);
    }

    if (isCanceled(mutableCtx))
    {
        return fail(ImportResult::fail(tr("Import canceled"), ImportErrorType::Canceled));
    }

    // ===== 3-5：构建文档 → 刷新显示 → 回写状态 =====
    // 导入期间抑制 2D 视口刷新调度：commitProgress 会泵送事件循环，16ms 刷新定时器
    // 若在写入中途触发，会对半成品场景做全量重建（97 万图元导入耗时的大头）。
    // 抑制到 phase 5 结束（含失败/取消路径，RAII 兜底），由协调器一次性补刷。
    {
        ScopedRefreshSuppression suppression(m_refreshCoordinator);

        // ===== 3：构建文档 =====
        result = phaseBuildDocument(mutableCtx, importedEntities, options, parseResult);
        if (!result.success)
        {
            APR_ERRORF("[ImportService] Phase 3 failed: %s", result.message.toUtf8().constData());
            return fail(result);
        }

        if (isCanceled(mutableCtx))
        {
            return fail(ImportResult::fail(tr("Import canceled"), ImportErrorType::Canceled));
        }

        // ===== 4：刷新显示 =====
        phaseRefreshDisplay(mutableCtx, result, options);

        // ===== 5：回写状态 =====
        phaseWriteBackState(mutableCtx, result);
    }

    // RAII 守卫在析构时自动清除忙状态并写最终提示
    guard.setResult(result);

    emit importFinished(result);

    return result;
}

void ImportService::importAsync(
    const ImportContext& context, const ImportOptions& options, std::function<void(const ImportResult&)> onComplete)
{
    APR_DEBUGF("[ImportService] importAsync START: path=%s", context.sourcePath.toUtf8().constData());

    if (!m_dispatcher)
    {
        QString msg = tr("ImportDispatcher not set");
        APR_ERRORF("[ImportService] importAsync aborted: %s", msg.toUtf8().constData());
        if (onComplete)
        {
            onComplete(ImportResult::fail(msg, ImportErrorType::Unknown));
        }
        return;
    }

    // 收掉上一条可能还在跑的异步 worker（同 service 不允许并发 importAsync）
    shutdownAsyncThread();
    // 重置取消标志：新的 importAsync 开始，旧的 cancel 信号作废
    m_asyncCancelFlag.store(false);

    // 忙状态与开始通知都在调用线程（主线程）发出：
    // 这些回调最终会动 UI，放到后台线程里发是跨线程访问
    if (m_busyStateCallback)
    {
        m_busyStateCallback(true);
    }
    if (m_statusPromptCallback)
    {
        m_statusPromptCallback(tr("Importing: %1").arg(context.sourcePath));
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
                if (safeSelf.isNull())
                    return;
                if (safeSelf->m_busyStateCallback)
                {
                    safeSelf->m_busyStateCallback(false);
                }
                if (safeSelf->m_statusPromptCallback)
                {
                    safeSelf->m_statusPromptCallback(r.success
                            ? ImportService::tr("Import completed: %1 entities").arg(r.entityCount)
                            : ImportService::tr("Import failed: %1").arg(r.message));
                }
                emit safeSelf->importFinished(r);
                if (onComplete)
                {
                    onComplete(r);
                }
                APR_DEBUGF("[ImportService] importAsync END: success=%d, entities=%d, message=%s",
                    r.success ? 1 : 0,
                    r.entityCount,
                    r.message.toUtf8().constData());
            },
            Qt::QueuedConnection);
    };

    // 为本次 worker 建立完成信号：worker 尾部置位，shutdownAsyncThread 靠它判断
    // 线程是否真的结束，不再用固定时长 sleep 推测
    auto donePromise = std::make_shared<std::promise<void>>();
    m_asyncDone = donePromise->get_future().share();

    // 启动后台线程执行 Phase 1-2（纯解析，不碰 UI 与场景）
    m_asyncThread = std::thread([this, safeSelf, context, options, finishOnMainThread, donePromise]() {
        WorkerCompletionGuard doneGuard(donePromise);
        if (safeSelf.isNull())
            return;
        ImportContext mutableCtx = context;
        auto importedEntities = std::make_shared<Fio::VecAprEntityPtr>();

        // ===== Async Phase 1: Detect format =====
        ImportResult result = safeSelf->phaseDetectFormat(mutableCtx);
        if (!result.success)
        {
            APR_ERRORF("[ImportService] Async Phase 1 failed: %s", result.message.toUtf8().constData());
            finishOnMainThread(result);
            return;
        }

        // 外部请求取消（析构或新 importAsync 启动时置 true）
        if (m_asyncCancelFlag.load() || safeSelf->isCanceled(mutableCtx))
        {
            APR_INFO("[ImportService] Async import canceled after Phase 1");
            finishOnMainThread(ImportResult::fail(ImportService::tr("Import canceled"), ImportErrorType::Canceled));
            return;
        }

        // ===== Async Phase 2: Parse file =====
        ImportResult parseResult = safeSelf->phaseParse(mutableCtx, *importedEntities);
        if (!parseResult.success)
        {
            APR_ERRORF("[ImportService] Async Phase 2 failed: %s", parseResult.message.toUtf8().constData());
            finishOnMainThread(parseResult);
            return;
        }

        // 外部请求取消（析构或新 importAsync 启动时置 true）
        if (m_asyncCancelFlag.load() || safeSelf->isCanceled(mutableCtx))
        {
            APR_INFO("[ImportService] Async import canceled after Phase 2");
            finishOnMainThread(ImportResult::fail(ImportService::tr("Import canceled"), ImportErrorType::Canceled));
            return;
        }

        // Phase 3-5 必须回主线程：要动场景、视口与状态栏
        auto entities = importedEntities;
        QMetaObject::invokeMethod(
            qApp,
            [safeSelf, mutableCtx, entities, parseResult, options, finishOnMainThread]() mutable {
                if (safeSelf.isNull())
                    return;
                ImportContext mainCtx = mutableCtx;

                // 导入期间抑制 2D 视口刷新调度（同 importWithContext：避免中途全量重建）
                ScopedRefreshSuppression suppression(safeSelf->m_refreshCoordinator);

                if (safeSelf->isCanceled(mainCtx))
                {
                    APR_INFO("[ImportService] Async import canceled before Phase 3");
                    finishOnMainThread(
                        ImportResult::fail(ImportService::tr("Import canceled"), ImportErrorType::Canceled));
                    return;
                }

                // Build document on main thread
                ImportResult result = safeSelf->phaseBuildDocument(mainCtx, *entities, options, parseResult);
                if (!result.success)
                {
                    APR_ERRORF("[ImportService] Async Phase 3 failed: %s", result.message.toUtf8().constData());
                    finishOnMainThread(result);
                    return;
                }

                safeSelf->phaseRefreshDisplay(mainCtx, result, options);
                safeSelf->phaseWriteBackState(mainCtx, result);

                finishOnMainThread(result);
            },
            Qt::QueuedConnection);
    });
}

bool ImportService::canImport(const QString& filePath, QString* reasonOut) const
{
    if (!m_dispatcher)
    {
        const QString reason = tr("Import service not initialized (dispatcher not registered): %1").arg(filePath);
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        APR_DEBUGF("[ImportService] canImport=false: %s", reason.toUtf8().constData());
        return false;
    }
    return m_dispatcher->canImport(filePath, reasonOut);
}

QStringList ImportService::supportedExtensions() const
{
    return m_dispatcher ? m_dispatcher->supportedExtensions() : QStringList();
}

QStringList ImportService::supportedExtensions(const QString& workbenchId) const
{
    if (!m_dispatcher)
    {
        return QStringList();
    }

    // 直接使用 ImportDispatcher 的按工作台过滤方法
    // 每个读取器通过 dimension() 方法声明自己支持的维度（2D/3D/通用）
    return m_dispatcher->supportedExtensions(workbenchId);
}

const QStringList& ImportService::imageExtensions()
{
    // 位图判据唯一来源：拖拽与菜单（图片对话框 / 全部支持）都从这里取，
    // 禁止在 UI 侧再抄一份列表，否则必然出现「拖拽能导入、菜单选不到」。
    static const QStringList kImageExtensions = { QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("bmp"),
        QStringLiteral("tga"),
        QStringLiteral("tiff"),
        QStringLiteral("tif"),
        QStringLiteral("gif"),
        QStringLiteral("webp") };
    return kImageExtensions;
}

bool ImportService::isImageExtension(const QString& extension)
{
    // 调用方负责把后缀转成小写
    return imageExtensions().contains(extension);
}

QStringList ImportService::importableExtensions(const QString& workbenchId) const
{
    // 未知工作台（含空串与初始化期的 "default"）不按维度过滤，与拖拽侧语义一致
    const bool knownWorkbench = (workbenchId == QStringLiteral("2D") || workbenchId == QStringLiteral("3D"));
    QStringList exts = knownWorkbench ? supportedExtensions(workbenchId) : supportedExtensions();
    // 位图不经过读取器，必须由同一判据补入
    exts.append(imageExtensions());
    exts.removeDuplicates();
    return exts;
}

QStringList ImportService::extensionsForFormat(Fio::FileFormat format) const
{
    return m_dispatcher ? m_dispatcher->extensionsForFormat(format) : QStringList();
}

bool ImportService::canImport(const QString& filePath, const QString& workbenchId, QString* reasonOut) const
{
    if (!m_dispatcher)
    {
        const QString reason = tr("Import service not initialized (dispatcher not registered): %1").arg(filePath);
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        APR_DEBUGF("[ImportService] canImport=false: %s", reason.toUtf8().constData());
        return false;
    }

    // 直接使用 ImportDispatcher 的按工作台过滤方法
    return m_dispatcher->canImport(filePath, workbenchId, reasonOut);
}

// ===== 1：识别文件格式 =====
ImportResult ImportService::phaseDetectFormat(ImportContext& context)
{
    updateProgress(context, ImportPhase::DetectFormat, 0.0f);
    emit importPhaseChanged(ImportPhase::DetectFormat);

    // Detect format phase

    // 文件存在性检查
    QFileInfo fi(context.sourcePath);
    if (!fi.exists())
    {
        QString msg = tr("File not found: %1").arg(context.sourcePath);
        APR_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FileNotFound);
    }

    // 格式检测
    if (context.format == Fio::FileFormat::Unknown)
    {
        context.format = ImportDispatcher::detectFormat(context.sourcePath);
    }

    if (context.format == Fio::FileFormat::Unknown)
    {
        QString msg = tr("Unsupported file format: %1").arg(fi.suffix().toUpper());
        APR_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    // 检查是否有对应的读取器
    if (!m_dispatcher->canImport(context.sourcePath))
    {
        QString msg = tr("No reader registered for format: %1").arg(fi.suffix().toUpper());
        APR_ERRORF("[ImportService] %s", msg.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
    }

    updateProgress(context, ImportPhase::DetectFormat, 1.0f);

    return ImportResult::ok();
}

// ===== Parse file =====
ImportResult ImportService::phaseParse(const ImportContext& context, Fio::VecAprEntityPtr& outEntities)
{
    updateProgress(context, ImportPhase::Parse, 0.0f);
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
        APR_ERRORF("[ImportService] Parse failed: %s", result.message.toUtf8().constData());
        return result;
    }

    APR_DEBUGF("[ImportService] Parsed %d entities", static_cast<int>(outEntities.size()));
    updateProgress(context, ImportPhase::Parse, 1.0f);

    return result;
}

// ===== 3：构建文档 =====
ImportResult ImportService::phaseBuildDocument(const ImportContext& context,
    Fio::VecAprEntityPtr& entities,
    const ImportOptions& options,
    const ImportResult& parseResult)
{
    updateProgress(context, ImportPhase::BuildDocument, 0.0f);
    emit importPhaseChanged(ImportPhase::BuildDocument);

    // Build document phase

    // 图元合法性过滤：拒绝坏数据进入 SceneManager / RenderWorld
    Fio::VecAprEntityPtr validEntities;
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
        APR_WARNF("[ImportService] Entity validation dropped %d null + %d invalid, %zu remaining",
            nullCount,
            invalidCount,
            entities.size());
    }

    if (entities.empty())
    {
        APR_ERRORF("[ImportService] All %d parsed entity(ies) were rejected by validation, nothing to import",
            nullCount + invalidCount);
        return ImportResult::fail(tr("No valid entities to import"), ImportErrorType::ParseFailed);
    }

    // 如果作为新文档导入，先清空场景
    if (options.importAsNewDocument && m_sceneManager)
    {
        APR_DEBUG("[ImportService] Clearing 2D scene before import");
        m_sceneManager->clearScene();
    }

    // ===== 3.1 按图元类型分拣：网格进 3D 场景，其余进 2D 场景 =====
    // 混合文件（例如同一个 OBJ 里既有网格又有线框标注）两侧都必须落地。
    std::vector<std::unique_ptr<Eg::AprMeshEntity>> meshEntities;
    Fio::VecAprEntityPtr flatEntities;
    for (auto& entity : entities)
    {
        if (!entity)
        {
            continue;
        }
        if (entity->eType == Eg::EType::MESH)
        {
            meshEntities.push_back(std::unique_ptr<Eg::AprMeshEntity>(static_cast<Eg::AprMeshEntity*>(entity.release())));
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
        APR_WARNF("[ImportService] SceneManager3D not set, %zu mesh entity(ies) fall back to the 2D scene",
            meshEntities.size());
        for (auto& mesh : meshEntities)
        {
            flatEntities.push_back(std::unique_ptr<Eg::AprEntity>(mesh.release()));
        }
        meshEntities.clear();
    }

    const bool hasMeshEntities = !meshEntities.empty();
    APR_DEBUGF("[ImportService] Entity split: %lld mesh -> 3D scene, %lld flat -> 2D scene",
        static_cast<long long>(meshEntities.size()),
        static_cast<long long>(flatEntities.size()));

    // ===== 3.2 网格图元落地到 3D 场景 =====
    int meshAdded = 0;
    if (hasMeshEntities)
    {
        if (options.importAsNewDocument)
        {
            APR_DEBUG("[ImportService] Clearing 3D scene before import");
            m_sceneManager3D->clearScene();
        }

#if BUILD_UI3D
        // 入场前图元数：撞号/非法 id 会被场景拒绝，用差值回填真实入场数
        const size_t entityCountBefore = m_sceneManager3D->entityCount();
        if (m_sceneEditService3D)
        {
            // 可撤销路径：整批只产生一条撤销记录
            APR_DEBUGF("[ImportService] Adding %zu mesh entity(ies) via SceneEditService3D (undoable)",
                meshEntities.size());
            m_sceneEditService3D->addEntities(std::move(meshEntities), "Import 3D mesh");
        }
        else
        {
            // 直通路径：直接入场、不入撤销栈、保留文件 id（与 2D 的 CommitMode::Direct 同语义）。
            // SceneManager3D 的结构写已降为 private，*Direct 是唯一授权的直通入口，
            // 本文件在 CMake/SceneWriteSurface.cmake 的 *Direct 白名单内。
            // 被拒绝的图元由场景就地销毁，所有权随之交出，调用方不再持有。
            APR_DEBUGF("[ImportService] SceneEditService3D not set, adding %zu mesh entity(ies) without undo",
                meshEntities.size());
            std::vector<Eg::AprMeshEntity*> rawMeshes;
            rawMeshes.reserve(meshEntities.size());
            for (auto& mesh : meshEntities)
            {
                rawMeshes.push_back(mesh.release());
            }
            SceneEditService3D::addEntitiesDirect(
                m_sceneManager3D, rawMeshes.data(), static_cast<int>(rawMeshes.size()));
        }
        meshAdded = static_cast<int>(m_sceneManager3D->entityCount() - entityCountBefore);
#else
        // 无 UI3D 构建没有授权的 3D 结构写入口，网格只能丢弃
        APR_ERRORF("[ImportService] BUILD_UI3D=0: no authorized 3D scene write entry, %zu mesh entity(ies) dropped",
            meshEntities.size());
        meshEntities.clear();
#endif

        m_sceneManager3D->markDataChanged();
        APR_DEBUGF("[ImportService] %d mesh entity(ies) added to SceneManager3D", meshAdded);
    }

    // ===== 3.3 其余图元落地到 2D 场景 =====
    int flatAdded = 0;
    // 转换期 ID → 入库后最终 ID 的修正表（见下方 addEntities 处的说明）
    std::unordered_map<int64_t, int64_t> idRemap;
    if (!flatEntities.empty())
    {
        // 大批量导入前按图元数预留几何仓容量：写入过程中 GPU 顶点仓不再反复 grow-拷贝
        // （顶点仓从小容量起步倍增扩容会放大海量导入的主线程停顿）
        if (m_refreshCoordinator)
        {
            m_refreshCoordinator->prepareForBulkAdd(flatEntities.size());
        }

        flatAdded = static_cast<int>(flatEntities.size());
        if (m_editService)
        {
            APR_DEBUGF("[ImportService] Adding %d entity(ies) via SceneEditService (direct, no undo)", flatAdded);

            // 入库前记下当前 ID：addEntities 在「ID 为 0 / 临时 ID / 与场景已有图元冲突」时
            // 会给图元换一个新持久 ID，而 parseResult 里的 entityLayerMap / entityGroupMap
            // 是以转换期的 ID 为键的。不修正就会整批查不到图元，表现为「导入后全在默认图层」。
            std::vector<Eg::EntityId> preInsertIds;
            preInsertIds.reserve(flatEntities.size());
            for (const auto& entity : flatEntities)
            {
                preInsertIds.push_back(entity ? entity->id : 0);
            }

            // 落库段细粒度进度：映射到 BuildDocument 阶段的 0.55~1.0，并在回调里节流泵送
            // 事件循环。十万级图元的入场/建索引会长时间占满主线程，不泵事件的话窗口模态
            // 进度对话框会冻住；这里与 AlgorithmApplicationService 的 apply 阶段同一手法。
            // 无进度消费者时（同步导入）progressCallback 为空，只做空转，不改变原行为。
            const auto commitProgress = [&context](double p) {
                if (!context.progressCallback)
                {
                    return;  // 同步导入无进度消费者：不泵事件，保持原有行为
                }
                const float mapped = 0.55f + 0.45f * static_cast<float>(std::clamp(p, 0.0, 1.0));
                context.progressCallback(ImportPhase::BuildDocument, mapped);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            };
            // 导入操作使用 CommitMode::Direct：用户几乎不会撤销一次"打开/导入文件"操作，
            // 走 Direct 可省去 Undo 命令对象的构建开销和额外的内存占用（29 万图元场景下显著）。
            const std::vector<Eg::EntityId> finalIds = m_editService->addEntities(std::move(flatEntities),
                "Import " + context.sourcePath.toStdString(),
                CommitMode::Direct,
                commitProgress);

            // 下标一一对应：addEntities 按入参顺序为每个非空图元返回最终 ID
            const size_t mapped = std::min(preInsertIds.size(), finalIds.size());
            for (size_t i = 0; i < mapped; ++i)
            {
                if (preInsertIds[i] != finalIds[i])
                {
                    idRemap[static_cast<int64_t>(preInsertIds[i])] = static_cast<int64_t>(finalIds[i]);
                }
            }
            if (!idRemap.empty())
            {
                APR_DEBUGF(
                    "[ImportService] %zu entity id(s) were reassigned on insert, remapped for layer/group restore",
                    idRemap.size());
            }
        }
        else if (m_sceneManager)
        {
            // 回退：无 SceneEditService 时直写 SceneManager，无 Undo 支持
            APR_WARNF("[ImportService] SceneEditService not set, adding %d entity(ies) without undo support", flatAdded);
            SceneEditService directEdit(m_sceneManager, nullptr);
            for (auto& entity : flatEntities)
            {
                if (entity)
                {
                    directEdit.addEntity(std::move(entity), "Import", CommitMode::Direct);
                }
            }
        }
        else
        {
            APR_ERRORF("[ImportService] No 2D scene available, %d entity(ies) cannot be imported", flatAdded);
            return ImportResult::fail(tr("No scene manager available"), ImportErrorType::Unknown);
        }
        APR_DEBUGF("[ImportService] %d entity(ies) added to the 2D scene", flatAdded);
    }

    const int entityCount = meshAdded + flatAdded;
    if (entityCount == 0)
    {
        APR_ERROR("[ImportService] No entity landed in any scene");
        return ImportResult::fail(tr("No scene manager available"), ImportErrorType::Unknown);
    }

    // ===== 3.4 还原源文件的图层与群组结构 =====
    // 只对 2D 通道有意义：3D 网格不参与图层体系与 AprGroup 树。
    // 时序约束：必须在图元进入场景、拿到运行时 EntityId 之后，才能按映射表反查归属。
    // 图层/工艺还原阶段的告警必须在最终结果里露出来：
    // ProcessData 找不到目标图层属于「参数没生效」，不能只写日志。
    QStringList layerRestoreWarnings;
    if (flatAdded > 0)
    {
        const int createdLayers = restoreImportedLayers(
            context, parseResult, idRemap, &layerRestoreWarnings);
        APR_DEBUGF("[ImportService] Layer restore: %d created from %lld source layer(s), %lld entity mapping(s)",
            createdLayers,
            static_cast<long long>(parseResult.importedLayers.size()),
            static_cast<long long>(parseResult.entityLayerMap.size()));

        const int createdGroups = restoreImportedGroups(parseResult, idRemap);
        APR_DEBUGF("[ImportService] Group restore: %d created from %lld source group(s), %lld entity mapping(s)",
            createdGroups,
            static_cast<long long>(parseResult.importedGroups.size()),
            static_cast<long long>(parseResult.entityGroupMap.size()));
    }
    else if (!parseResult.importedLayers.empty() || !parseResult.importedGroups.empty())
    {
        APR_WARNF("[ImportService] Skipping layer/group restore: no 2D entity imported (%zu layer(s), %zu group(s) "
                 "carried by IR are dropped)",
            parseResult.importedLayers.size(),
            parseResult.importedGroups.size());
    }

    APR_DEBUGF("[ImportService] Document built: %d entities (%d mesh + %d flat)", entityCount, meshAdded, flatAdded);
    updateProgress(context, ImportPhase::BuildDocument, 1.0f);

    // 载入的图元**保留文件里的 id**，而 EntityIdGenerator 是进程级计数器且重启后从 1 开始。
    // 不同步的话，打开文档后新画的图元会拿到文件里已被占用的 id，被 insertEntityPreserveId
    // 静默拒绝（症状：打开文件后画的东西不出现）。这里把生成器推到两侧场景的最大 id 之后。
    syncIdGeneratorWithScenes();

    // 根据图元类型设置目标工作台 ID
    auto result = ImportResult::ok(tr("Imported %1 entities successfully").arg(entityCount), entityCount);
    result.usedWorkbenchId = hasMeshEntities ? QStringLiteral("3D") : QStringLiteral("2D");
    // phaseParse 的告警（例如旧 .apr 只有图层 Profile 绑定、没有值集）必须带进
    // 最终结果，否则在构建阶段被整个吞掉，状态栏只显示「导入成功」。
    for (const auto& warn : parseResult.warnings)
    {
        result.addWarning(warn);
    }
    for (const auto& warn : layerRestoreWarnings)
    {
        result.addWarning(warn);
    }
    return result;
}
// ===== 载入后同步 id 生成器 =====
void ImportService::syncIdGeneratorWithScenes()
{
    Eg::EntityId maxSeen = 0;

    // 无捕获 lambda 可直接转成 Eg::EntityIdVisitor（bool (*)(EntityId, void*)）
    auto collectMax = [](Eg::EntityId id, void* ctx) -> bool {
        auto* maxValue = static_cast<Eg::EntityId*>(ctx);
        if (id > *maxValue)
        {
            *maxValue = id;
        }
        return true;
    };

    if (m_sceneManager)
    {
        m_sceneManager->forEachEntityId(collectMax, &maxSeen);
    }
    if (m_sceneManager3D)
    {
        m_sceneManager3D->forEachEntityId(collectMax, &maxSeen);
    }

    if (maxSeen <= 0)
    {
        return;
    }

    // setMaxId 内部是 CAS 推进：只在当前值 ≤ maxSeen 时才抬高，重复调用是幂等的
    Eg::EntityIdGenerator::instance().setMaxId(maxSeen);
    APR_DEBUGF("[ImportService] id generator advanced past loaded entities: maxId=%lld",
        static_cast<long long>(maxSeen));
}

// ===== 4：刷新显示 =====
void ImportService::phaseRefreshDisplay(
    const ImportContext& context, const ImportResult& result, const ImportOptions& options)
{
    updateProgress(context, ImportPhase::RefreshDisplay, 0.0f);
    emit importPhaseChanged(ImportPhase::RefreshDisplay);

    // Refresh display phase

    // 工作台切换
    if (options.autoSwitchWorkbench)
    {
        const QString targetId = result.usedWorkbenchId.isEmpty() ? QStringLiteral("2D") : result.usedWorkbenchId;
        if (m_workbenchSwitchCallback)
        {
            APR_DEBUGF("[ImportService] Switching workbench to '%s'", targetId.toUtf8().constData());
            m_workbenchSwitchCallback(targetId);
        }
        else
        {
            APR_TRACEF("[ImportService] Workbench switch to '%s' skipped: no callback registered",
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
            APR_TRACE("[ImportService] Viewport fit skipped: no callback registered");
        }
    }

    // 统一显示刷新（优先使用批量回调，避免多次分离刷新）
    if (m_displayRefreshCallback)
    {
        m_displayRefreshCallback();
    }
    else
    {
        // 兼容旧回调：分别调用树刷新和属性刷新
        // 树结构刷新
        if (m_treeRebuildCallback)
        {
            m_treeRebuildCallback();
        }
        else
        {
            APR_TRACE("[ImportService] Scene tree rebuild skipped: no callback registered");
        }

        // 属性面板刷新
        if (m_propertyRefreshCallback)
        {
            m_propertyRefreshCallback();
        }
        else
        {
            APR_TRACE("[ImportService] Property panel refresh skipped: no callback registered");
        }
    }

    updateProgress(context, ImportPhase::RefreshDisplay, 1.0f);
}

// ===== Write back state =====
void ImportService::phaseWriteBackState(const ImportContext& context, ImportResult& result)
{
    updateProgress(context, ImportPhase::WriteBackState, 0.0f);
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
        APR_INFO("[ImportService] Current document path not updated: no callback registered");
    }

    // 添加到最近文件（优先使用 context 中的回调）。
    // 工作台归属取自构建阶段判定的 result.usedWorkbenchId：
    // 2D 菜单里导入的 STEP 等若载入为网格图元，实际发生在 3D，应记入 3D 的最近列表。
    const QString workbenchId = result.usedWorkbenchId.isEmpty() ? QStringLiteral("2D") : result.usedWorkbenchId;
    if (context.recentFileAddCallback)
    {
        context.recentFileAddCallback(context.sourcePath, workbenchId);
    }
    else if (m_recentFileAddCallback)
    {
        m_recentFileAddCallback(context.sourcePath, workbenchId);
    }
    else
    {
        APR_INFO("[ImportService] Recent file list not updated: no callback registered");
    }

    // 文档持久化（使用成员变量回调，全局配置）。
    // 工作台归属与最近文件同源：result.usedWorkbenchId（STEP 等载入为网格时归 3D）。
    if (m_documentPersistenceCallback)
    {
        if (!m_documentPersistenceCallback(context.sourcePath, result.entityCount, workbenchId))
        {
            // 文件已导入成功，但历史记录写入失败：不能只报“导入完成”
            result.addWarning(QCoreApplication::translate("WorkbenchWindow", "History save failed"));
        }
    }
    else
    {
        APR_TRACE("[ImportService] Document persistence skipped: no callback registered");
    }

    // 更新状态栏（使用成员变量回调，全局配置）
    if (m_statusPromptCallback)
    {
        QString statusMsg =
            tr("Imported %1 entities from %2").arg(result.entityCount).arg(QFileInfo(context.sourcePath).fileName());
        if (!result.warnings.isEmpty())
        {
            statusMsg += QStringLiteral(" (") + result.warnings.join(QStringLiteral("; ")) + QStringLiteral(")");
        }
        m_statusPromptCallback(statusMsg);
    }

    updateProgress(context, ImportPhase::WriteBackState, 1.0f);
}

int ImportService::restoreImportedLayers(
    const ImportContext& context, const ImportResult& parseResult, const std::unordered_map<int64_t, int64_t>& idRemap,
    QStringList* outWarnings)
{
    if (parseResult.importedLayers.empty())
    {
        // 该格式本身不带图层信息（OBJ / STL / PLT 等），无需还原
        return 0;
    }
    if (!m_layerManager)
    {
        APR_WARNF("[ImportService] Layer restore skipped: LayerManager not set, %zu source layer(s) dropped",
            parseResult.importedLayers.size());
        return 0;
    }
    if (!context.preserveLayers)
    {
        APR_DEBUGF("[ImportService] Layer restore skipped: preserveLayers=false, %lld source layer(s) ignored",
            static_cast<long long>(parseResult.importedLayers.size()));
        return 0;
    }

    // 查找某源图层的加工语义（只有原生 .apr 有这个概念）
    const auto findMachining = [&parseResult](uint32_t sourceId) -> const ImportResult::LayerMachining* {
        for (const auto& m : parseResult.importedLayerMachining)
        {
            if (m.sourceId == sourceId)
            {
                return &m;
            }
        }
        return nullptr;
    };

    // 值集必须**先于**图层绑定落地：setLayerHardwareProfileId 会校验 id 存在。
    // 返回值 false 时跳过绑定还原，避免把悬空 id 套到运行时 Profile 上串层。
    const bool bindingsRestorable = restoreNativeProfileValueSet(m_layerManager, parseResult);
    const bool isNativeSnapshot = parseResult.hasNativeMachiningSnapshot && bindingsRestorable;

    // 源图层 sourceId → 运行时 LayerManager 图层 ID
    std::unordered_map<uint32_t, int> sourceToLayerId;
    int createdCount = 0;
    int reusedCount = 0;
    // 逐个记住来源图层的落地结果，供下面按层输出日志（每层只输出一次）
    struct ResolvedSourceLayer
    {
        std::string sourceName;
        uint32_t sourceColor = 0;
        int runtimeLayerId = 0;
        const char* status = "reused";
    };

    std::vector<ResolvedSourceLayer> resolvedLayers;
    resolvedLayers.reserve(parseResult.importedLayers.size());

    // DXF 标准图层 "0" 直接对应内置默认图层（id 0），不按名再建一层
    const bool isDxf = (context.format == Fio::FileFormat::DXF);

    for (const auto& src : parseResult.importedLayers)
    {
        const std::string srcName(src.name);

        int layerId = (isDxf && srcName == "0") ? 0 : m_layerManager->findLayerByName(srcName);
        const char* status = "reused";

        if (layerId >= 0)
        {
            // 同名图层视为同一个工艺层：保留用户已有的颜色/可见性/锁定设置，
            // 不用源文件属性覆盖，避免重复导入污染既有图层。
            //
            // 但**带完整加工语义快照的 .apr 是例外**：那是一次「打开文档」，
            // 文件里的加工语义必须是权威的，否则重建的图层会沿用上一份文档的
            // 输出开关/工艺/Profile 绑定，参数看似存下来了实则没还原。
            // 显示属性仍按上面的规则保留，只覆盖加工语义。
            ++reusedCount;
            if (isNativeSnapshot)
            {
                const auto machining = findMachining(src.sourceId);
                if (machining)
                {
                    applyLayerMachining(m_layerManager, layerId, *machining, true);
                }
            }
        }
        else
        {
            // 严格按源图层名新建，并带上源图层的颜色/可见性/锁定。
            // 刻意不再按颜色复用：内置 8 个默认图层恰好覆盖黑/红/绿/蓝等常见色，
            // 按颜色去重会把源图层全部吸进默认图层，图层名永远建不出来。
            layerId = m_layerManager->createLayer(srcName);
            if (layerId >= 0)
            {
                ++createdCount;
                status = "created";
                const uint8_t r = static_cast<uint8_t>((src.color >> 16) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((src.color >> 8) & 0xFF);
                const uint8_t b = static_cast<uint8_t>(src.color & 0xFF);
                // 高 8 位为 alpha；历史文件 alpha 位为 0 时按不透明处理，
                // 否则旧工程加载后整层会变透明
                const uint8_t a = static_cast<uint8_t>((src.color >> 24) & 0xFF);
                const int alpha = (a == 0) ? 255 : static_cast<int>(a);
                m_layerManager->setLayerColor(layerId, Ut::Color::fromRGB255(r, g, b, alpha));
                m_layerManager->setLayerVisible(layerId, src.visible);
                m_layerManager->setLayerLocked(layerId, src.locked);
                // 加工语义：输出开关、默认工艺、硬件 Profile 绑定跟着图层一起还原。
                // 少了这一步，打开 .apr 后「这一层要加工 / 用哪组功率速度」的信息
                // 就丢了 —— 表现是图纸明明设了输出，机器却不烧这一层，或者用
                // 默认参数把工件打坏。数据走独立 machining 通道（IrLayerInfo
                // 是冻结的跨 DLL POD，装不下这些字段）。
                const auto machining = findMachining(src.sourceId);
                if (machining)
                {
                    applyLayerMachining(m_layerManager, layerId, *machining, isNativeSnapshot);
                    applyLayerDisplayExtension(m_layerManager, layerId, *machining);
                }
            }
            else
            {
                // 图层数量已达上限：回退到内置默认图层，图元不会因此丢失
                APR_WARNF("[ImportService] Layer count limit reached, falling back to default layer for '%s'",
                    srcName.c_str());
                layerId = 0;
                status = "fell back to default (layer limit)";
            }
        }

        sourceToLayerId[src.sourceId] = layerId;
        resolvedLayers.push_back(ResolvedSourceLayer{ srcName, src.color, layerId, status });
    }

    // 图元归属只认解析层产出的 entityLayerMap（实体运行时 ID → 源图层 sourceId），
    // 键全部来自本次导入，因此不会误改场景里已有的其他图元；映射缺失的实体留在默认层。
    std::unordered_map<int64_t, int> runtimeLayerMap;
    runtimeLayerMap.reserve(parseResult.entityLayerMap.size());
    int unresolvedCount = 0;
    for (const auto& [entityId, sourceLayerId] : parseResult.entityLayerMap)
    {
        const auto it = sourceToLayerId.find(sourceLayerId);
        if (it == sourceToLayerId.end())
        {
            ++unresolvedCount;
            continue;
        }
        // 入库时被换过 ID 的图元要按修正表换成最终 ID，否则这里查不到它
        const auto remapIt = idRemap.find(entityId);
        runtimeLayerMap[remapIt != idRemap.end() ? remapIt->second : entityId] = it->second;
    }

    if (!runtimeLayerMap.empty())
    {
        m_layerManager->applyEntityLayerMap(runtimeLayerMap);
    }

    if (unresolvedCount > 0)
    {
        APR_WARNF("[ImportService] %d imported entit(y/ies) reference unknown source layer(s), left on default layer",
            unresolvedCount);
    }

    // 源文件带了图层就逐层记录一行、每层只记一次。
    // 汇总数字看不出「某层是被新建、被复用、还是回退到了默认层」，也无法核对图层名与颜色，
    // 多图层文件排查分层问题（DXF / SVG 等）时这组日志是第一手依据。
    if (!resolvedLayers.empty())
    {
        // 每个运行时图层在**本次导入**中落了多少图元（不是场景里的总数，合并导入时才有区分）
        std::unordered_map<int, int> importedCountByLayer;
        for (const auto& entry : runtimeLayerMap)
        {
            ++importedCountByLayer[entry.second];
        }

        for (const auto& layer : resolvedLayers)
        {
            const auto countIt = importedCountByLayer.find(layer.runtimeLayerId);
            APR_INFOF(
                "[ImportService] Source layer '%s' #%06X -> runtime id=%d ('%s') %s, %d entity(ies) in this import",
                layer.sourceName.c_str(),
                static_cast<unsigned>(layer.sourceColor & 0x00FFFFFFu),
                layer.runtimeLayerId,
                m_layerManager->layerName(layer.runtimeLayerId).c_str(),
                layer.status,
                countIt != importedCountByLayer.end() ? countIt->second : 0);
        }
    }

    // 工艺对象回灌必须排在图层落地之后：ProcessData 的目标是文档图层 ID，
    // 而运行时图层 ID 可能因「按名复用」与文件里的不同，只能经 sourceToLayerId 换算。
    const int appliedProcesses = restoreNativeProcesses(m_layerManager, parseResult, sourceToLayerId, outWarnings);

    // 工艺细节模型（ProcessModelData）：按图层回灌到 LayerManager，供加工启动时读取
    // （穿孔等细节只存在于这个块里，ProcessData 只有基础参数）。
    // 模型里的 layerId 是文档 ID，必须经 sourceToLayerId 换成运行时 ID，
    // 否则「按名复用」的图层会查到别的层的细节。
    if (parseResult.hasProcessModel)
    {
        Eg::ProcessModel model = parseResult.processModel;
        std::vector<Eg::LayerProcessEntry> translated;
        translated.reserve(model.layers.size());
        for (const Eg::LayerProcessEntry& entry : model.layers)
        {
            const auto it = sourceToLayerId.find(static_cast<uint32_t>(entry.layerId));
            if (it == sourceToLayerId.end())
            {
                // 该层本次没有落地（被跳过/超限），不保留悬空细节
                continue;
            }
            Eg::LayerProcessEntry copy = entry;
            copy.layerId = static_cast<int32_t>(it->second);
            translated.push_back(std::move(copy));
        }
        model.layers = std::move(translated);
        m_layerManager->setProcessModel(std::move(model));
    }
    else
    {
        // 旧档案/外来格式没有模型块：清空，避免沿用上一份文档的细节
        m_layerManager->clearProcessModel();
    }

    APR_DEBUGF("[ImportService] Layer restore: %zu total, %d created, %d reused, %zu entity assignment(s), "
               "%d process assignment(s)",
        parseResult.importedLayers.size(),
        createdCount,
        reusedCount,
        runtimeLayerMap.size(),
        appliedProcesses);
    return createdCount;
}

int ImportService::restoreImportedGroups(
    const ImportResult& parseResult, const std::unordered_map<int64_t, int64_t>& idRemap)
{
    if (parseResult.importedGroups.empty())
    {
        // 该格式本身不带群组信息，或解析层未产出群组表
        return 0;
    }
    if (!m_sceneManager)
    {
        APR_WARNF("[ImportService] Group restore skipped: SceneManager not set, %zu source group(s) dropped",
            parseResult.importedGroups.size());
        return 0;
    }

    // 群组 id 不复用 IR 的 sourceId：IR 的 sourceId 是 1-based 密集编号，与运行时 EntityId
    // 空间会撞车，所以这里统一用 createGroup 分配新 id，再靠 sourceId → AprGroup* 映射表衔接。
    Eg::GroupManager& groupManager = m_sceneManager->groupManager();

    std::unordered_map<uint64_t, Eg::AprGroup*> sourceToGroup;
    sourceToGroup.reserve(parseResult.importedGroups.size());

    // 第一遍：只建群组，不接父子。先全建出来可以免除对拓扑排序的依赖。
    int createdCount = 0;
    for (const auto& src : parseResult.importedGroups)
    {
        Eg::AprGroup* group = groupManager.createGroup(src.name);
        if (!group)
        {
            APR_WARNF("[ImportService] Failed to create group for source group %llu ('%s')",
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
            APR_WARNF("[ImportService] Group %llu references missing parent %llu, keeping it top-level",
                static_cast<unsigned long long>(src.sourceId),
                static_cast<unsigned long long>(src.parentSourceId));
            continue;
        }
        if (parentIt->second->wouldCreateCycle(childIt->second))
        {
            APR_WARNF("[ImportService] Group %llu would create a cycle under parent %llu, keeping it top-level",
                static_cast<unsigned long long>(src.sourceId),
                static_cast<unsigned long long>(src.parentSourceId));
            continue;
        }
        parentIt->second->addSubGroup(childIt->second);
    }

    // 第三遍：把图元挂到所属群组。EntityId → 源群组 sourceId → 运行时 AprGroup*
    size_t assignedCount = 0;
    size_t missingEntities = 0;
    for (const auto& [entityId, sourceId] : parseResult.entityGroupMap)
    {
        auto groupIt = sourceToGroup.find(sourceId);
        if (groupIt == sourceToGroup.end())
        {
            APR_WARNF("[ImportService] Entity %lld references unknown source group %llu, leaving it ungrouped",
                static_cast<long long>(entityId),
                static_cast<unsigned long long>(sourceId));
            continue;
        }

        // 与图层归属同理：入库时被换过 ID 的图元要按修正表换成最终 ID
        const auto remapIt = idRemap.find(entityId);
        const int64_t runtimeEntityId = remapIt != idRemap.end() ? remapIt->second : entityId;

        Eg::AprEntity* entity = m_sceneManager->findEntityById(runtimeEntityId);
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
        APR_WARNF("[ImportService] %zu group member(s) not found in scene, skipped", missingEntities);
    }
    APR_DEBUGF("[ImportService] Restored %d/%lld source group(s), %lld member assignment(s)",
        createdCount,
        static_cast<long long>(parseResult.importedGroups.size()),
        static_cast<long long>(assignedCount));
    return createdCount;
}

void ImportService::updateProgress(const ImportContext& context, ImportPhase phase, float progress)
{
    // 阶段号只落 Debug 级：Info 级里已有每个 phase 的 START/END，这里避免重复噪音，
    // 但排查"卡在哪个阶段"时把日志级别降到 Debug 就能看到进度推进
    APR_DEBUGF("[ImportService] Progress: phase=%d, value=%.2f", static_cast<int>(phase), progress);

    // 转发给上下文的进度回调：这是后台线程唯一安全的进度出口（调用方通常写入
    // 线程安全的 ProgressTracker，再由 UI 定时器读取刷新进度条）。
    // 解析阶段在工作线程调用、落库阶段在主线程调用，两侧都会经过这里。
    if (context.progressCallback)
    {
        context.progressCallback(phase, progress);
    }

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