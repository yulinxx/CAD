/**
 * @file ProcessingJobService.cpp
 * @brief 加工作业服务实现
 *
 * 管理加工任务的执行和调度。
 */
#include "ProcessingJobService.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <atomic>

#include "DeviceHost.h"
#include "EmissionGuard.h"
#include "LaserRoute.h"
#include "Log/AprLogger.h"

#ifdef ENABLE_HARDWARE

    #include "Hardware/Device/IGalvoCard.h"
    #include "Hardware/Device/IMotionCard.h"
    #include "Hardware/Device/MotionPlan.h"

    #include "MotionPlanCompiler.h"

namespace ProcessingJobDetail
{
    /// 进度轮询周期。100ms 对进度条足够，也不会把 SDK 的状态查询打满。
    constexpr int kPollIntervalMs = 100;

    /**
     * @brief 主线程等待「下发完成」的预算。
     *
     * 30ms 足够瞬时下发的机型走完 load + start，又短到用户完全感觉不到卡顿。
     * 超过即判定该卡是阻塞式下发，转由工作线程驱动。
     */
    constexpr int kDispatchWaitMs = 30;

    /// 阻塞式下发时工作线程发布进度快照的周期
    constexpr int kSnapshotIntervalMs = 20;

    /**
     * @brief 运动卡 / 振镜卡的执行 API 收口。
     *
     * 刻意每次调用都从 DeviceHost 重新解析指针，而不是在 startJob 时缓存：
     * 加工中途设备可能被 stop()（急停后重连、档案重载），
     * 缓存下来的指针此刻已经指向已销毁的对象，下一次轮询就是崩溃。
     */
    struct PlanRunner
    {
        Hw::IMotionCard* motion = nullptr;
        Hw::IGalvoCard* galvo = nullptr;

        static PlanRunner resolve(DeviceHost* host)
        {
            PlanRunner r;
            if (!host || !host->isRunning())
            {
                return r;
            }
            // 复合机优先用振镜：出光与扫描的同步在振镜卡硬件里完成，
            // 换成运动卡 + IO 控光会掉到毫秒级，成型质量直接不同
            r.galvo = host->galvoCard();
            if (!r.galvo)
            {
                r.motion = host->motionCard();
            }
            return r;
        }

        bool valid() const
        {
            return motion != nullptr || galvo != nullptr;
        }

        const char* kindName() const
        {
            return galvo ? "galvo" : "motion";
        }

        Hw::HwResult load(const Hw::MotionPlanView& view) const
        {
            return galvo ? galvo->loadPlan(view) : motion->loadPlan(view);
        }

        Hw::HwResult start() const
        {
            return galvo ? galvo->startMark() : motion->startPlan();
        }

        Hw::HwResult pause() const
        {
            return galvo ? galvo->pauseMark() : motion->pausePlan();
        }

        Hw::HwResult resume() const
        {
            return galvo ? galvo->resumeMark() : motion->resumePlan();
        }

        Hw::HwResult abort() const
        {
            return galvo ? galvo->abortMark() : motion->abortPlan();
        }

        Hw::PlanProgress progress() const
        {
            return galvo ? galvo->markProgress() : motion->planProgress();
        }
    };

    /// HwResult → 可展示的错误串（源串英文，经 tr 本地化）。
    QString describe(const QString& what, const Hw::HwResult& result)
    {
        return ProcessingJobService::tr("%1 failed: [%2] %3")
            .arg(what)
            .arg(QString::fromUtf8(Hw::hwErrorName(result.error)))
            .arg(QString::fromUtf8(result.message));
    }

    bool isTerminalState(Hw::PlanState state)
    {
        return state == Hw::PlanState::Completed || state == Hw::PlanState::Aborted
            || state == Hw::PlanState::Error;
    }

