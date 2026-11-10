#include "LaserOperationRegistry.h"
#include "OpRegistryTypes.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include <QCoreApplication>

#include "Log/AprLogger.h"

#include "UI/Safety/EmergencyStopController.h"

#include "../Hardware/DeviceHost.h"
#include "../Hardware/ProcessingJobService.h"

namespace
{
QString hardwareUnavailableMessage()
{
    return QCoreApplication::translate("LaserOperationRegistry",
        "Hardware module is not enabled in this build (BUILD_HARDWARE=OFF). This command is disabled.");
}

/// 未编入硬件支持时，命令保持可点击，点击后给出明确提示，而不是灰掉后毫无反应。
bool hardwareSupportMissing()
{
    return !DeviceHost::isHardwareSupportCompiled();
}
}  // namespace

LaserOperationRegistry::LaserOperationRegistry(const LaserOperationConfig& config)
    : m_config(config)
{
}

void LaserOperationRegistry::reportError(const char* tag, const QString& message) const
{
    APR_ERRORF("[LaserOperation] %s: %s", tag, message.toUtf8().constData());
    if (m_config.errorReporter)
    {
        m_config.errorReporter(message);
    }
}

ToolpathJobSpec LaserOperationRegistry::buildSpec() const
{
    if (m_config.specProvider)
    {
        return m_config.specProvider();
    }
    // 没有参数来源时用结构体默认值。刻意不去猜、不去读某张还没接通的表：
    // 猜出来的功率速度打在真料上就是废件
    return ToolpathJobSpec{};
}

void LaserOperationRegistry::registerAll()
{
    if (!m_config.bus)
    {
        APR_WARNF("[LaserOperation] No OperationBus, laser commands are not registered");
        return;
    }

    auto& reg = m_config.bus->registry();

    ProcessingJobService* job = m_config.jobService;
    DeviceHost* host = m_config.deviceHost;

    // --- 开始加工 ---
    Ops::declareChecked(
        reg,
        OperationId::Laser_StartProcess,
        [this, job] {
            if (hardwareSupportMissing())
            {
                reportError("StartProcess", hardwareUnavailableMessage());
                return;
            }
            if (!job || !m_config.sceneManager)
            {
                reportError("StartProcess",
                    QCoreApplication::translate(
                        "LaserOperationRegistry", "Processing service not configured, cannot start processing"));
                return;
            }
            QString error;
            if (!job->startJob(*m_config.sceneManager, m_config.layerManager, buildSpec(), error))
            {
                reportError("StartProcess", error);
            }
        },
        [job, host, safety = m_config.safetyController] {
            // 急停态与 BUILD_HARDWARE=OFF 之外的判断保持原有语义；
            // OFF 时保持可点击，由 execute 给出明确的禁用提示
            if (hardwareSupportMissing())
            {
                return true;
            }
            // canExec 决定菜单/工具栏的可用态：设备没起来、安全门没关、
            // 或者已经在加工时，「开始加工」必须是灰的 ——
            // 让操作员点了之后才看到报错，是最容易被投诉的交互
            return job && host && host->isRunning() && host->canStartProcessing() && !job->isRunning() &&
                !job->isPaused() && (!safety || !safety->isReadOnly());
        });

    // --- 暂停 / 恢复（同一个命令来回切） ---
    Ops::declareChecked(
        reg,
        OperationId::Laser_PauseProcess,
        [this, job] {
            if (hardwareSupportMissing())
            {
                reportError("PauseProcess", hardwareUnavailableMessage());
                return;
            }
            if (!job)
            {
                reportError("PauseProcess",
                    QCoreApplication::translate("LaserOperationRegistry", "Processing service not configured"));
                return;
            }
            QString error;
            // OperationId 里没有单独的「恢复加工」，而现场需要同一个按钮来回切。
            // 这不是偷懒：暂停与恢复互斥，两个按钮里永远有一个是灰的，
            // 占两个位置却只有一个能按
            const bool ok = job->isPaused() ? job->resumeJob(error) : job->pauseJob(error);
            if (!ok)
            {
                reportError("PauseProcess", error);
            }
        },
        [job] {
            if (hardwareSupportMissing())
            {
                return true;
            }
            return job && (job->isRunning() || job->isPaused());
        });

    // --- 停止加工 ---
    Ops::declareChecked(
        reg,
        OperationId::Laser_StopProcess,
        [this, job] {
            if (hardwareSupportMissing())
            {
                reportError("StopProcess", hardwareUnavailableMessage());
                return;
            }
            if (!job)
            {
                reportError("StopProcess",
                    QCoreApplication::translate("LaserOperationRegistry", "Processing service not configured"));
                return;
            }
            QString error;
            if (!job->abortJob(error))
            {
                reportError("StopProcess", error);
            }
        },
        [job] {
            if (hardwareSupportMissing())
            {
                return true;
            }
            return job && (job->isRunning() || job->isPaused());
        });

    // --- 急停 ---
    Ops::declareChecked(
        reg,
        OperationId::Laser_EmergencyStop,
        [job, host] {
            // 顺序：先让设备执行急停动作（关光 → 停机械），再收作业状态。
            // 反过来的话，中止作业那几毫秒里激光还亮着
            if (host)
            {
                host->requestEmergencyStop();
            }
            if (job && (job->isRunning() || job->isPaused()))
            {
                QString ignored;
                job->abortJob(ignored);
            }
        },
        [] {
            // 急停永远可按。任何 canExec 判断都可能把它变灰，
            // 而「需要急停的时刻」恰恰是状态最混乱的时刻
            return true;
        });

    // --- 复位确认（与开光分离，只清闭锁不开光） ---
    Ops::declareChecked(
        reg,
        OperationId::Laser_ResetSafety,
        [this, safety = m_config.safetyController] {
            if (!safety)
            {
                reportError("ResetSafety",
                    QCoreApplication::translate("LaserOperationRegistry", "Safety controller not configured"));
                return;
            }
            if (!safety->confirmReset())
            {
                reportError("ResetSafety",
                    QCoreApplication::translate(
                        "LaserOperationRegistry", "Confirm Reset is not available until the emergency stop is reset"));
            }
        },
        [safety = m_config.safetyController] { return safety && safety->canConfirmReset(); });

    APR_INFO("[LaserOperation] registered: StartProcess / PauseProcess / StopProcess / EmergencyStop / ResetSafety");
}