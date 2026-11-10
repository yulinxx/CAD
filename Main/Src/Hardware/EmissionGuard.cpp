/**
 * @file EmissionGuard.cpp
 * @brief 出光使能生命周期状态机实现
 */
#include "EmissionGuard.h"

#include <QMutex>
#include <QMutexLocker>

#include "Log/AprLogger.h"

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/ILaserSource.h"
#endif

QString emissionStateName(EmissionState state)
{
    switch (state)
    {
    case EmissionState::Off:     return QStringLiteral("Off");
    case EmissionState::Armed:   return QStringLiteral("Armed");
    case EmissionState::Enabled: return QStringLiteral("Enabled");
    case EmissionState::Latched: return QStringLiteral("Latched");
    }
    return QStringLiteral("Unknown");
}

struct EmissionGuard::Impl
{
    mutable QMutex mutex;
    EmissionState state = EmissionState::Off;
    QStringList log;

#ifdef ENABLE_HARDWARE
    Hw::ILaserSource* laser = nullptr;
#else
    void* laser = nullptr;
#endif

    /// 记录一次状态迁移。调用方必须已持锁。
    void move(EmissionState to, const QString& reason)
    {
        const EmissionState from = state;
        state = to;
        log.append(QStringLiteral("%1>%2:%3").arg(emissionStateName(from), emissionStateName(to), reason));
    }

    /// 安全路径统一关光。latchWhenOff 为 false 时，出光本来就是关的不进入闭锁：
    /// 启动阶段安全链会例行下发关光，若也闭锁，机器将永远无法开工。
    /// 调用方必须已持锁。
    void cutoff(bool latchWhenOff, const QString& reason)
    {
        QString ignored;
        apply(false, ignored);
        if (state == EmissionState::Latched)
        {
            return;
        }
        if (state == EmissionState::Off && !latchWhenOff)
        {
            APR_INFOF("[EmissionGuard] Laser already off, safety request is a no-op (%s)",
                reason.toUtf8().constData());
            return;
        }
        move(EmissionState::Latched, reason);
        APR_ERRORF("[EmissionGuard] Emission latched off (%s)", reason.toUtf8().constData());
    }

    /// 落实硬件使能。调用方必须已持锁。
    bool apply(bool enable, QString& errorOut)
    {
#ifdef ENABLE_HARDWARE
        if (!laser)
        {
            // 没有激光器面时不算失败：运动卡机型由 IO 控光，
            // 是否真的出光取决于安全链与 IO，本层不做假设。
            return true;
        }
        const Hw::HwResult r = laser->setEmissionEnabled(enable);
        if (r.failed())
        {
            errorOut = QStringLiteral("[%1] %2")
                           .arg(QString::fromUtf8(Hw::hwErrorName(r.error)))
                           .arg(QString::fromUtf8(r.message));
            return false;
        }
#else
        Q_UNUSED(enable);
        Q_UNUSED(errorOut);
#endif
        return true;
    }
};

EmissionGuard::EmissionGuard()
    : m_impl(std::make_unique<Impl>())
{
}

EmissionGuard::~EmissionGuard() = default;

#ifdef ENABLE_HARDWARE
void EmissionGuard::attachLaser(Hw::ILaserSource* laser)
{
    QMutexLocker lock(&m_impl->mutex);
    m_impl->laser = laser;
}
#else
void EmissionGuard::attachLaser(void* laser)
{
    QMutexLocker lock(&m_impl->mutex);
    m_impl->laser = laser;
}
#endif

EmissionState EmissionGuard::state() const
{
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->state;
}

bool EmissionGuard::isEmissionPermitted() const
{
    return state() == EmissionState::Enabled;
}