    /**
     * @brief 下发线程。
     *
     * ==================== 为什么必须有它 ====================
     *
     * 金橙子 MarkEzd 的 startMark 是同步阻塞循环：每条 markTo 都等振镜走完，
     * 一次打标几十分钟非常正常。而 startPlan 是被命令 lambda 在 UI 线程里
     * 同步调用的 —— 于是整个加工期间窗口无响应，暂停/停止按钮根本点不动，
     * 循环里也没有任何取消检查点。
     *
     * ==================== 主线程如何不被打住 ====================
     *
     * 主线程只在一开始等一个很短的预算（kDispatchWaitMs）：
     *   - 等到了：这块卡的下发是瞬时的（运动卡、Scanlab、模拟设备），
     *     主线程按原路径自己轮询进度，语义与改造前完全一致；
     *   - 没等到：卡已经陷在阻塞调用里，主线程从此只读本线程发布的快照，
     *     不再碰设备指针，也就不会与工作线程并发调 SDK。
     */
    class JobDispatchThread : public QThread
    {
    public:
        JobDispatchThread(const PlanRunner& runner, const Hw::MotionPlanView& view)
            : m_runner(runner)
            , m_view(view)
        {
            setObjectName(QStringLiteral("ProcessingJobDispatch"));
        }

        QSemaphore& startSignal() { return m_startSignal; }

        bool startFinished() const { return m_startFinished.load(); }

        /// 主线程放弃等待，本线程继续驱动进度快照直到终态
        void enterSnapshotMode() { m_snapshotMode.store(true); }

        /// 停止请求。主线程置位，循环内检查
        void requestCancel() { m_cancel.store(true); }

        /// 下发结果（主线程 acquire 之后读，或 pollProgress 在快照模式下读）
        bool loadSucceeded() const;
        bool startSucceeded() const;
        QString errorText() const;

        /// 已发布的进度快照
        Hw::PlanProgress snapshot() const;

    protected:
        void run() override;

    private:
        void publish(const Hw::PlanProgress& p);
        void publishError(const QString& text, Hw::HwError error = Hw::HwError::Unknown);

        PlanRunner m_runner;
        Hw::MotionPlanView m_view;

        QSemaphore m_startSignal;
        std::atomic<bool> m_startFinished{ false };
        std::atomic<bool> m_loadOk{ false };
        std::atomic<bool> m_startOk{ false };
        std::atomic<bool> m_snapshotMode{ false };
        std::atomic<bool> m_cancel{ false };

        mutable QMutex m_resultMutex;
        QString m_errorText;
        Hw::HwError m_errorCode = Hw::HwError::Unknown;

        std::atomic<int> m_state{ static_cast<int>(Hw::PlanState::Idle) };
        std::atomic<size_t> m_index{ 0 };
        std::atomic<size_t> m_count{ 0 };
        std::atomic<double> m_fraction{ 0.0 };
    };

    bool JobDispatchThread::loadSucceeded() const
    {
        return m_loadOk.load();
    }

    bool JobDispatchThread::startSucceeded() const
    {
        return m_startOk.load();
    }

    QString JobDispatchThread::errorText() const
    {
        QMutexLocker lock(&m_resultMutex);
        return m_errorText;
    }

    Hw::PlanProgress JobDispatchThread::snapshot() const
    {
        Hw::PlanProgress p{};
        p.state = static_cast<Hw::PlanState>(m_state.load());
        p.commandIndex = m_index.load();
        p.commandCount = m_count.load();
        p.fraction = m_fraction.load();
        p.error = m_errorCode;
        return p;
    }

    void JobDispatchThread::publishError(const QString& text, Hw::HwError error)
    {
        QMutexLocker lock(&m_resultMutex);
        m_errorText = text;
        m_errorCode = error;
        m_state.store(static_cast<int>(Hw::PlanState::Error));
    }

    void JobDispatchThread::publish(const Hw::PlanProgress& p)
    {
        m_state.store(static_cast<int>(p.state));
        m_index.store(p.commandIndex);
        m_count.store(p.commandCount);
        m_fraction.store(p.fraction);
        if (p.state == Hw::PlanState::Error)
        {
            m_errorCode = p.error;
        }
    }

