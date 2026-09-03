#include "DeviceHost.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>

#include "Log/SyLogger.h"

#ifdef ENABLE_HARDWARE

#include "Hardware/Device/DeviceRegistry.h"
#include "Hardware/Device/IGalvoCard.h"
#include "Hardware/Device/IIoModule.h"
#include "Hardware/Device/ILaserSource.h"
#include "Hardware/Device/IMotionCard.h"
#include "Hardware/Device/IoPointMap.h"
#include "Hardware/Device/ISimulationClock.h"
#include "Hardware/Device/SafetyMonitor.h"

namespace
{
    /// 档案里的 severity 字符串 → Hw::SafetyState。无法识别时取最严格的一档。
    Hw::SafetyState parseSeverity(const QString& text)
    {
        if (text.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0)
        {
            return Hw::SafetyState::Normal;
        }
        if (text.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0)
        {
            return Hw::SafetyState::Warning;
        }
        if (text.compare(QStringLiteral("emergency"), Qt::CaseInsensitive) == 0)
        {
            return Hw::SafetyState::Emergency;
        }
        if (text.compare(QStringLiteral("blocked"), Qt::CaseInsensitive) != 0)
        {
            // 拼错的 severity 按最严处理：安全等级上「猜宽松」是不能接受的
            SY_WARNF("[DeviceHost] Unknown safety severity '%s', treating as Emergency",
                text.toUtf8().constData());
            return Hw::SafetyState::Emergency;
        }
        return Hw::SafetyState::Blocked;
    }

    /// 档案里的 actions 字符串数组 → Hw::SafetyAction 位掩码。
    uint32_t parseActions(const QStringList& actions)
    {
        uint32_t mask = Hw::SafetyActionNone;
        for (const QString& a : actions)
        {
            if (a.compare(QStringLiteral("block_start"), Qt::CaseInsensitive) == 0)
            {
                mask |= Hw::SafetyActionBlockStart;
            }
            else if (a.compare(QStringLiteral("pause"), Qt::CaseInsensitive) == 0)
            {
                mask |= Hw::SafetyActionPause;
            }
            else if (a.compare(QStringLiteral("laser_off"), Qt::CaseInsensitive) == 0)
            {
                mask |= Hw::SafetyActionLaserOff;
            }
            else if (a.compare(QStringLiteral("stop_motion"), Qt::CaseInsensitive) == 0)
            {
                mask |= Hw::SafetyActionStopMotion;
            }
            else if (a.compare(QStringLiteral("emergency"), Qt::CaseInsensitive) == 0)
            {
                mask |= Hw::SafetyActionEmergency;
            }
            else
            {
                SY_WARNF("[DeviceHost] Unknown safety action '%s' ignored", a.toUtf8().constData());
            }
        }
        if (mask == Hw::SafetyActionNone)
        {
            // 一条「被违反了但什么都不做」的安全条件没有意义，
            // 大概率是 actions 拼错或漏写，至少要拦住开工
            mask = Hw::SafetyActionBlockStart;
        }
        return mask;
    }
}

/**
 * @brief 装配后的机器实体。
 *
 * 所有 Hw:: 类型都收在这里，头文件保持零 Hardware 依赖。
 */
struct DeviceHost::Impl
{
    /**
     * @brief 安全动作执行器：把安全裁决落到具体设备上。
     *
     * 「切断出光」在振镜一体机上是 ILaserSource，在运动卡机型上是一路 DO，
     * 因此这层映射必须由宿主给出 —— 这正是 ISafetyActuator 用回调
     * 而不是让 SafetyMonitor 直接持有设备指针的原因。
     */
    struct Actuator final : public Hw::ISafetyActuator
    {
        Impl* owner = nullptr;

        void onLaserOff() override
        {
            if (owner->laser)
            {
                owner->laser->setEmissionEnabled(false);
            }
            if (owner->galvo)
            {
                owner->galvo->abortMark();
            }
        }

        void onStopMotion() override
        {
            if (owner->motion)
            {
                const int32_t axes = owner->motion->axisCount();
                for (int32_t i = 0; i < axes; ++i)
                {
                    owner->motion->stopAxis(i);
                }
            }
        }

        void onPauseProcessing() override
        {
            if (owner->motion)
            {
                owner->motion->pausePlan();
            }
            if (owner->galvo)
            {
                owner->galvo->pauseMark();
            }
        }

