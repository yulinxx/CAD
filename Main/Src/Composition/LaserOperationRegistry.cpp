#include "LaserOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"

#include "Log/SyLogger.h"

#include "../Hardware/DeviceHost.h"
#include "../Hardware/ProcessingJobService.h"

LaserOperationRegistry::LaserOperationRegistry(const LaserOperationConfig& config)
    : m_config(config)
{
}

void LaserOperationRegistry::reportError(const char* tag, const QString& message) const
{
    SY_ERRORF("[LaserOperation] %s: %s", tag, message.toUtf8().constData());
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
        SY_ERRORF("[LaserOperation] No OperationBus, laser commands are not registered");
        return;
    }

    auto& reg = m_config.bus->registry();

    ProcessingJobService* job = m_config.jobService;
    DeviceHost* host = m_config.deviceHost;

    // --- 开始加工 ---
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Laser_StartProcess,
        [this, job] {
            if (!job || !m_config.sceneManager)
            {
                reportError("StartProcess", QStringLiteral("加工服务未装配，无法开始加工"));
                return;
            }
            QString error;
            if (!job->startJob(*m_config.sceneManager, m_config.layerManager, buildSpec(), error))
            {
                reportError("StartProcess", error);
            }
        },
        [job, host] {
            // canExec 决定菜单/工具栏的可用态：设备没起来、安全门没关、
            // 或者已经在加工时，「开始加工」必须是灰的 ——
            // 让操作员点了之后才看到报错，是最容易被投诉的交互
            return job && host && host->isRunning() && host->canStartProcessing()
                && !job->isRunning() && !job->isPaused();
        }));

    // --- 暂停 / 恢复（同一个命令来回切） ---
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Laser_PauseProcess,
        [this, job] {
            if (!job)
            {
                reportError("PauseProcess", QStringLiteral("加工服务未装配"));
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
            return job && (job->isRunning() || job->isPaused());
        }));

    // --- 停止加工 ---
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Laser_StopProcess,
        [this, job] {
            if (!job)
            {
                reportError("StopProcess", QStringLiteral("加工服务未装配"));
                return;
            }
            QString error;
            if (!job->abortJob(error))
            {
                reportError("StopProcess", error);
            }
        },
        [job] {
            return job && (job->isRunning() || job->isPaused());
        }));

    // --- 急停 ---
    reg.registerOperation(std::make_unique<LambdaOperation>(
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
        }));

    SY_DEBUG("[LaserOperation] registered: StartProcess / PauseProcess / StopProcess / EmergencyStop");
}
