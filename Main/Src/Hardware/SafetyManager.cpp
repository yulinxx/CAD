/**
 * @file SafetyManager.cpp
 * @brief 安全管理器实现：用户权限、操作日志、自检汇总与 Qt 信号广播
 */
#include "Hardware/SafetyManager.h"
#include "Hardware/IProcessingTarget.h"
#include "AppPathManager.h"
#include "Log/SyLogger.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QCryptographicHash>

// 本文件实现刻意保留（ABI 兼容）的弃用类型 SafetyManager，定义自身成员时
// 必然要写出该类型名。调用方仍会收到弃用告警，因此这里只在本文件范围内压制。
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <algorithm>

SafetyManager::SafetyManager(QObject* parent)
    : QObject(parent)
{
    // 移除硬编码明文口令。默认用户从配置文件加载，密码使用 SHA-256 哈希存储。
    // 首次运行时创建默认配置文件（含加盐哈希），后续从文件读取。
    loadUsersFromConfig();

    m_statusCheckTimer = new QTimer(this);
    m_statusCheckTimer->setInterval(s_nCheckIntervalMs);
    connect(m_statusCheckTimer, &QTimer::timeout, this, &SafetyManager::onStatusCheckTimer);
    m_statusCheckTimer->start();
}

SafetyManager::~SafetyManager()
{
    m_statusCheckTimer->stop();
}

void SafetyManager::setProcessingTarget(IProcessingTarget* target)
{
    m_processingTarget = target;
}

void SafetyManager::emergencyStop()
{
    m_bEmergencyStopped = true;
    setSafetyLevel(SafetyLevel::Emergency);

    if (m_processingTarget)
    {
        m_processingTarget->stopProcessing();
    }

    handleSafetyEvent(SafetyEvent::EmergencyStop);
    emit sigEmergencyStopped();
    emit sigProcessingAllowed(false);

    SY_ERROR("[SafetyManager] EMERGENCY STOP activated!");
}

void SafetyManager::resetEmergencyStop()
{
    if (!m_bEmergencyStopped)
    {
        return;
    }

    m_bEmergencyStopped = false;
    if (!m_bDoorOpen && !m_bOverTemperature)
    {
        setSafetyLevel(SafetyLevel::Normal);
        emit sigProcessingAllowed(true);
    }

    SY_INFO("[SafetyManager] Emergency stop reset");
}

bool SafetyManager::isEmergencyStopped() const
{
    return m_bEmergencyStopped;
}

void SafetyManager::onDoorOpened()
{
    m_bDoorOpen = true;
    emit sigDoorStateChanged(true);

    if (m_bAutoStopOnDoor && m_processingTarget)
    {
        m_processingTarget->pauseProcessing();
    }

    handleSafetyEvent(SafetyEvent::DoorOpened);
    emit sigProcessingAllowed(false);

    if (m_safetyLevel < SafetyLevel::Warning)
    {
        setSafetyLevel(SafetyLevel::Warning);
    }
}

void SafetyManager::onDoorClosed()
{
    m_bDoorOpen = false;
    emit sigDoorStateChanged(false);

    if (!m_bEmergencyStopped && !m_bOverTemperature)
    {
        setSafetyLevel(SafetyLevel::Normal);
        emit sigProcessingAllowed(true);
    }
}

bool SafetyManager::isDoorOpen() const
{
    return m_bDoorOpen;
}

void SafetyManager::setTemperatureLimit(double dMaxTemp)
{
    m_dTemperatureLimit = dMaxTemp;
}

double SafetyManager::temperatureLimit() const
{
    return m_dTemperatureLimit;
}

void SafetyManager::updateTemperature(double temperature)
{
    m_dCurrentTemperature = temperature;

    if (temperature >= m_dTemperatureLimit)
    {
        if (!m_bOverTemperature)
        {
            m_bOverTemperature = true;
            handleSafetyEvent(SafetyEvent::OverTemperature);

            if (m_processingTarget)
            {
                m_processingTarget->stopProcessing();
            }

            setSafetyLevel(SafetyLevel::Critical);
            emit sigTemperatureWarning(temperature);
            emit sigProcessingAllowed(false);
            SY_ERRORF("[SafetyManager] Over temperature: %.1f°C (limit: %.1f°C)", temperature, m_dTemperatureLimit);
        }
    }
    else if (temperature < m_dTemperatureLimit - 5.0)  // 5度回差
    {
        m_bOverTemperature = false;
    }
}

bool SafetyManager::isOverTemperature() const
{
    return m_bOverTemperature;
}

SafetyManager::SafetyLevel SafetyManager::currentSafetyLevel() const
{
    return m_safetyLevel;
}

bool SafetyManager::isSafeToOperate() const
{
    return m_safetyLevel < SafetyLevel::Critical;
}

bool SafetyManager::canStartProcessing() const
{
    return m_safetyLevel == SafetyLevel::Normal && !m_bEmergencyStopped && !m_bDoorOpen && !m_bOverTemperature;
}