        void onEmergencyStop() override
        {
            // 顺序刻意如此：先关光再停机械。
            // 反过来的话，机械已停但激光还亮着的那几毫秒足够烧穿工件。
            if (owner->laser)
            {
                owner->laser->setEmissionEnabled(false);
            }
            if (owner->galvo)
            {
                owner->galvo->abortMark();
            }
            if (owner->motion)
            {
                owner->motion->emergencyStop();
            }
            if (owner->io)
            {
                owner->pointMap.deactivateAllOutputs();
            }
        }

        void onSafetyStateChanged(const Hw::SafetyVerdict& verdict) override
        {
            // 只记日志：信号的发出统一由 onTick 做（那里能保证在主线程）。
            // 打点位名而不是 firstViolation —— 后者是给界面看的本地化文案。
            SY_WARNF("[DeviceHost] Safety state -> %s (violations=%d, first=%s)",
                Hw::safetyStateName(verdict.state), verdict.violationCount, verdict.firstViolationPoint);
        }

    };

    /**
     * @brief 设备事件接收者：把 HAL 的回调转成 Qt 信号。
     *
     * HAL 约定 onDeviceEvent 可能在适配器工作线程里被调用，
     * 因此这里只做「投递」，不做任何 UI 或状态修改。
     */
    struct EventSink final : public Hw::IDeviceEventSink
    {
        DeviceHost* host = nullptr;

        void onDeviceEvent(const Hw::DeviceEvent& event) override
        {
            if (!host)
            {
                return;
            }
            // QueuedConnection：把跨线程的 POD 事件安全搬到主线程。
            // 直接 emit 会让 UI 槽函数在适配器线程里跑。
            QMetaObject::invokeMethod(host, "onDeviceEventQueued", Qt::QueuedConnection,
                Q_ARG(int, static_cast<int>(event.type)),
                Q_ARG(int, event.channel),
                Q_ARG(double, event.value),
                Q_ARG(QString, QString::fromUtf8(event.message)));
        }
    };

    Hw::IDevice* device = nullptr;
    Hw::IMotionCard* motion = nullptr;
    Hw::IGalvoCard* galvo = nullptr;
    Hw::ILaserSource* laser = nullptr;
    Hw::IIoModule* io = nullptr;
    Hw::ISimulationClock* simClock = nullptr;

    Hw::IoPointMap pointMap;
    Hw::SafetyMonitor monitor;
    Actuator actuator;
    EventSink sink;

    QTimer timer;
    QElapsedTimer clock;
    qint64 lastTickMs = 0;

    /// 累加的时间基准（毫秒），供 IO 去抖与安全评估使用
    qint64 nowMs = 0;


    MachineProfile profile;
    QString displayName;

    // 上一次裁决，用于「只在变化时发信号」
    bool lastCanStart = false;
    QString lastFirstViolation;
    QStringList violations;
    int lastDeviceState = -1;

    /// 销毁设备。必须走 destroy()：new 在 Hardware.dll 里，delete 也得在那边。
    void destroyDevice()
    {
        pointMap.attachModule(nullptr);
        monitor.attach(nullptr, nullptr);
        if (device)
        {
            device->setEventSink(nullptr);
            device->close();
            device->destroy();
            device = nullptr;
        }
        motion = nullptr;
        galvo = nullptr;
        laser = nullptr;
        io = nullptr;
        simClock = nullptr;
    }
};

DeviceHost::DeviceHost(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->actuator.owner = m_impl.get();
    m_impl->sink.host = this;
    m_impl->timer.setTimerType(Qt::PreciseTimer);
    connect(&m_impl->timer, &QTimer::timeout, this, &DeviceHost::onTick);
}

DeviceHost::~DeviceHost()
{
    stop();
}

bool DeviceHost::isHardwareSupportCompiled()
{
    return true;
}