    void JobDispatchThread::run()
    {
        const Hw::HwResult loaded = m_runner.load(m_view);
        if (loaded.failed())
        {
            publishError(describe(ProcessingJobService::tr("Load processing plan"), loaded), loaded.error);
            m_startFinished.store(true);
            m_startSignal.release();
            return;
        }
        m_loadOk.store(true);

        // 下发前的取消检查点：排队期间用户已经点了停止
        if (m_cancel.load())
        {
            publishError(ProcessingJobService::tr("Job cancelled before dispatch"), Hw::HwError::EmergencyStop);
            m_startFinished.store(true);
            m_startSignal.release();
            return;
        }

        const Hw::HwResult started = m_runner.start();  // 阻塞型适配器会在这里停很久
        if (started.failed())
        {
            publishError(describe(ProcessingJobService::tr("Start processing"), started), started.error);
            m_startFinished.store(true);
            m_startSignal.release();
            return;
        }
        m_startOk.store(true);
        m_startFinished.store(true);
        m_startSignal.release();

        // 主线程在预算内等到了：它接管轮询，本线程退出
        if (!m_snapshotMode.load())
        {
            return;
        }

        // 阻塞型下发：由本线程推进度快照，并处理停止请求
        while (true)
        {
            if (m_cancel.load())
            {
                m_cancel.store(false);
                m_runner.abort();
            }
            const Hw::PlanProgress p = m_runner.progress();
            publish(p);
            if (isTerminalState(p.state))
            {
                break;
            }
            msleep(kSnapshotIntervalMs);
        }
    }
}  // namespace ProcessingJobDetail

using namespace ProcessingJobDetail;

struct ProcessingJobService::Impl
{
    DeviceHost* host = nullptr;

    /**
     * @brief 计划的持有者。
     *
     * 必须是成员而不是 startJob 的局部变量：MotionPlanView 只是指针 + 数量，
     * 虽然 loadPlan 约定「实现自行复制」，但把 builder 留着还有两个实际用处 ——
     * 失败重试时不用重编译，以及排查时能把计划导出成文本。
     */
    Hw::MotionPlanBuilder builder;

    QTimer pollTimer;

    QString jobId;
    ToolpathCompileResult lastResult;

    /// 是否有一份作业已下发且尚未结束（Completed / Aborted / Error 之前）
    bool active = false;

    /**
     * @brief 下发线程。为空表示没有阻塞式下发在跑。
     *
     * 主线程在快照模式下只读它的 snapshot()，不碰设备指针。
     */
    std::unique_ptr<JobDispatchThread> worker;

    /// 主线程是否已切换到「只读快照」模式
    bool snapshotMode = false;

    /// 最近一次下发所在线程名，用于诊断
    QString dispatchThread;

    Hw::PlanState lastState = Hw::PlanState::Idle;
    size_t lastIndex = 0;
    double lastFraction = 0.0;

    /// 收尾：停掉轮询、释放工作线程（必要时 join）
    void finishJob()
    {
        active = false;
        snapshotMode = false;
        pollTimer.stop();
        if (worker)
        {
            worker->requestCancel();
            if (!worker->wait(2000))
            {
                // 到点还没退出说明适配器卡在 SDK 调用里：
                // 只能等，绝不能 detach —— 线程会拿着即将销毁的设备继续跑
                APR_ERROR("[ProcessingJob] Dispatch thread did not stop in 2s, waiting for it");
                worker->wait();
            }
            worker.reset();
        }
    }