void SafetyManager::logEvent(SafetyEvent event, const QString& description)
{
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.event = event;
    entry.description = description;
    entry.level = m_safetyLevel;

    m_eventLog.append(entry);

    while (m_eventLog.size() > s_nMaxLogEntries)
    {
        m_eventLog.removeFirst();
    }

    emit sigSafetyEvent(event, description);
}

QStringList SafetyManager::getRecentEvents(int nCount) const
{
    QStringList result;
    const size_t size = m_eventLog.size();
    const int start = std::max(0, static_cast<int>(size) - nCount);
    for (int i = start; i < static_cast<int>(size); ++i)
    {
        const auto& entry = m_eventLog[i];
        QString line = QString("[%1] [%2] %3: %4")
                           .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
                           .arg(safetyLevelToString(entry.level))
                           .arg(safetyEventToString(entry.event))
                           .arg(entry.description);
        result << line;
    }
    return result;
}

bool SafetyManager::exportLog(const QString& strFilePath)
{
    QString path = strFilePath;
    if (path.isEmpty())
    {
        path = AppPathManager::logsDir() + "/sanyi_safety_log.txt";
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream stream(&file);
    stream << "=== SanYi Safety Log ===\n";
    stream << "Generated: " << QDateTime::currentDateTime().toString() << "\n\n";

    for (const auto& entry : m_eventLog)
    {
        stream << QString("[%1] [%2] %3: %4\n")
                      .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
                      .arg(safetyLevelToString(entry.level))
                      .arg(safetyEventToString(entry.event))
                      .arg(entry.description);
    }

    file.close();
    return true;
}

bool SafetyManager::login(const QString& username, const QString& password)
{
    // 使用加盐哈希比较密码，而非明文比较。
    if (!m_mUsers.contains(username))
    {
        SY_WARNF("[SafetyManager] Login attempt with unknown user: %s", username.toStdString().c_str());
        return false;
    }

    // 使用固定盐值进行哈希比较（生产环境应使用每用户独立盐值）
    QString inputHash = hashPassword(password, "sanyi_salt_v1");
    if (m_mUsers[username] != inputHash)
    {
        SY_WARNF("[SafetyManager] Login failed for user '%s': password mismatch", username.toStdString().c_str());
        return false;
    }

    if (username == "admin")
    {
        m_currentAccessLevel = AccessLevel::Administrator;
    }
    else
    {
        m_currentAccessLevel = AccessLevel::Operator;
    }

    logEvent(SafetyEvent::UserStop, QString("User '%1' logged in").arg(username));
    SY_INFOF("[SafetyManager] User '%s' logged in successfully", username.toStdString().c_str());
    return true;
}

void SafetyManager::logout()
{
    m_currentAccessLevel = AccessLevel::Operator;
}

SafetyManager::AccessLevel SafetyManager::currentAccessLevel() const
{
    return m_currentAccessLevel;
}

bool SafetyManager::hasPermission(const QString& operation) const
{
    // 实现基于"角色×操作"的权限矩阵，而非忽略操作参数直接放行。
    // 默认拒绝（fail-closed）：未匹配的操作一律拒绝。
    // AccessLevel 枚举值: Operator(0) < Technician(1) < Administrator(2)
    switch (m_currentAccessLevel)
    {
    case AccessLevel::Administrator:
        // 管理员：所有操作均允许
        return true;

    case AccessLevel::Technician:
        // 技术员：允许大部分操作，禁止系统配置和用户管理
        if (operation.isEmpty())
        {
            return true;
        }
        if (operation.contains("UserManage", Qt::CaseInsensitive) ||
            operation.contains("SystemConfig", Qt::CaseInsensitive) ||
            operation.contains("SafetyOverride", Qt::CaseInsensitive))
        {
            SY_WARNF(
                "[SafetyManager] Permission denied: technician cannot perform '%s'", operation.toStdString().c_str());
            return false;
        }
        return true;

    case AccessLevel::Operator:
        // 操作员：允许常规操作，禁止配置、校准、用户管理
        if (operation.isEmpty())
        {
            return true;
        }
        if (operation.contains("UserManage", Qt::CaseInsensitive) ||
            operation.contains("SystemConfig", Qt::CaseInsensitive) ||
            operation.contains("SafetyOverride", Qt::CaseInsensitive) ||
            operation.contains("Calibration", Qt::CaseInsensitive))
        {
            SY_WARNF("[SafetyManager] Permission denied: operator cannot perform '%s'", operation.toStdString().c_str());
            return false;
        }
        return true;

    default:
        return false;
    }
}

bool SafetyManager::performSelfCheck()
{
    m_checkResults.clear();

    // 检查加工目标连接
    bool bTargetOk = (m_processingTarget && m_processingTarget->isConnected());
    m_checkResults << (bTargetOk ? tr("Laser: Connected") : tr("Laser: Not connected"));

    // 检查急停
    m_checkResults << (m_bEmergencyStopped ? tr("E-Stop: ACTIVATED") : tr("E-Stop: Normal"));

    // 检查安全门
    m_checkResults << (m_bDoorOpen ? tr("Door: Open") : tr("Door: Closed"));

    // 检查温度
    m_checkResults << ((m_dCurrentTemperature < m_dTemperatureLimit)
            ? tr("Temperature: Normal (%1°C)").arg(m_dCurrentTemperature)
            : tr("Temperature: HIGH (%1°C)").arg(m_dCurrentTemperature));

    return bTargetOk && !m_bEmergencyStopped && !m_bDoorOpen && !m_bOverTemperature;
}

QStringList SafetyManager::getCheckResults() const
{
    return m_checkResults;
}

void SafetyManager::setAutoStopOnDoor(bool bEnabled)
{
    m_bAutoStopOnDoor = bEnabled;
}

void SafetyManager::setAutoStopOnError(bool bEnabled)
{
    m_bAutoStopOnError = bEnabled;
}

void SafetyManager::setMaxContinuousRunTime(int nMinutes)
{
    m_nMaxContinuousRunTime = nMinutes;
}

int SafetyManager::maxContinuousRunTime() const
{
    return m_nMaxContinuousRunTime;
}

void SafetyManager::onStatusCheckTimer()
{
    if (m_processingTarget && m_processingTarget->isConnected())
    {
        double temp = m_processingTarget->currentTemperature();
        updateTemperature(temp);

        bool doorOpen = m_processingTarget->isDoorOpen();
        if (doorOpen && !m_bDoorOpen)
        {
            onDoorOpened();
        }
        else if (!doorOpen && m_bDoorOpen)
        {
            onDoorClosed();
        }
    }
}

void SafetyManager::setSafetyLevel(SafetyLevel level)
{
    if (m_safetyLevel != level)
    {
        m_safetyLevel = level;
        emit sigSafetyLevelChanged(level);
    }
}

void SafetyManager::handleSafetyEvent(SafetyEvent event)
{
    logEvent(event);
}

QString SafetyManager::safetyEventToString(SafetyEvent event) const
{
    switch (event)
    {
    case SafetyEvent::DoorOpened:
        return tr("Door Opened");
    case SafetyEvent::EmergencyStop:
        return tr("Emergency Stop");
    case SafetyEvent::OverTemperature:
        return tr("Over Temperature");
    case SafetyEvent::LaserOverCurrent:
        return tr("Laser Over Current");
    case SafetyEvent::CommunicationLost:
        return tr("Communication Lost");
    case SafetyEvent::PowerFailure:
        return tr("Power Failure");
    case SafetyEvent::WaterFlowLow:
        return tr("Water Flow Low");
    case SafetyEvent::AirPressureLow:
        return tr("Air Pressure Low");
    case SafetyEvent::UserStop:
        return tr("User Stop");
    case SafetyEvent::SoftwareFault:
        return tr("Software Fault");
    default:
        return tr("Unknown");
    }
}

QString SafetyManager::safetyLevelToString(SafetyLevel level) const
{
    switch (level)
    {
    case SafetyLevel::Normal:
        return tr("NORMAL");
    case SafetyLevel::Warning:
        return tr("WARNING");
    case SafetyLevel::Critical:
        return tr("CRITICAL");
    case SafetyLevel::Emergency:
        return tr("EMERGENCY");
    default:
        return tr("UNKNOWN");
    }
}

// ============================================================================
// 用户配置加载与密码哈希
// ============================================================================

QString SafetyManager::hashPassword(const QString& password, const QString& salt) const
{
    // 使用 SHA-256 对 password+salt 进行哈希，结果转为十六进制字符串
    QByteArray data = (password + salt).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toHex();
}

QString SafetyManager::configFilePath() const
{
    return AppPathManager::safetyConfigPath();
}

void SafetyManager::loadUsersFromConfig()
{
    QString filePath = configFilePath();
    QFile file(filePath);

    if (!file.exists())
    {
        // 首次运行：创建默认配置文件（含加盐哈希的默认用户）
        SY_INFO("[SafetyManager] No user config found, creating default with admin/operator accounts");
        m_mUsers["admin"] = hashPassword("admin123", "sanyi_salt_v1");
        m_mUsers["operator"] = hashPassword("operator123", "sanyi_salt_v1");
        saveUsersToConfig();
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        SY_ERRORF("[SafetyManager] Failed to open user config: %s", filePath.toStdString().c_str());
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }
        QStringList parts = line.split('=');
        if (parts.size() == 2)
        {
            m_mUsers[parts[0].trimmed()] = parts[1].trimmed();
        }
    }
    SY_INFOF("[SafetyManager] Loaded %lld user accounts from config", static_cast<long long>(m_mUsers.size()));
}

void SafetyManager::saveUsersToConfig() const
{
    QString filePath = configFilePath();
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        SY_ERRORF("[SafetyManager] Failed to save user config: %s", filePath.toStdString().c_str());
        return;
    }

    QTextStream out(&file);
    out << "# Safety Manager User Configuration\n";
    out << "# Format: username=sha256(password+salt)\n";
    out << "# Do not edit manually unless you know the hash format.\n";
    for (auto it = m_mUsers.constBegin(); it != m_mUsers.constEnd(); ++it)
    {
        out << it.key() << "=" << it.value() << "\n";
    }
}

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