bool EmissionGuard::beginJob(QString& errorOut)
{
    QMutexLocker lock(&m_impl->mutex);

    if (m_impl->state == EmissionState::Latched)
    {
        errorOut = QStringLiteral(
            "Emission is latched off by the safety chain; explicit acknowledgement is required before firing again");
        APR_WARNF("[EmissionGuard] beginJob refused: latched (%s)", m_impl->log.last().toUtf8().constData());
        return false;
    }
    if (m_impl->state == EmissionState::Enabled || m_impl->state == EmissionState::Armed)
    {
        errorOut = QStringLiteral("Emission is already enabled for a running job");
        APR_WARNF("[EmissionGuard] beginJob refused: %s", errorOut.toUtf8().constData());
        return false;
    }

    m_impl->move(EmissionState::Armed, QStringLiteral("beginJob"));
    if (!m_impl->apply(true, errorOut))
    {
        m_impl->move(EmissionState::Off, QStringLiteral("enableFailed"));
        APR_ERRORF("[EmissionGuard] beginJob failed: %s", errorOut.toUtf8().constData());
        return false;
    }
    m_impl->move(EmissionState::Enabled, QStringLiteral("enableApplied"));
    APR_INFO("[EmissionGuard] Emission enabled for job");
    return true;
}

void EmissionGuard::endJob()
{
    QMutexLocker lock(&m_impl->mutex);
    if (m_impl->state == EmissionState::Off || m_impl->state == EmissionState::Latched)
    {
        return;
    }
    QString ignored;
    m_impl->apply(false, ignored);
    m_impl->move(EmissionState::Off, QStringLiteral("endJob"));
    APR_INFO("[EmissionGuard] Emission disabled: job finished");
}

void EmissionGuard::abort(const QString& reason)
{
    QMutexLocker lock(&m_impl->mutex);
    QString ignored;
    m_impl->apply(false, ignored);
    if (m_impl->state == EmissionState::Latched)
    {
        return;
    }
    m_impl->move(EmissionState::Off, QStringLiteral("abort:%1").arg(reason));
    APR_WARNF("[EmissionGuard] Emission disabled: job aborted (%s)", reason.toUtf8().constData());
}

void EmissionGuard::safetyCutoff(const QString& reason)
{
    QMutexLocker lock(&m_impl->mutex);
    // 关光路径不做前置状态检查：哪怕当前显示已经是 Off，
    // 也必须再落一次硬件关光，避免「状态机以为关了、硬件其实还亮着」。
    m_impl->cutoff(false, QStringLiteral("safety:%1").arg(reason));
}

void EmissionGuard::emergencyStop(const QString& reason)
{
    QMutexLocker lock(&m_impl->mutex);
    // 急停是人工事件：即使当时没在出光，也必须闭锁到显式确认
    m_impl->cutoff(true, QStringLiteral("estop:%1").arg(reason));
}

void EmissionGuard::fault(const QString& reason)
{
    QMutexLocker lock(&m_impl->mutex);
    // 故障可能在出光前就被检测到，但仍要求确认后才允许再次开光
    m_impl->cutoff(true, QStringLiteral("fault:%1").arg(reason));
}

bool EmissionGuard::confirmRearm(QString& errorOut)
{
    QMutexLocker lock(&m_impl->mutex);
    if (m_impl->state != EmissionState::Latched)
    {
        errorOut = QStringLiteral("Emission is not latched, no acknowledgement is required");
        return false;
    }
    m_impl->move(EmissionState::Off, QStringLiteral("confirmedByOperator"));
    APR_INFO("[EmissionGuard] Latch cleared by operator confirmation");
    return true;
}

QStringList EmissionGuard::transitionLog() const
{
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->log;
}

void EmissionGuard::reset()
{
    QMutexLocker lock(&m_impl->mutex);
    QString ignored;
    m_impl->apply(false, ignored);
    if (m_impl->state != EmissionState::Off)
    {
        m_impl->move(EmissionState::Off, QStringLiteral("deviceStopped"));
    }
    else
    {
        m_impl->log.clear();
    }
}
