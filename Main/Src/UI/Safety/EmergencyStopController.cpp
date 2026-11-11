#include "UI/Safety/EmergencyStopController.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>

#include "Hardware/DeviceHost.h"
#include "Log/AprLogger.h"

namespace
{
const QString kLatchKey = QStringLiteral("safety/emergencyStopLatched");
const int kPollIntervalMs = 100;

/// 设置身份统一取自 QCoreApplication（由品牌层在启动时设置），不再硬编码组织名/应用名
QString settingsOrganization()
{
    const QString organization = QCoreApplication::organizationName();
    return organization.isEmpty() ? QStringLiteral("April") : organization;
}

QString settingsApplication()
{
    const QString application = QCoreApplication::applicationName();
    return application.isEmpty() ? QStringLiteral("April") : application;
}

bool loadLatchFromSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, settingsOrganization(),
                       settingsApplication());
    if (settings.contains(kLatchKey))
    {
        return settings.value(kLatchKey).toBool();
    }

    // 旧版本把设置身份硬编码为 April/April：升级后必须把已闭锁状态迁过来，
    // 否则急停闭锁会被静默遗忘，属于安全风险
    QSettings legacy(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("April"),
                     QStringLiteral("April"));
    if (!legacy.contains(kLatchKey))
    {
        return false;
    }

    const bool latched = legacy.value(kLatchKey).toBool();
    settings.setValue(kLatchKey, latched);
    settings.sync();
    APR_INFOF("[EmergencyStop] Latch state migrated from legacy settings: latched=%d", latched ? 1 : 0);
    return latched;
}

void saveLatchToSettings(bool latched)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, settingsOrganization(),
                       settingsApplication());
    settings.setValue(kLatchKey, latched);
    settings.sync();
}
}  // namespace

EmergencyStopController::EmergencyStopController(QObject* parent)
    : EmergencyStopController(loadLatchFromSettings, saveLatchToSettings, parent)
{
}

EmergencyStopController::EmergencyStopController(
    std::function<bool()> loadLatch, std::function<void(bool)> saveLatch, QObject* parent)
    : QObject(parent)
    , m_loadLatch(std::move(loadLatch))
    , m_saveLatch(std::move(saveLatch))
{
    // 重启后不得自动回到可出光态：上次退出时仍在急停闭锁则直接进入急停态
    if (m_loadLatch && m_loadLatch())
    {
        m_state = State::EmergencyStopped;
        // 重启时无法得知物理急停是否已复位，先假定需要一次输入放行来确认
        m_estopInputSeenActive = true;
    }

    auto* pollTimer = new QTimer(this);
    pollTimer->setInterval(kPollIntervalMs);
    connect(pollTimer, &QTimer::timeout, this, &EmergencyStopController::pollDeviceState);
    pollTimer->start();
}

EmergencyStopController::~EmergencyStopController() = default;

void EmergencyStopController::attachDeviceHost(DeviceHost* host)
{
    if (m_host == host)
    {
        return;
    }

    if (m_host)
    {
        disconnect(m_host, nullptr, this, nullptr);
    }

    m_host = host;
    if (m_host)
    {
        connect(m_host, &DeviceHost::safetyVerdictChanged, this, [this]() { pollDeviceState(); });
        connect(m_host, &DeviceHost::deviceStateChanged, this, [this]() { pollDeviceState(); });
        connect(m_host, &DeviceHost::machineStateChanged, this, [this]() { pollDeviceState(); });
    }

    pollDeviceState();
}

void EmergencyStopController::setDeviceStateProviders(
    std::function<bool()> monitoring, std::function<bool()> inputActive)
{
    m_monitoringProvider = std::move(monitoring);
    m_inputActiveProvider = std::move(inputActive);
    pollDeviceState();
}

EmergencyStopController::State EmergencyStopController::state() const
{
    return m_state;
}

bool EmergencyStopController::isReadOnly() const
{
    return m_state == State::EmergencyStopped;
}

bool EmergencyStopController::isPhysicalResetObserved() const
{
    return m_physicalResetObserved;
}

bool EmergencyStopController::isMonitoringAvailable() const
{
    return m_monitoringAvailable;
}

bool EmergencyStopController::canConfirmReset() const
{
    if (m_state != State::EmergencyStopped)
    {
        return false;
    }

    // 没有硬件监测时设备本身无法出光，按无风险处理，允许软件侧解除界面闭锁；
    // 一旦具备监测能力，必须观测到物理复位后才允许确认
    return !m_monitoringAvailable || m_physicalResetObserved;
}

void EmergencyStopController::triggerEmergencyStop()
{
    m_estopInputSeenActive = false;
    setPhysicalResetObserved(false);
    if (m_saveLatch)
    {
        m_saveLatch(true);
    }
    setState(State::EmergencyStopped);
    pollDeviceState();
}

bool EmergencyStopController::confirmReset()
{
    if (!canConfirmReset())
    {
        return false;
    }

    // 只清除软件闭锁，不开光；开光必须由另一个独立动作触发
    if (m_host && m_host->isSoftwareEmergencyActive())
    {
        m_host->clearSoftwareEmergencyStop();
    }

    m_physicalResetObserved = false;
    if (m_saveLatch)
    {
        m_saveLatch(false);
    }
    setState(State::ResetConfirmed);
    emit physicalResetObservedChanged(false);
    return true;
}

void EmergencyStopController::pollDeviceState()
{
    bool monitoring = false;
    if (m_monitoringProvider)
    {
        monitoring = m_monitoringProvider();
    }
    else if (m_host)
    {
        monitoring = DeviceHost::isHardwareSupportCompiled() && m_host->isRunning();
    }
    setMonitoringAvailable(monitoring);

    if (m_state != State::EmergencyStopped)
    {
        return;
    }

    if (!monitoring)
    {
        setPhysicalResetObserved(false);
        return;
    }

    bool inputActive = true;
    if (m_inputActiveProvider)
    {
        inputActive = m_inputActiveProvider();
    }
    else if (m_host)
    {
        inputActive = m_host->isEmergencyStopInputActive();
    }

    // 先见过急停输入按下、再回到未按下，才算完成一次物理复位
    if (inputActive)
    {
        m_estopInputSeenActive = true;
        setPhysicalResetObserved(false);
        return;
    }

    if (m_estopInputSeenActive)
    {
        setPhysicalResetObserved(true);
    }
}

void EmergencyStopController::setState(State state)
{
    if (m_state == state)
    {
        return;
    }

    m_state = state;
    emit stateChanged(m_state);
}

void EmergencyStopController::setPhysicalResetObserved(bool observed)
{
    if (m_physicalResetObserved == observed)
    {
        return;
    }

    m_physicalResetObserved = observed;
    emit physicalResetObservedChanged(m_physicalResetObserved);
}

void EmergencyStopController::setMonitoringAvailable(bool available)
{
    if (m_monitoringAvailable == available)
    {
        return;
    }

    m_monitoringAvailable = available;
    emit monitoringAvailableChanged(m_monitoringAvailable);
}