    /// 开工前的统一门控。通过返回 true，否则 errorOut 已填好可展示原因。
    bool checkGate(QString& errorOut) const
    {
        if (!host)
        {
            errorOut = tr("Device host not configured");  // 未装配设备宿主
            return false;
        }
        if (active)
        {
            errorOut = tr("Job already in progress (%1), stop it first").arg(jobId);  // 已有加工作业正在进行
            return false;
        }
        if (!host->isRunning())
        {
            errorOut = tr("Device not started, cannot start processing");  // 设备未启动，无法开始加工
            return false;
        }
        if (!host->canStartProcessing())
        {
            const QStringList violations = host->safetyViolations();
            errorOut = violations.isEmpty()
                ? tr("Safety conditions not met, processing prohibited")  // 安全条件不满足，禁止开始加工
                : tr("Safety conditions not met, processing prohibited: %1").arg(violations.first());
            return false;
        }
        return true;
    }
};

ProcessingJobService::ProcessingJobService(DeviceHost* host, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->host = host;
    m_impl->pollTimer.setInterval(kPollIntervalMs);
    connect(&m_impl->pollTimer, &QTimer::timeout, this, &ProcessingJobService::pollProgress);

    if (host)
    {
        connect(host, &DeviceHost::safetyVerdictChanged, this, &ProcessingJobService::onSafetyVerdictChanged);
        connect(host, &DeviceHost::deviceStopping, this, &ProcessingJobService::onDeviceStopping);
    }
    else
    {
        APR_ERRORF("[ProcessingJob] Constructed without a DeviceHost — all jobs will be refused");
    }
}

ProcessingJobService::~ProcessingJobService()
{
    if (m_impl->active)
    {
        // 析构时还在加工：不能就这么走。设备侧的缓冲还在跑，
        // 进程退出后没人再收进度，机器会把整份计划走完 —— 无人看管的出光。
        APR_WARNF("[ProcessingJob] Destroyed while job '%s' was active, aborting it", m_impl->jobId.toUtf8().constData());
        m_impl->host->emissionGuard().abort(QStringLiteral("serviceDestroyed"));
        const PlanRunner runner = PlanRunner::resolve(m_impl->host);
        if (runner.valid())
        {
            runner.abort();
        }
        // 工作线程必须在析构完成前退出：线程里持有 host 与设备指针
        m_impl->finishJob();
    }
    m_impl->pollTimer.stop();
}

