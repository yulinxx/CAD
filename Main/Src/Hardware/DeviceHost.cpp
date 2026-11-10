/**
 * @file DeviceHost.cpp
 * @brief 硬件装配层实现
 *
 * 将 HAL 零件组装为可用的机器设备。
 */
#include "DeviceHost.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QTimer>

#include "Log/AprLogger.h"

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
            APR_WARNF("[DeviceHost] Unknown safety severity '%s', treating as Emergency", text.toUtf8().constData());
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
                APR_WARNF("[DeviceHost] Unknown safety action '%s' ignored", a.toUtf8().constData());
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
}  // namespace

/**
 * @brief 装配后的机器图元。
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
            // 安全链关光必须闭锁：关光后禁止自动恢复，只有操作员显式确认才可再次开光
            owner->emission.safetyCutoff(QStringLiteral("safetyChainLaserOff"));
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
            owner->emission.emergencyStop(QStringLiteral("safetyChainEmergency"));
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
            APR_WARNF("[DeviceHost] Safety state -> %s (violations=%d, first=%s)",
                Hw::safetyStateName(verdict.state),
                verdict.violationCount,
                verdict.firstViolationPoint);
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
            QMetaObject::invokeMethod(host,
                "onDeviceEventQueued",
                Qt::QueuedConnection,
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

    /// 由 attachLaser 接入的独立激光器（运动卡机型用），不归本类所有
    Hw::ILaserSource* attachedLaser = nullptr;

    EmissionGuard emission;
    SafetyInputMonitor safetyInputs;
    SafetyInputMonitor::State safetyInputState = SafetyInputMonitor::State::NotConfigured;

    /// 当前生效的报警，key 为 "source:axis:code"
    QHash<QString, AlarmEvent> activeAlarmEvents;
    MachineState machineState = MachineState::Offline;
    qint64 lastAlarmPollMs = -1;

    Hw::IoPointMap pointMap;
    Hw::SafetyMonitor monitor;
    Actuator actuator;
    EventSink sink;

    QTimer timer;
    QElapsedTimer clock;
    qint64 lastTickMs = 0;

    /// 累加的时间基准（毫秒），供 IO 去抖与安全评估使用
    qint64 nowMs = 0;

    /// 报警聚合轮询周期。比 tick 慢一倍：状态查询是 SDK 往返，不需要 20ms 一次。
    static constexpr qint64 kAlarmPollIntervalMs = 40;

    MachineProfile profile;
    QString displayName;

    // 上一次裁决，用于「只在变化时发信号」
    bool lastCanStart = false;
    QString lastFirstViolation;
    QStringList violations;
    int lastDeviceState = -1;

    /// 实际承担出光的激光器：接入的独立激光器优先，否则用设备自带的激光面
    Hw::ILaserSource* routedLaser() const
    {
        return attachedLaser ? attachedLaser : laser;
    }

    QString routedLaserOriginText() const
    {
        return attachedLaser ? QStringLiteral("attached") : (laser ? QStringLiteral("device") : QStringLiteral("none"));
    }

    /**
     * @brief 采集一轮报警，并把当前生效的报警集合做差。
     * @param newOut 本轮新出现的报警（已恢复的不会出现在这里）
     * @return 本轮是否执行了采集
     */
    bool pollAlarms(qint64 now, QList<AlarmEvent>& newOut)
    {
        if (lastAlarmPollMs >= 0 && now - lastAlarmPollMs < kAlarmPollIntervalMs)
        {
            return false;
        }
        lastAlarmPollMs = now;

        QHash<QString, AlarmEvent> current;

        // --- 轴报警与限位 ---
        if (motion)
        {
            const int32_t axes = motion->axisCount();
            for (int32_t i = 0; i < axes; ++i)
            {
                Hw::AxisStatus st;
                if (motion->readAxisStatus(i, st).failed())
                {
                    continue;
                }
                const auto keyOf = [i](int code) { return QStringLiteral("0:%1:%2").arg(i).arg(code); };
                if (st.alarm)
                {
                    current.insert(keyOf(1001),
                        AlarmEvent{ AlarmSource::Motion, i, 1001,
                            QStringLiteral("Axis %1 drive alarm").arg(i) });
                }
                if (st.positiveLimit)
                {
                    current.insert(keyOf(1002),
                        AlarmEvent{ AlarmSource::Motion, i, 1002,
                            QStringLiteral("Axis %1 positive limit triggered").arg(i) });
                }
                if (st.negativeLimit)
                {
                    current.insert(keyOf(1003),
                        AlarmEvent{ AlarmSource::Motion, i, 1003,
                            QStringLiteral("Axis %1 negative limit triggered").arg(i) });
                }
            }
        }

        // --- 激光器故障位 ---
        if (Hw::ILaserSource* src = routedLaser())
        {
            Hw::LaserSourceStatus ls;
            if (src->readStatus(ls).ok())
            {
                const auto put = [&current](int code, bool on, const QString& text) {
                    if (on)
                    {
                        current.insert(QStringLiteral("1:-1:%1").arg(code),
                            AlarmEvent{ AlarmSource::Laser, -1, code, text });
                    }
                };
                put(2001, ls.overTemperature, QStringLiteral("Laser over temperature"));
                put(2002, ls.overCurrent, QStringLiteral("Laser over current"));
                put(2003, ls.waterFlowLow, QStringLiteral("Laser water flow low"));
                put(2004, ls.interlockOpen, QStringLiteral("Laser interlock open"));
                put(2005, ls.fault, QStringLiteral("Laser reports fault"));
            }
        }

        // --- 双通道安全输入 ---
        if (safetyInputs.isConfigured())
        {
            // 读不到点位时按「安全需求成立」处理（fail-safe），不能按「一切正常」
            const bool a = pointMap.isActive(profile.safetyChannelAPoint.toUtf8().constData(), true);
            const bool b = pointMap.isActive(profile.safetyChannelBPoint.toUtf8().constData(), true);
            const bool fb = !profile.safetyFeedbackPoint.isEmpty()
                && pointMap.isActive(profile.safetyFeedbackPoint.toUtf8().constData(), true);

            safetyInputState = safetyInputs.evaluate(a, b, fb, now);
            switch (safetyInputState)
            {
            case SafetyInputMonitor::State::CrossCheckFault:
                current.insert(QStringLiteral("2:-1:3001"),
                    AlarmEvent{ AlarmSource::Safety, -1, 3001, safetyInputs.lastReason() });
                break;
            case SafetyInputMonitor::State::FeedbackFault:
                current.insert(QStringLiteral("2:-1:3003"),
                    AlarmEvent{ AlarmSource::Safety, -1, 3003, safetyInputs.lastReason() });
                break;
            case SafetyInputMonitor::State::DemandActive:
                current.insert(QStringLiteral("2:-1:3002"),
                    AlarmEvent{ AlarmSource::Safety, -1, 3002, safetyInputs.lastReason() });
                break;
            default:
                break;
            }
        }

        for (auto it = current.constBegin(); it != current.constEnd(); ++it)
        {
            if (!activeAlarmEvents.contains(it.key()))
            {
                newOut.append(it.value());
            }
        }
        activeAlarmEvents = current;
        return true;
    }

    MachineState computeMachineState() const
    {
        if (!device)
        {
            return MachineState::Offline;
        }
        if (!timer.isActive())
        {
            return MachineState::Opening;
        }
        if (device->state() == Hw::DeviceState::Fault || !activeAlarmEvents.isEmpty())
        {
            return MachineState::Alarm;
        }
        if (motion && motion->isHoming())
        {
            return MachineState::Homing;
        }
        return MachineState::Ready;
    }

    /// 销毁设备。必须走 destroy()：new 在 Hardware.dll 里，delete 也得在那边。
    void destroyDevice()
    {
        // 设备正在被销毁：出光守卫与外部激光器指针都必须先解绑，
        // 否则后台线程或下一次开工还会打到已释放的对象
        emission.attachLaser(nullptr);
        attachedLaser = nullptr;

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
        errorOut = QStringLiteral("Device already started, call stop() first");  // 设备已经启动，请先 stop()
        APR_WARNF("[DeviceHost] start() refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    // 幂等，可以放心每次启动都调
    Hw::registerBuiltinDevices();

    const QByteArray idUtf8 = profile.deviceId.toUtf8();
    Hw::IDevice* device = Hw::DeviceRegistry::instance().create(idUtf8.constData());
    if (!device)
    {
        errorOut =
            QStringLiteral("Unknown device ID: %1 (see log for available IDs)").arg(profile.deviceId);  // 未知设备 ID
        APR_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
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
        APR_WARNF("[DeviceHost] autoOpen=false, device '%s' created but NOT opened", idUtf8.constData());
        return true;
    }

    // 档案里的键值搬进 ParamSet。HAL 侧一律用字符串承载，
    // 类型解析由各适配器按自己的 ParamSchema 负责
    Hw::ParamSet params;
    for (auto it = profile.openParams.constBegin(); it != profile.openParams.constEnd(); ++it)
    {
        if (params.count >= static_cast<int32_t>(Hw::kMaxParams))
        {
            APR_ERRORF("[DeviceHost] Too many open params (>%zu), '%s' and the rest are dropped",
                Hw::kMaxParams,
                it.key().toUtf8().constData());
            break;
        }
        Hw::ParamValue& pv = params.items[params.count++];
        Hw::hwCopyText(pv.key, Hw::kIdLen, it.key().toUtf8().constData());
        Hw::hwCopyText(pv.value, Hw::kTextLen, it.value().toUtf8().constData());
    }

    const Hw::HwResult opened = device->open(params);
    if (opened.failed())
    {
        errorOut = QStringLiteral("Failed to open device %1: [%2] %3")
                       .arg(profile.deviceId)
                       .arg(QString::fromUtf8(Hw::hwErrorName(opened.error)))
                       .arg(QString::fromUtf8(opened.message));  // 打开设备失败
        APR_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
        m_impl->destroyDevice();
        return false;
    }

    m_impl->motion = Hw::hwQuery<Hw::IMotionCard>(device);
    m_impl->galvo = Hw::hwQuery<Hw::IGalvoCard>(device);
    m_impl->laser = Hw::hwQuery<Hw::ILaserSource>(device);
    m_impl->io = Hw::hwQuery<Hw::IIoModule>(device);
    m_impl->simClock = Hw::hwQuery<Hw::ISimulationClock>(device);

    // 出光守卫指向实际承担出光的一方（独立激光器优先）
    m_impl->emission.attachLaser(m_impl->routedLaser());

    // 双通道安全输入：只监测与请求，安全功能由硬回路承担
    m_impl->safetyInputs.configure(
        profile.safetyChannelAPoint, profile.safetyChannelBPoint, profile.safetyFeedbackPoint);
    m_impl->safetyInputState = m_impl->safetyInputs.isConfigured() ? SafetyInputMonitor::State::Normal
                                                                  : SafetyInputMonitor::State::NotConfigured;

    m_impl->activeAlarmEvents.clear();
    m_impl->lastAlarmPollMs = -1;
    m_impl->machineState = MachineState::Opening;

    APR_DEBUGF("[DeviceHost] Opened '%s' (%s) caps: motion=%d galvo=%d laser=%d io=%d sim=%d",
        idUtf8.constData(),
        m_impl->displayName.toUtf8().constData(),
        m_impl->motion ? 1 : 0,
        m_impl->galvo ? 1 : 0,
        m_impl->laser ? 1 : 0,
        m_impl->io ? 1 : 0,
        m_impl->simClock ? 1 : 0);

    // --- IO 点位 ---
    if (!profile.ioPoints.isEmpty())
    {
        if (!m_impl->io)
        {
            // 配了点位却没有 IO 能力：安全条件会全部判为 invalid，
            // 而 violateWhenInvalid 默认 true，结果是永远无法开工。
            // 与其让人对着「无法开工」发愣，不如在这里直接说清
            errorOut = QStringLiteral("Profile configured %1 IO points, but device %2 does not provide IO capability")
                           .arg(profile.ioPoints.size())
                           .arg(profile.deviceId);  // 档案配置了 IO 点位，但设备不提供 IO 能力
            APR_ERRORF("[DeviceHost] %s", errorOut.toUtf8().constData());
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
                APR_ERRORF("[DeviceHost] definePoint failed: '%s' ch=%d", pc.name.toUtf8().constData(), pc.channel);
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
            APR_ERRORF("[DeviceHost] addCondition failed for point '%s'", cc.pointName.toUtf8().constData());
        }
    }

    if (profile.safetyConditions.isEmpty())
    {
        // 没有任何安全条件时 SafetyMonitor 会一路放行。
        // 这在演示/模拟场景合理，在真机上是重大隐患，必须留痕
        APR_WARNF("[DeviceHost] No safety conditions configured — "
                 "processing will never be blocked by interlocks");
    }

    // --- 周期驱动 ---
    m_impl->clock.start();
    m_impl->lastTickMs = 0;
    m_impl->timer.start(profile.tickIntervalMs);

    APR_DEBUGF("[DeviceHost] Started: tick=%dms, %lld IO point(s), %d safety condition(s)%s",
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

    // 设备即将销毁：所有持有设备指针的后台线程（加工作业下发线程）
    // 必须在这里同步收到并退出，否则会拿着已释放的指针继续调 SDK
    emit deviceStopping();

    if (m_impl->device)
    {
        APR_DEBUGF("[DeviceHost] Stopping '%s'", m_impl->profile.deviceId.toUtf8().constData());
    }
    m_impl->destroyDevice();
    m_impl->emission.reset();
    m_impl->safetyInputs.reset();
    m_impl->safetyInputState = SafetyInputMonitor::State::NotConfigured;
    m_impl->activeAlarmEvents.clear();
    m_impl->lastAlarmPollMs = -1;
    m_impl->machineState = MachineState::Offline;
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

MachineState DeviceHost::machineState() const
{
    return m_impl->machineState;
}

QString DeviceHost::machineStateName() const
{
    switch (m_impl->machineState)
    {
    case MachineState::Offline: return QStringLiteral("Offline");
    case MachineState::Opening: return QStringLiteral("Opening");
    case MachineState::Homing:  return QStringLiteral("Homing");
    case MachineState::Ready:   return QStringLiteral("Ready");
    case MachineState::Alarm:   return QStringLiteral("Alarm");
    }
    return QStringLiteral("Unknown");
}

QStringList DeviceHost::activeAlarms() const
{
    QStringList out;
    out.reserve(m_impl->activeAlarmEvents.size());
    for (const AlarmEvent& ev : m_impl->activeAlarmEvents)
    {
        out.append(ev.message);
    }
    out.sort();
    return out;
}

bool DeviceHost::supportsRuntimePause() const
{
    if (!m_impl->device)
    {
        return false;
    }
    // 文件传输型设备只能把计划生成成离线文件，运行中没有任何控制通道：
    // 点了暂停也只会继续出光。这里显式声明，供 UI 置灰，不做假支持。
    const QString transport = m_impl->profile.openParams.value(QStringLiteral("transport"));
    if (transport.compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0)
    {
        return false;
    }
    if (m_impl->profile.deviceId == QStringLiteral("ruida_rdc"))
    {
        return false;
    }
    return m_impl->motion != nullptr || m_impl->galvo != nullptr;
}

EmissionGuard& DeviceHost::emissionGuard()
{
    return m_impl->emission;
}

SafetyInputMonitor::State DeviceHost::safetyInputState() const
{
    return m_impl->safetyInputState;
}

bool DeviceHost::acknowledgeEmissionLatch(QString& errorOut)
{
    return m_impl->emission.confirmRearm(errorOut);
}

Eg::MachineFrameConfig DeviceHost::machineFrameConfig() const
{
    return m_impl->profile.machineFrame;
}

bool DeviceHost::canStartProcessing() const
{
    return m_impl->device != nullptr && m_impl->lastCanStart;
}

QStringList DeviceHost::safetyViolations() const
{
    return m_impl->violations;
}

bool DeviceHost::isEmergencyStopInputActive() const
{
    if (!m_impl->device)
    {
        // 未启动设备时读不到硬输入，按未复位处理，避免误判为可以复位
        return true;
    }

    // 点位未配置或读数无效时同样返回 true（fail-safe）
    return m_impl->pointMap.isActive(Hw::IoPoints::kEmergencyStop, true);
}

bool DeviceHost::isSoftwareEmergencyActive() const
{
    return m_impl->monitor.isSoftwareEmergency();
}

void DeviceHost::clearSoftwareEmergencyStop()
{
    // 不能以「设备是否在运行」作前置条件：离线按下急停后 softwareEmergency
    // 同样会被置位，若这里提前返回，闭锁永远清不掉，现场最省事的「修复」
    // 就会变成把急停逻辑注释掉。UI 的 EmergencyStopController::confirmReset
    // 走的正是这条路径。
    m_impl->monitor.clearSoftwareEmergency();
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

void DeviceHost::attachLaser(Hw::ILaserSource* laser)
{
    if (m_impl->attachedLaser == laser)
    {
        return;
    }
    m_impl->attachedLaser = laser;
    // 出光守卫必须跟着路由一起换，否则会继续去写已经不再承担出光的那一方
    m_impl->emission.attachLaser(m_impl->routedLaser());
    APR_INFOF("[DeviceHost] Laser route attached: %s", m_impl->routedLaserOriginText().toUtf8().constData());
}

Hw::ILaserSource* DeviceHost::routedLaserSource() const
{
    return m_impl->routedLaser();
}

QString DeviceHost::routedLaserOrigin() const
{
    return m_impl->routedLaserOriginText();
}

void DeviceHost::requestEmergencyStop()
{
    APR_ERRORF("[DeviceHost] Emergency stop requested by application");
    if (!m_impl->device)
    {
        // 不能再 early-return：设备没启动 / autoOpen=false 时 SafetyMonitor
        // 还没有 attach 执行器，triggerSoftwareEmergency 只会置标志位，
        // 走不到 Actuator::onEmergencyStop()，出光守卫不会被闭锁。
        // 后果是「离线按急停 → 开机 → 无需操作员确认即可出光」。
        APR_WARNF("[DeviceHost] No device is running; emission is still latched off");
    }

    // 出光闭锁无条件执行：emergencyStop() 再落一次硬件关光并进入 Latched
    // （已闭锁时 cutoff 直接返回，不会重复记日志）。即便随后执行器又调了一次，
    // 也只是重复「关光」这一安全动作，不会出现「一次急停停两遍机械」。
    m_impl->emission.emergencyStop(QStringLiteral("applicationEStop"));

    // canStartProcessing() 读的是上一拍 tick 的缓存 lastCanStart。
    // 不等下一拍：否则按下急停后最多 20ms 内 checkGate 仍会放行，
    // 而这段时间里出光守卫刚闭锁、机械却还没被判定为「禁止开工」。
    m_impl->lastCanStart = false;

    // 软件急停覆盖一切：后续每次 evaluate 都禁止开工，直到操作员走
    // confirmReset → clearSoftwareEmergencyStop()。设备后来才启动时，
    // 首拍 evaluate 会因 softwareEmergency 重新触发一次执行器急停。
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
    if (verdict.canStartProcessing != m_impl->lastCanStart || firstViolation != m_impl->lastFirstViolation)
    {
        m_impl->lastCanStart = verdict.canStartProcessing;
        m_impl->lastFirstViolation = firstViolation;
        // 只在变化时发：20ms 一次的信号会把界面刷爆，也让日志无法阅读
        emit safetyVerdictChanged(
            verdict.canStartProcessing, firstViolation, QString::fromUtf8(verdict.firstViolationPoint));
    }

    const int state = static_cast<int>(m_impl->device->state());
    if (state != m_impl->lastDeviceState)
    {
        m_impl->lastDeviceState = state;
        emit deviceStateChanged(state, QString::fromUtf8(Hw::deviceStateName(m_impl->device->state())));
    }

    // === 报警/限位轮询闭环 ===
    // 真机没有事件推送：轴报警、限位、激光故障位只能主动轮询。
    // 40ms 一轮，比 tick 慢一倍，避免把 SDK 状态查询打成总线拥堵。
    QList<AlarmEvent> newAlarms;
    m_impl->pollAlarms(now, newAlarms);
    for (const AlarmEvent& ev : newAlarms)
    {
        APR_ERRORF("[DeviceHost] Alarm raised: source=%d axis=%d code=%d %s", static_cast<int>(ev.source), ev.axis, ev.code,
            ev.message.toUtf8().constData());
        emit alarmRaised(static_cast<int>(ev.source), ev.axis, ev.code, ev.message);
    }
    if (!newAlarms.isEmpty())
    {
        // 报警即停光，且走闭锁路径：报警往往会重复出现，
        // 自动恢复会让激光在报警期间反复亮起
        m_impl->emission.fault(newAlarms.first().message);
        if (m_impl->galvo)
        {
            m_impl->galvo->abortMark();
        }
    }

    // 双通道安全输入：本层只做监测与请求，安全切断由硬回路承担
    if (m_impl->safetyInputs.isConfigured() && m_impl->safetyInputs.laserOffRequested())
    {
        m_impl->emission.safetyCutoff(m_impl->safetyInputs.lastReason());
    }

    const MachineState machineState = m_impl->computeMachineState();
    if (machineState != m_impl->machineState)
    {
        m_impl->machineState = machineState;
        emit machineStateChanged(static_cast<int>(machineState), machineStateName());
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
    EmissionGuard emission;
    /// 与 ENABLE_HARDWARE 分支保持同一语义：急停是人工事件，
    /// 即使本构建没有硬件也必须能被查询、闭锁与显式复位。
    bool softwareEmergency = false;
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
    // 本构建未编入硬件模块，模拟设备同样不可用（模拟实现位于 Hardware.dll）。
    // 这是构建配置的结果而非故障：用 INFO 而不是 WARN，避免与 MachineProfile
    // 的「无真机配置」告警叠在一起、被误读成一串启动错误。
    errorOut = QStringLiteral(
        "Device simulation is not available in this build (BUILD_HARDWARE=OFF); device %1 skipped")
                   .arg(profile.deviceId);
    APR_INFOF("[DeviceHost] %s", errorOut.toUtf8().constData());
    return false;
}

void DeviceHost::stop() {}

bool DeviceHost::isRunning() const
{
    return false;
}

QString DeviceHost::deviceId() const
{
    return QString();
}

QString DeviceHost::deviceDisplayName() const
{
    return QString();
}

bool DeviceHost::isSimulated() const
{
    return false;
}

Eg::MachineFrameConfig DeviceHost::machineFrameConfig() const
{
    return Eg::MachineFrameConfig{};  // 未启用硬件模块 → 恒等变换
}

/// fail-safe：没有硬件支持就不允许开工，而不是「没有约束所以放行」。
bool DeviceHost::canStartProcessing() const
{
    return false;
}

QStringList DeviceHost::safetyViolations() const
{
    return QStringList{ QStringLiteral("Hardware module not enabled") };  // 未启用硬件模块
}

bool DeviceHost::isEmergencyStopInputActive() const
{
    // 无硬件支持时不存在可观测的硬输入，按未复位处理
    return true;
}

bool DeviceHost::isSoftwareEmergencyActive() const
{
    return m_impl->softwareEmergency;
}

void DeviceHost::clearSoftwareEmergencyStop()
{
    m_impl->softwareEmergency = false;
}

void DeviceHost::requestEmergencyStop()
{
    APR_ERRORF("[DeviceHost] Emergency stop requested by application");
    APR_WARNF("[DeviceHost] Hardware support is not compiled in; latching emission off anyway");
    m_impl->softwareEmergency = true;
    m_impl->emission.emergencyStop(QStringLiteral("applicationEStop"));
}

void DeviceHost::onTick() {}

void DeviceHost::tick(qint64 elapsedMs)
{
    Q_UNUSED(elapsedMs);
}

MachineState DeviceHost::machineState() const
{
    return MachineState::Offline;
}

QString DeviceHost::machineStateName() const
{
    return QStringLiteral("Offline");
}

QStringList DeviceHost::activeAlarms() const
{
    return QStringList();
}

bool DeviceHost::supportsRuntimePause() const
{
    return false;
}

EmissionGuard& DeviceHost::emissionGuard()
{
    return m_impl->emission;
}

SafetyInputMonitor::State DeviceHost::safetyInputState() const
{
    return SafetyInputMonitor::State::NotConfigured;
}

bool DeviceHost::acknowledgeEmissionLatch(QString& errorOut)
{
    // 与 requestEmergencyStop 成对：既然无硬件构建也会闭锁出光，
    // 就必须允许操作员显式解除，否则闭锁永远清不掉。
    return m_impl->emission.confirmRearm(errorOut);
}

void DeviceHost::onDeviceEventQueued(int eventType, int channel, double value, const QString& message)
{
    Q_UNUSED(eventType);
    Q_UNUSED(channel);
    Q_UNUSED(value);
    Q_UNUSED(message);
}

#endif  // ENABLE_HARDWARE