bool DeviceHost::start(const MachineProfile& profile, QString& errorOut)
{
    if (m_impl->device)
    {
        errorOut = QStringLiteral("设备已经启动，请先 stop()");
        return false;
    }

    // 幂等，可以放心每次启动都调
    Hw::registerBuiltinDevices();

    const QByteArray idUtf8 = profile.deviceId.toUtf8();
    Hw::IDevice* device = Hw::DeviceRegistry::instance().create(idUtf8.constData());
    if (!device)
    {
        errorOut = QStringLiteral("未知设备 ID：%1（可用 ID 见日志）").arg(profile.deviceId);
        SY_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
        return false;
    }
    m_impl->device = device;
    m_impl->profile = profile;

    const Hw::DeviceDescriptor& desc = device->descriptor();
    m_impl->displayName = QStringLiteral("%1 %2")
                              .arg(QString::fromUtf8(desc.vendor).trimmed())
                              .arg(QString::fromUtf8(desc.displayName).trimmed())
                              .trimmed();

    device->setEventSink(&m_impl->sink);

    if (!profile.autoOpen)
    {
        // 现场排查接线时的模式：软件起来了但不碰硬件。
        // 这不是失败，但必须让日志说清楚，否则会被当成「设备连不上」
        SY_WARNF("[DeviceHost] autoOpen=false, device '%s' created but NOT opened",
            idUtf8.constData());
        return true;
    }

    // 档案里的键值搬进 ParamSet。HAL 侧一律用字符串承载，
    // 类型解析由各适配器按自己的 ParamSchema 负责
    Hw::ParamSet params;
    for (auto it = profile.openParams.constBegin(); it != profile.openParams.constEnd(); ++it)
    {
        if (params.count >= static_cast<int32_t>(Hw::kMaxParams))
        {
            SY_ERRORF("[DeviceHost] Too many open params (>%zu), '%s' and the rest are dropped",
                Hw::kMaxParams, it.key().toUtf8().constData());
            break;
        }
        Hw::ParamValue& pv = params.items[params.count++];
        Hw::hwCopyText(pv.key, Hw::kIdLen, it.key().toUtf8().constData());
        Hw::hwCopyText(pv.value, Hw::kTextLen, it.value().toUtf8().constData());
    }

    const Hw::HwResult opened = device->open(params);
    if (opened.failed())
    {
        errorOut = QStringLiteral("打开设备 %1 失败：[%2] %3")
                       .arg(profile.deviceId)
                       .arg(QString::fromUtf8(Hw::hwErrorName(opened.error)))
                       .arg(QString::fromUtf8(opened.message));
        SY_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
        m_impl->destroyDevice();
        return false;
    }

    m_impl->motion = Hw::hwQuery<Hw::IMotionCard>(device);
    m_impl->galvo = Hw::hwQuery<Hw::IGalvoCard>(device);
    m_impl->laser = Hw::hwQuery<Hw::ILaserSource>(device);
    m_impl->io = Hw::hwQuery<Hw::IIoModule>(device);
    m_impl->simClock = Hw::hwQuery<Hw::ISimulationClock>(device);

    SY_DEBUGF("[DeviceHost] Opened '%s' (%s) caps: motion=%d galvo=%d laser=%d io=%d sim=%d",
        idUtf8.constData(), m_impl->displayName.toUtf8().constData(),
        m_impl->motion ? 1 : 0, m_impl->galvo ? 1 : 0, m_impl->laser ? 1 : 0,
        m_impl->io ? 1 : 0, m_impl->simClock ? 1 : 0);

    // --- IO 点位 ---
    if (!profile.ioPoints.isEmpty())
    {
        if (!m_impl->io)
        {
            // 配了点位却没有 IO 能力：安全条件会全部判为 invalid，
            // 而 violateWhenInvalid 默认 true，结果是永远无法开工。
            // 与其让人对着「无法开工」发愣，不如在这里直接说清
            errorOut = QStringLiteral("档案配置了 %1 个 IO 点位，但设备 %2 不提供 IO 能力")
                           .arg(profile.ioPoints.size())
                           .arg(profile.deviceId);
            SY_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
            m_impl->destroyDevice();
            return false;
        }
        m_impl->pointMap.attachModule(m_impl->io);

        for (const MachineIoPointConfig& pc : profile.ioPoints)
        {
            Hw::IoPointDef def;
            Hw::hwCopyText(def.name, Hw::kPointNameLen, pc.name.toUtf8().constData());
            Hw::hwCopyText(def.label, Hw::kNameLen, pc.label.toUtf8().constData());
            def.channel = pc.channel;
            def.direction = pc.output ? Hw::IoDirection::Output : Hw::IoDirection::Input;
            def.signalType = pc.analog ? Hw::IoSignalType::Analog : Hw::IoSignalType::Digital;
            def.activeLow = pc.activeLow;
            def.debounceMs = pc.debounceMs;
            def.analogThreshold = pc.analogThreshold;
            def.analogActiveBelow = pc.analogActiveBelow;

            if (!m_impl->pointMap.definePoint(def))
            {
                SY_ERRORF("[DeviceHost] definePoint failed: '%s' ch=%d",
                    pc.name.toUtf8().constData(), pc.channel);
            }
        }
    }

    // --- 安全条件 ---
    m_impl->monitor.attach(&m_impl->pointMap, &m_impl->actuator);
    for (const MachineSafetyConditionConfig& cc : profile.safetyConditions)
    {
        Hw::SafetyCondition cond;
        Hw::hwCopyText(cond.pointName, Hw::kPointNameLen, cc.pointName.toUtf8().constData());
        Hw::hwCopyText(cond.description, Hw::kTextLen, cc.description.toUtf8().constData());
        cond.triggerOnActive = cc.triggerOnActive;
        cond.severity = parseSeverity(cc.severity);
        cond.actions = parseActions(cc.actions);
        cond.violateWhenInvalid = cc.violateWhenInvalid;

        if (!m_impl->monitor.addCondition(cond))
        {
            SY_ERRORF("[DeviceHost] addCondition failed for point '%s'",
                cc.pointName.toUtf8().constData());
        }
    }

    if (profile.safetyConditions.isEmpty())
    {
        // 没有任何安全条件时 SafetyMonitor 会一路放行。
        // 这在演示/模拟场景合理，在真机上是重大隐患，必须留痕
        SY_WARNF("[DeviceHost] No safety conditions configured — "
                 "processing will never be blocked by interlocks");
    }

    // --- 周期驱动 ---
    m_impl->clock.start();
    m_impl->lastTickMs = 0;
    m_impl->timer.start(profile.tickIntervalMs);

    SY_DEBUGF("[DeviceHost] Started: tick=%dms, %lld IO point(s), %d safety condition(s)%s",
        profile.tickIntervalMs,
        static_cast<long long>(m_impl->pointMap.pointCount()),
        m_impl->monitor.conditionCount(),
        profile.fromFallback ? " [SIMULATED FALLBACK]" : "");
    return true;
}

