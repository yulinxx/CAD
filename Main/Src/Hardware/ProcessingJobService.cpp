#include "ProcessingJobService.h"

#include <QTimer>

#include "DeviceHost.h"
#include "Log/SyLogger.h"

#ifdef ENABLE_HARDWARE

#include "Hardware/Device/IGalvoCard.h"
#include "Hardware/Device/IMotionCard.h"
#include "Hardware/Device/MotionPlan.h"

#include "MotionPlanCompiler.h"

namespace
{
    /// 进度轮询周期。100ms 对进度条足够，也不会把 SDK 的状态查询打满。
    constexpr int kPollIntervalMs = 100;

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

        bool valid() const { return motion != nullptr || galvo != nullptr; }

        const char* kindName() const { return galvo ? "galvo" : "motion"; }

        Hw::HwResult load(const Hw::MotionPlanView& view) const
        {
            return galvo ? galvo->loadPlan(view) : motion->loadPlan(view);
        }

        Hw::HwResult start() const { return galvo ? galvo->startMark() : motion->startPlan(); }
        Hw::HwResult pause() const { return galvo ? galvo->pauseMark() : motion->pausePlan(); }
        Hw::HwResult resume() const { return galvo ? galvo->resumeMark() : motion->resumePlan(); }
        Hw::HwResult abort() const { return galvo ? galvo->abortMark() : motion->abortPlan(); }

        Hw::PlanProgress progress() const
        {
            return galvo ? galvo->markProgress() : motion->planProgress();
        }
    };

    /// HwResult → 可展示的中文错误串。
    QString describe(const QString& what, const Hw::HwResult& result)
    {
        return QStringLiteral("%1失败：[%2] %3")
            .arg(what)
            .arg(QString::fromUtf8(Hw::hwErrorName(result.error)))
            .arg(QString::fromUtf8(result.message));
    }
}

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

    Hw::PlanState lastState = Hw::PlanState::Idle;
    size_t lastIndex = 0;
    double lastFraction = 0.0;


    /// 开工前的统一门控。通过返回 true，否则 errorOut 已填好可展示原因。
    bool checkGate(QString& errorOut) const
    {
        if (!host)
        {
            errorOut = QStringLiteral("未装配设备宿主");
            return false;
        }
        if (active)
        {
            errorOut = QStringLiteral("已有加工作业正在进行（%1），请先停止").arg(jobId);
            return false;
        }
        if (!host->isRunning())
        {
            errorOut = QStringLiteral("设备未启动，无法开始加工");
            return false;
        }
        if (!host->canStartProcessing())
        {
            const QStringList violations = host->safetyViolations();
            errorOut = violations.isEmpty()
                           ? QStringLiteral("安全条件不满足，禁止开始加工")
                           : QStringLiteral("安全条件不满足，禁止开始加工：%1").arg(violations.first());
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
        connect(host, &DeviceHost::safetyVerdictChanged,
            this, &ProcessingJobService::onSafetyVerdictChanged);
    }
    else
    {
        SY_ERRORF("[ProcessingJob] Constructed without a DeviceHost — all jobs will be refused");
    }
}

ProcessingJobService::~ProcessingJobService()
{
    if (m_impl->active)
    {
        // 析构时还在加工：不能就这么走。设备侧的缓冲还在跑，
        // 进程退出后没人再收进度，机器会把整份计划走完 —— 无人看管的出光。
        SY_WARNF("[ProcessingJob] Destroyed while job '%s' was active, aborting it",
            m_impl->jobId.toUtf8().constData());
        const PlanRunner runner = PlanRunner::resolve(m_impl->host);
        if (runner.valid())
        {
            runner.abort();
        }
    }
    m_impl->pollTimer.stop();
}