bool ProcessingJobService::startJob(
    const Eg::SceneManager& scene, const LayerManager* layers, const ToolpathJobSpec& spec, QString& errorOut)
{
    // 先门控再编译：十万条指令的编译要几百毫秒，
    // 门没关就先编译等于让操作员盯着卡顿的界面等一条「门没关」的提示
    if (!m_impl->checkGate(errorOut))
    {
        APR_WARNF("[ProcessingJob] startJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    m_impl->lastResult = MotionPlanCompiler::compile(scene, layers, spec, m_impl->builder);
    if (!m_impl->lastResult.ok)
    {
        errorOut = m_impl->lastResult.error;
        APR_ERRORF("[ProcessingJob] Compile failed: %s", errorOut.toUtf8().constData());
        return false;
    }

    return startPlan(m_impl->builder.view(), spec.jobId, errorOut);
}

bool ProcessingJobService::startPlan(const Hw::MotionPlanView& view, const QString& jobId, QString& errorOut)
{
    if (!m_impl->checkGate(errorOut))
    {
        APR_WARNF("[ProcessingJob] startPlan refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    if (!view.valid())
    {
        errorOut = tr("Processing plan is empty, no executable commands");  // 加工计划为空，没有任何可执行指令
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        // 例如只接了激光电源或纯 IO 板的机型
        errorOut = tr("Device %1 provides neither motion nor galvo capability, cannot execute plan")
                       .arg(m_impl->host->deviceId());  // 设备既不提供运动卡也不提供振镜卡能力
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    // === 激光参数路由 ===
    // 运动卡机型的功率/频率/脉宽只能靠宿主主动投递，否则会被静默丢弃
    LaserRoute::ApplyReport laserReport;
    const LaserRoute::Route laserRoute = LaserRoute::resolve(m_impl->host);
    if (!LaserRoute::applyFromPlan(laserRoute, view, laserReport, errorOut))
    {
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    if (!laserReport.warning.isEmpty())
    {
        // 部分生效必须让操作员看到，不能算成功
        emit jobWarning(laserReport.warning);
    }

    // === 出光使能 ===
    // 必须在下发之前打开：振镜卡的 startMark/startPlan 会直接拒绝未开光的调用
    if (!m_impl->host->emissionGuard().beginJob(errorOut))
    {
        APR_ERRORF("[ProcessingJob] Emission enable refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    std::unique_ptr<JobDispatchThread> worker = std::make_unique<JobDispatchThread>(runner, view);
    m_impl->dispatchThread = worker->objectName();
    worker->start();

    // 主线程只等一个很短的预算：等到了说明下发是瞬时的，等不到说明卡在阻塞调用里
    bool blocked = false;
    if (!worker->startSignal().tryAcquire(1, kDispatchWaitMs))
    {
        worker->enterSnapshotMode();
        blocked = !worker->startFinished();
    }

    if (!blocked)
    {
        worker->startSignal().tryAcquire(1);  // 消费掉可能刚到的那一次释放
        if (!worker->loadSucceeded() || !worker->startSucceeded())
        {
            errorOut = worker->errorText();
            worker->wait();
            // 下发没成功就必须关光：不能把激光留在使能状态等下一次操作
            m_impl->host->emissionGuard().endJob();
            APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
            return false;
        }
        worker->wait();  // start 已返回，线程正在退出
    }
    else
    {
        // 阻塞式下发（金橙子 MarkEzd 的同步打标循环）：
        // 设备从此归工作线程所有，主线程只读快照
        m_impl->worker = std::move(worker);
        m_impl->snapshotMode = true;
    }

    m_impl->jobId = jobId.isEmpty() ? QStringLiteral("job") : jobId;
    m_impl->active = true;
    m_impl->lastState = Hw::PlanState::Running;
    m_impl->lastIndex = 0;
    m_impl->lastFraction = 0.0;
    m_impl->pollTimer.start();

    APR_DEBUGF("[ProcessingJob] Started '%s' on %s: %lld command(s), emission=on, dispatch=%s, laserParams=%zu",
        m_impl->jobId.toUtf8().constData(),
        runner.kindName(),
        static_cast<long long>(view.commandCount),
        blocked ? "worker(blocking)" : "worker(instant)",
        laserReport.paramCount);

    emit jobStateChanged(
        static_cast<int>(Hw::PlanState::Running), QString::fromUtf8(Hw::planStateName(Hw::PlanState::Running)));
    emit jobStarted(m_impl->jobId, static_cast<int>(view.commandCount));
    return true;
}

bool ProcessingJobService::pauseJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = tr("No processing job is currently running");
        APR_WARNF("[ProcessingJob] pauseJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    if (!isPauseSupported())
    {
        // 明确拒绝而不是走适配器再失败：这类设备的暂停按钮应当直接置灰
        errorOut = tr("Current machine does not support pausing a running job");
        APR_WARNF("[ProcessingJob] pauseJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        errorOut = tr("Device disconnected");
        APR_WARNF("[ProcessingJob] pauseJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    const Hw::HwResult r = runner.pause();
    if (r.failed())
    {
        errorOut = describe(tr("Pause processing"), r);
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    APR_DEBUGF("[ProcessingJob] Paused '%s'", m_impl->jobId.toUtf8().constData());
    // 不在这里改 lastState：状态一律以设备上报为准，
    // 本地先改会让「调用成功但设备没真正暂停」变成看不见的谎
    pollProgress();
    return true;
}

bool ProcessingJobService::resumeJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = tr("No processing job is currently running");
        APR_WARN("[ProcessingJob] resumeJob refused: no active job");
        return false;
    }
    // 恢复也是「开始出光」，安全门必须重新过一遍：
    // 暂停期间操作员很可能开门取件了
    if (!m_impl->host->canStartProcessing())
    {
        const QStringList violations = m_impl->host->safetyViolations();
        errorOut = violations.isEmpty() ? tr("Safety conditions not met, resume prohibited")
                                        : tr("Safety conditions not met, resume prohibited: %1").arg(violations.first());
        APR_WARNF("[ProcessingJob] resumeJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    // 安全链关光后出光被闭锁：禁止自动恢复，必须由操作员显式确认
    EmissionGuard& emission = m_impl->host->emissionGuard();
    if (emission.state() == EmissionState::Latched)
    {
        errorOut = tr("Emission is latched off by the safety chain, explicit acknowledgement is required first");
        APR_WARNF("[ProcessingJob] resumeJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    if (emission.state() == EmissionState::Off && !emission.beginJob(errorOut))
    {
        APR_WARNF("[ProcessingJob] resumeJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        errorOut = tr("Device disconnected");
        APR_ERROR("[ProcessingJob] resumeJob failed: runner invalid (device disconnected)");
        return false;
    }
    const Hw::HwResult r = runner.resume();
    if (r.failed())
    {
        errorOut = describe(tr("Resume processing"), r);
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    APR_DEBUGF("[ProcessingJob] Resumed '%s'", m_impl->jobId.toUtf8().constData());
    pollProgress();
    return true;
}

bool ProcessingJobService::abortJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = tr("No processing job is currently running");
        APR_WARN("[ProcessingJob] abortJob refused: no active job");
        return false;
    }

    // 先关光再停机械：反过来的话，机械已经停了但激光还亮着的那几毫秒足够烧穿工件。
    // abort 不闭锁 —— 用户主动停止之后应当允许立刻重新开工。
    m_impl->host->emissionGuard().abort(QStringLiteral("userAbort"));
    if (m_impl->worker)
    {
        m_impl->worker->requestCancel();
    }

    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        // 设备都没了，本地状态必须收干净，否则永远显示「加工中」
        m_impl->finishJob();
        APR_ERROR("[ProcessingJob] abortJob: device disappeared during active job, cleaning up local state");
        emit jobFinished(false, tr("Device disconnected, job aborted"));
        errorOut = tr("Device disconnected");
        return false;
    }
    const Hw::HwResult r = runner.abort();
    if (r.failed())
    {
        errorOut = describe(tr("Abort processing"), r);
        APR_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    APR_WARNF("[ProcessingJob] Aborted '%s'", m_impl->jobId.toUtf8().constData());
    pollProgress();
    return true;
}

bool ProcessingJobService::isRunning() const
{
    return m_impl->active && m_impl->lastState == Hw::PlanState::Running;
}

bool ProcessingJobService::isPaused() const
{
    return m_impl->active && m_impl->lastState == Hw::PlanState::Paused;
}

bool ProcessingJobService::isPauseSupported() const
{
    return m_impl->host ? m_impl->host->supportsRuntimePause() : false;
}

bool ProcessingJobService::acknowledgeSafetyCutoff(QString& errorOut)
{
    if (!m_impl->host)
    {
        errorOut = tr("Device host not configured");
        return false;
    }
    if (!m_impl->host->acknowledgeEmissionLatch(errorOut))
    {
        return false;
    }
    APR_INFO("[ProcessingJob] Safety cutoff acknowledged by operator");
    return true;
}

QString ProcessingJobService::dispatchThreadName() const
{
    return m_impl->dispatchThread;
}

QString ProcessingJobService::currentJobId() const
{
    return m_impl->jobId;
}

ToolpathCompileResult ProcessingJobService::lastCompileResult() const
{
    return m_impl->lastResult;
}

double ProcessingJobService::progressFraction() const
{
    return m_impl->lastFraction;
}

int ProcessingJobService::planState() const
{
    return static_cast<int>(m_impl->lastState);
}

QString ProcessingJobService::planStateName() const
{
    return QString::fromUtf8(Hw::planStateName(m_impl->lastState));
}

void ProcessingJobService::pollProgress()
{
    if (!m_impl->active)
    {
        return;
    }

    Hw::PlanProgress p{};
    bool deviceDisappeared = false;

    if (m_impl->snapshotMode && m_impl->worker)
    {
        // 阻塞式下发：设备归工作线程所有，主线程只读快照。
        // 这里绝不能再调 runner.progress()：那等于和 SDK 抢同一个句柄。
        p = m_impl->worker->snapshot();
        // 工作线程还陷在 start() 里时快照仍是 Idle，但作业已经开始下发；
        // 这一段必须对外报 Running，否则界面会在按下开始后显示"空闲"。
        if (!m_impl->worker->startFinished() && p.state == Hw::PlanState::Idle)
        {
            p.state = Hw::PlanState::Running;
        }
    }
    else
    {
        const PlanRunner runner = PlanRunner::resolve(m_impl->host);
        if (!runner.valid())
        {
            deviceDisappeared = true;
        }
        else
        {
            p = runner.progress();
        }
    }

    if (deviceDisappeared)
    {
        APR_ERRORF("[ProcessingJob] Device disappeared while job '%s' was active", m_impl->jobId.toUtf8().constData());
        m_impl->host->emissionGuard().abort(QStringLiteral("deviceDisconnected"));
        m_impl->finishJob();
        emit jobFinished(false, tr("Device disconnected during processing"));
        return;
    }

    if (p.state != m_impl->lastState)
    {
        m_impl->lastState = p.state;
        emit jobStateChanged(static_cast<int>(p.state), QString::fromUtf8(Hw::planStateName(p.state)));
    }

    if (p.commandIndex != m_impl->lastIndex || p.fraction != m_impl->lastFraction)
    {
        m_impl->lastIndex = p.commandIndex;
        m_impl->lastFraction = p.fraction;
        emit jobProgress(p.fraction, static_cast<int>(p.commandIndex), static_cast<int>(p.commandCount));
    }

    if (!isTerminalState(p.state))
    {
        return;
    }

    // 终态：先关光，再收尾。关光必须在这里做，而不是等析构 ——
    // 作业已经结束但激光还使能着，等于把一把上了膛的枪放在桌上。
    m_impl->host->emissionGuard().endJob();
    m_impl->finishJob();

    switch (p.state)
    {
    case Hw::PlanState::Completed:
        APR_DEBUGF("[ProcessingJob] Finished '%s'", m_impl->jobId.toUtf8().constData());
        emit jobFinished(true, tr("Processing completed: %1").arg(m_impl->jobId));
        break;

    case Hw::PlanState::Aborted:
        emit jobFinished(false, tr("Processing aborted: %1").arg(m_impl->jobId));
        break;

    case Hw::PlanState::Error:
        APR_ERRORF("[ProcessingJob] Job '%s' failed: %s", m_impl->jobId.toUtf8().constData(), Hw::hwErrorName(p.error));
        emit jobFinished(false, tr("Processing error: [%1]").arg(QString::fromUtf8(Hw::hwErrorName(p.error))));
        break;

    default:
        break;
    }
}

void ProcessingJobService::onSafetyVerdictChanged(
    bool canStartProcessing, const QString& firstViolation, const QString& firstViolationPoint)
{
    Q_UNUSED(firstViolation);
    if (canStartProcessing || !m_impl->active)
    {
        return;
    }

    // 安全条件在加工中途被破坏（开门、踩安全光栅）。
    // 这里主动暂停，不依赖安全条件是否配了 pause 动作 ——
    // 「裁决已经不允许开工，却还在继续出光」在任何配置下都不可接受。
    // 同时闭锁出光：安全条件恢复后也不得自动恢复，必须由操作员显式确认。
    m_impl->host->emissionGuard().safetyCutoff(QStringLiteral("safetyVerdict:%1").arg(firstViolationPoint));

    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        return;
    }
    APR_ERRORF("[ProcessingJob] Safety violated during job '%s' (point=%s), pausing and latching emission off",
        m_impl->jobId.toUtf8().constData(),
        firstViolationPoint.toUtf8().constData());
    runner.pause();
    pollProgress();
}

void ProcessingJobService::onDeviceStopping()
{
    if (!m_impl->active)
    {
        return;
    }

    // 设备即将销毁：工作线程还在用设备指针的话，必须在这里把它停掉。
    // 这一步不能延后 —— destroyDevice 之后指针就悬空了。
    APR_WARNF("[ProcessingJob] Device stopping while job '%s' is active, aborting it",
        m_impl->jobId.toUtf8().constData());
    m_impl->host->emissionGuard().abort(QStringLiteral("deviceStopping"));
    m_impl->finishJob();
    emit jobFinished(false, tr("Device stopped, job aborted"));
}

#else  // !ENABLE_HARDWARE

/**
 * @brief 未编入硬件支持时的实现。
 *
 * 与 DeviceHost 同一条原则：不提供「假装成功」的空实现。
 * 加工命令一律失败并说明真正的原因是构建配置，
 * 否则界面会点亮「开始加工」，按下去毫无反应。
 */
struct ProcessingJobService::Impl
{
    DeviceHost* host = nullptr;
    QString jobId;
    ToolpathCompileResult lastResult;
};

namespace
{
    QString noHardwareReason()
    {
        return QStringLiteral(
            "Hardware module not enabled (BUILD_HARDWARE=OFF), cannot process");  // 本次构建未启用硬件模块（BUILD_HARDWARE=OFF），无法加工
    }
}  // namespace

ProcessingJobService::ProcessingJobService(DeviceHost* host, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->host = host;
}

ProcessingJobService::~ProcessingJobService() = default;

bool ProcessingJobService::startJob(
    const Eg::SceneManager& scene, const LayerManager* layers, const ToolpathJobSpec& spec, QString& errorOut)
{
    Q_UNUSED(scene);
    Q_UNUSED(layers);
    Q_UNUSED(spec);
    errorOut = noHardwareReason();
    APR_WARNF("[ProcessingJob] %s", errorOut.toUtf8().constData());
    return false;
}

bool ProcessingJobService::pauseJob(QString& errorOut)
{
    errorOut = noHardwareReason();
    return false;
}

bool ProcessingJobService::resumeJob(QString& errorOut)
{
    errorOut = noHardwareReason();
    return false;
}

bool ProcessingJobService::abortJob(QString& errorOut)
{
    errorOut = noHardwareReason();
    return false;
}

bool ProcessingJobService::isRunning() const
{
    return false;
}

bool ProcessingJobService::isPaused() const
{
    return false;
}

bool ProcessingJobService::isPauseSupported() const
{
    return false;
}

bool ProcessingJobService::acknowledgeSafetyCutoff(QString& errorOut)
{
    errorOut = noHardwareReason();
    return false;
}

QString ProcessingJobService::dispatchThreadName() const
{
    return QString();
}

QString ProcessingJobService::currentJobId() const
{
    return m_impl->jobId;
}

ToolpathCompileResult ProcessingJobService::lastCompileResult() const
{
    return m_impl->lastResult;
}

double ProcessingJobService::progressFraction() const
{
    return 0.0;
}

/// 0 == Hw::PlanState::Idle，保持与硬件版一致的数值语义。
int ProcessingJobService::planState() const
{
    return 0;
}

QString ProcessingJobService::planStateName() const
{
    return QStringLiteral("Idle");
}

void ProcessingJobService::pollProgress() {}

void ProcessingJobService::onSafetyVerdictChanged(
    bool canStartProcessing, const QString& firstViolation, const QString& firstViolationPoint)
{
    Q_UNUSED(canStartProcessing);
    Q_UNUSED(firstViolation);
    Q_UNUSED(firstViolationPoint);
}

void ProcessingJobService::onDeviceStopping() {}

#endif  // ENABLE_HARDWARE