void DeviceHost::stop()
{
    // 幂等：stop() 有多条调用路径（应用关闭流程、切换档案、析构），
    // 且 AppBootstrapper 的显式 shutdown 与其析构都会走到这里。
    // 没有 device 也没有定时器时直接返回，避免同一次关闭刷出多轮
    // 「Stopping / Detached / clearPoints」日志，掩盖真正的一次停机。
    if (!m_impl->device && !m_impl->timer.isActive())
    {
        return;
    }

    m_impl->timer.stop();
    if (m_impl->device)
    {
        SY_DEBUGF("[DeviceHost] Stopping '%s'", m_impl->profile.deviceId.toUtf8().constData());
    }
    m_impl->destroyDevice();
    m_impl->monitor.clearConditions();
    m_impl->pointMap.clearPoints();
    m_impl->lastCanStart = false;
    m_impl->lastFirstViolation.clear();
    m_impl->violations.clear();
    m_impl->lastDeviceState = -1;
    m_impl->nowMs = 0;
    m_impl->lastTickMs = 0;
}


bool DeviceHost::isRunning() const
{
    return m_impl->device != nullptr && m_impl->timer.isActive();
}

QString DeviceHost::deviceId() const
{
    return m_impl->device ? m_impl->profile.deviceId : QString();
}

QString DeviceHost::deviceDisplayName() const
{
    return m_impl->device ? m_impl->displayName : QString();
}

bool DeviceHost::isSimulated() const
{
    return m_impl->simClock != nullptr;
}

bool DeviceHost::canStartProcessing() const
{
    return m_impl->device != nullptr && m_impl->lastCanStart;
}

QStringList DeviceHost::safetyViolations() const
{
    return m_impl->violations;
}

Hw::IDevice* DeviceHost::device() const
{
    return m_impl->device;
}

Hw::IMotionCard* DeviceHost::motionCard() const
{
    return m_impl->motion;
}

Hw::IGalvoCard* DeviceHost::galvoCard() const
{
    return m_impl->galvo;
}

Hw::ILaserSource* DeviceHost::laserSource() const
{
    return m_impl->laser;
}

Hw::IIoModule* DeviceHost::ioModule() const
{
    return m_impl->io;
}

void DeviceHost::requestEmergencyStop()
{
    if (!m_impl->device)
    {
        SY_WARNF("[DeviceHost] Emergency stop requested but no device is running");
        return;
    }
    SY_ERRORF("[DeviceHost] Emergency stop requested by application");
    // 只走 triggerSoftwareEmergency：它内部已经立即调用同一个执行器
    // （见 SafetyMonitor::triggerSoftwareEmergency 的注释）并置上闭锁。
    // 这里再直接调一次 actuator.onEmergencyStop()，一次按下就会关光/停机两遍，
    // 日志也翻倍。
    m_impl->monitor.triggerSoftwareEmergency("Emergency stop requested by application");
}

void DeviceHost::onTick()
{
    const qint64 now = m_impl->clock.elapsed();
    const qint64 dt = now - m_impl->lastTickMs;
    m_impl->lastTickMs = now;
    tick(dt);
}