bool ProcessingJobService::startJob(const Eg::SceneManager& scene, const LayerManager* layers,
    const ToolpathJobSpec& spec, QString& errorOut)
{
    // 先门控再编译：十万条指令的编译要几百毫秒，
    // 门没关就先编译等于让操作员盯着卡顿的界面等一条「门没关」的提示
    if (!m_impl->checkGate(errorOut))
    {
        SY_WARNF("[ProcessingJob] startJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    m_impl->lastResult = MotionPlanCompiler::compile(scene, layers, spec, m_impl->builder);
    if (!m_impl->lastResult.ok)
    {
        errorOut = m_impl->lastResult.error;
        SY_ERRORF("[ProcessingJob] Compile failed: %s", errorOut.toUtf8().constData());
        return false;
    }

    return startPlan(m_impl->builder.view(), spec.jobId, errorOut);
}

bool ProcessingJobService::startPlan(const Hw::MotionPlanView& view, const QString& jobId,
    QString& errorOut)
{
    if (!m_impl->checkGate(errorOut))
    {
        SY_WARNF("[ProcessingJob] startPlan refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    if (!view.valid())
    {
        errorOut = QStringLiteral("加工计划为空，没有任何可执行指令");
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        // 例如只接了激光电源或纯 IO 板的机型
        errorOut = QStringLiteral("设备 %1 既不提供运动卡也不提供振镜卡能力，无法执行加工计划")
                       .arg(m_impl->host->deviceId());
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    const Hw::HwResult loaded = runner.load(view);
    if (loaded.failed())
    {
        // loadPlan 里通常是软限位/幅面预检失败，错误信息带具体越界轴，直接透出
        errorOut = describe(QStringLiteral("下发加工计划"), loaded);
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    const Hw::HwResult started = runner.start();
    if (started.failed())
    {
        errorOut = describe(QStringLiteral("启动加工"), started);
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }

    m_impl->jobId = jobId.isEmpty() ? QStringLiteral("job") : jobId;
    m_impl->active = true;
    m_impl->lastState = Hw::PlanState::Running;
    m_impl->lastIndex = 0;
    m_impl->lastFraction = 0.0;
    m_impl->pollTimer.start();


    SY_INFOF("[ProcessingJob] Started '%s' on %s: %zu command(s), bounds [%.3f %.3f]-[%.3f %.3f]",
        m_impl->jobId.toUtf8().constData(), runner.kindName(), view.commandCount,
        view.header.boundsMinX, view.header.boundsMinY,
        view.header.boundsMaxX, view.header.boundsMaxY);

    emit jobStateChanged(static_cast<int>(Hw::PlanState::Running),
        QString::fromUtf8(Hw::planStateName(Hw::PlanState::Running)));
    emit jobStarted(m_impl->jobId, static_cast<int>(view.commandCount));
    return true;
}

bool ProcessingJobService::pauseJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = QStringLiteral("当前没有正在进行的加工作业");
        return false;
    }
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        errorOut = QStringLiteral("设备已断开");
        return false;
    }
    const Hw::HwResult r = runner.pause();
    if (r.failed())
    {
        errorOut = describe(QStringLiteral("暂停加工"), r);
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    SY_INFOF("[ProcessingJob] Paused '%s'", m_impl->jobId.toUtf8().constData());
    // 不在这里改 lastState：状态一律以设备上报为准，
    // 本地先改会让「调用成功但设备没真正暂停」变成看不见的谎
    pollProgress();
    return true;
}

bool ProcessingJobService::resumeJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = QStringLiteral("当前没有正在进行的加工作业");
        return false;
    }
    // 恢复也是「开始出光」，安全门必须重新过一遍：
    // 暂停期间操作员很可能开门取件了
    if (!m_impl->host->canStartProcessing())
    {
        const QStringList violations = m_impl->host->safetyViolations();
        errorOut = violations.isEmpty()
                       ? QStringLiteral("安全条件不满足，禁止恢复加工")
                       : QStringLiteral("安全条件不满足，禁止恢复加工：%1").arg(violations.first());
        SY_WARNF("[ProcessingJob] resumeJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        errorOut = QStringLiteral("设备已断开");
        return false;
    }
    const Hw::HwResult r = runner.resume();
    if (r.failed())
    {
        errorOut = describe(QStringLiteral("恢复加工"), r);
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    SY_INFOF("[ProcessingJob] Resumed '%s'", m_impl->jobId.toUtf8().constData());
    pollProgress();
    return true;
}

bool ProcessingJobService::abortJob(QString& errorOut)
{
    if (!m_impl->active)
    {
        errorOut = QStringLiteral("当前没有正在进行的加工作业");
        return false;
    }
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        // 设备都没了，本地状态必须收干净，否则永远显示「加工中」
        m_impl->active = false;
        m_impl->pollTimer.stop();
        emit jobFinished(false, QStringLiteral("设备已断开，作业中止"));
        errorOut = QStringLiteral("设备已断开");
        return false;
    }
    const Hw::HwResult r = runner.abort();
    if (r.failed())
    {
        errorOut = describe(QStringLiteral("中止加工"), r);
        SY_ERRORF("[ProcessingJob] %s", errorOut.toUtf8().constData());
        return false;
    }
    SY_WARNF("[ProcessingJob] Aborted '%s'", m_impl->jobId.toUtf8().constData());
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

    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        SY_ERRORF("[ProcessingJob] Device disappeared while job '%s' was active",
            m_impl->jobId.toUtf8().constData());
        m_impl->active = false;
        m_impl->pollTimer.stop();
        emit jobFinished(false, QStringLiteral("设备在加工过程中断开"));
        return;
    }

    const Hw::PlanProgress p = runner.progress();

    if (p.state != m_impl->lastState)
    {
        m_impl->lastState = p.state;
        emit jobStateChanged(static_cast<int>(p.state),
            QString::fromUtf8(Hw::planStateName(p.state)));
    }

    if (p.commandIndex != m_impl->lastIndex || p.fraction != m_impl->lastFraction)
    {
        m_impl->lastIndex = p.commandIndex;
        m_impl->lastFraction = p.fraction;
        emit jobProgress(p.fraction, static_cast<int>(p.commandIndex),
            static_cast<int>(p.commandCount));
    }

    switch (p.state)
    {
    case Hw::PlanState::Completed:
        m_impl->active = false;
        m_impl->pollTimer.stop();
        SY_INFOF("[ProcessingJob] Finished '%s'", m_impl->jobId.toUtf8().constData());
        emit jobFinished(true, QStringLiteral("加工完成：%1").arg(m_impl->jobId));
        break;

    case Hw::PlanState::Aborted:
        m_impl->active = false;
        m_impl->pollTimer.stop();
        emit jobFinished(false, QStringLiteral("加工已中止：%1").arg(m_impl->jobId));
        break;

    case Hw::PlanState::Error:
        m_impl->active = false;
        m_impl->pollTimer.stop();
        SY_ERRORF("[ProcessingJob] Job '%s' failed: %s",
            m_impl->jobId.toUtf8().constData(), Hw::hwErrorName(p.error));
        emit jobFinished(false, QStringLiteral("加工出错：[%1]")
                                    .arg(QString::fromUtf8(Hw::hwErrorName(p.error))));
        break;

    default:
        break;
    }
}

void ProcessingJobService::onSafetyVerdictChanged(bool canStartProcessing,
    const QString& firstViolation)
{
    if (canStartProcessing || !m_impl->active)
    {
        return;
    }

    // 安全条件在加工中途被破坏（开门、踩安全光栅）。
    // 这里主动暂停，不依赖安全条件是否配了 pause 动作 ——
    // 「裁决已经不允许开工，却还在继续出光」在任何配置下都不可接受。
    const PlanRunner runner = PlanRunner::resolve(m_impl->host);
    if (!runner.valid())
    {
        return;
    }
    SY_ERRORF("[ProcessingJob] Safety violated during job '%s' ('%s'), pausing",
        m_impl->jobId.toUtf8().constData(), firstViolation.toUtf8().constData());
    runner.pause();
    pollProgress();
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
        return QStringLiteral("本次构建未启用硬件模块（BUILD_HARDWARE=OFF），无法加工");
    }
}

ProcessingJobService::ProcessingJobService(DeviceHost* host, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->host = host;
}

ProcessingJobService::~ProcessingJobService() = default;

bool ProcessingJobService::startJob(const Eg::SceneManager& scene, const LayerManager* layers,
    const ToolpathJobSpec& spec, QString& errorOut)
{
    Q_UNUSED(scene);
    Q_UNUSED(layers);
    Q_UNUSED(spec);
    errorOut = noHardwareReason();
    SY_WARNF("[ProcessingJob] %s", errorOut.toUtf8().constData());
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

bool ProcessingJobService::isRunning() const { return false; }

bool ProcessingJobService::isPaused() const { return false; }

QString ProcessingJobService::currentJobId() const { return m_impl->jobId; }

ToolpathCompileResult ProcessingJobService::lastCompileResult() const { return m_impl->lastResult; }

double ProcessingJobService::progressFraction() const { return 0.0; }

/// 0 == Hw::PlanState::Idle，保持与硬件版一致的数值语义。
int ProcessingJobService::planState() const { return 0; }

QString ProcessingJobService::planStateName() const { return QStringLiteral("Idle"); }

void ProcessingJobService::pollProgress() {}

void ProcessingJobService::onSafetyVerdictChanged(bool canStartProcessing,
    const QString& firstViolation)
{
    Q_UNUSED(canStartProcessing);
    Q_UNUSED(firstViolation);
}

#endif  // ENABLE_HARDWARE