void DeviceHost::tick(qint64 elapsedMs)
{
    if (!m_impl->device)
    {
        return;
    }

    if (elapsedMs > 0)
    {
        // 时间基准用累加值而不是 QElapsedTimer：
        // 手动推进（测试、无事件循环的宿主）与定时器驱动必须落在同一条时间轴上，
        // 否则去抖窗口在两种驱动下的行为不同
        m_impl->nowMs += elapsedMs;

        // 模拟设备靠宿主推进时间；真机这里是 nullptr，自然跳过
        if (m_impl->simClock)
        {
            m_impl->simClock->advance(elapsedMs);
        }
    }

    const qint64 now = m_impl->nowMs;

    // 顺序不能颠倒：evaluate 读的是上一次 poll 的结果
    m_impl->pointMap.poll(now);
    const Hw::SafetyVerdict verdict = m_impl->monitor.evaluate(now);


    const int32_t count = m_impl->monitor.violationCount();
    QStringList violations;
    violations.reserve(count);
    for (int32_t i = 0; i < count; ++i)
    {
        const char* text = m_impl->monitor.violationAt(i);
        if (text)
        {
            violations.append(QString::fromUtf8(text));
        }
    }
    m_impl->violations = violations;

    const QString firstViolation = QString::fromUtf8(verdict.firstViolation);
    if (verdict.canStartProcessing != m_impl->lastCanStart
        || firstViolation != m_impl->lastFirstViolation)
    {
        m_impl->lastCanStart = verdict.canStartProcessing;
        m_impl->lastFirstViolation = firstViolation;
        // 只在变化时发：20ms 一次的信号会把界面刷爆，也让日志无法阅读
        emit safetyVerdictChanged(verdict.canStartProcessing, firstViolation,
            QString::fromUtf8(verdict.firstViolationPoint));
    }


    const int state = static_cast<int>(m_impl->device->state());
    if (state != m_impl->lastDeviceState)
    {
        m_impl->lastDeviceState = state;
        emit deviceStateChanged(
            state, QString::fromUtf8(Hw::deviceStateName(m_impl->device->state())));
    }
}

void DeviceHost::onDeviceEventQueued(int eventType, int channel, double value, const QString& message)
{
    emit deviceEventReceived(eventType, channel, value, message);
}

#else  // !ENABLE_HARDWARE

/**
 * @brief 未编入硬件支持时的实现。
 *
 * 刻意不提供「假装成功」的空实现：start() 明确失败并给出原因。
 * 若这里返回 true，上层会以为设备已就绪，第一个加工命令才炸，
 * 且错误信息与真正的原因（构建时关掉了 BUILD_HARDWARE）毫无关系。
 */
struct DeviceHost::Impl
{
    QTimer timer;  ///< 仅为让构造函数中的 connect 有对象可连，不会启动
};

DeviceHost::DeviceHost(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
}

DeviceHost::~DeviceHost() = default;

bool DeviceHost::isHardwareSupportCompiled()
{
    return false;
}

bool DeviceHost::start(const MachineProfile& profile, QString& errorOut)
{
    errorOut = QStringLiteral("本次构建未启用硬件模块（BUILD_HARDWARE=OFF），无法启动设备 %1")
                   .arg(profile.deviceId);
    SY_WARNF("[DeviceHost] %s", errorOut.toUtf8().constData());
    return false;
}

void DeviceHost::stop() {}

bool DeviceHost::isRunning() const { return false; }

QString DeviceHost::deviceId() const { return QString(); }

QString DeviceHost::deviceDisplayName() const { return QString(); }

bool DeviceHost::isSimulated() const { return false; }

/// fail-safe：没有硬件支持就不允许开工，而不是「没有约束所以放行」。
bool DeviceHost::canStartProcessing() const { return false; }

QStringList DeviceHost::safetyViolations() const
{
    return QStringList{ QStringLiteral("未启用硬件模块") };
}

void DeviceHost::requestEmergencyStop()
{
    SY_WARNF("[DeviceHost] Emergency stop requested but hardware support is not compiled in");
}

void DeviceHost::onTick() {}

void DeviceHost::tick(qint64 elapsedMs)
{
    Q_UNUSED(elapsedMs);
}


void DeviceHost::onDeviceEventQueued(int eventType, int channel, double value, const QString& message)
{
    Q_UNUSED(eventType);
    Q_UNUSED(channel);
    Q_UNUSED(value);
    Q_UNUSED(message);
}

#endif  // ENABLE_HARDWARE
