#include "Hardware/ToolpathExecutor.h"

#include "Hardware/IProcessingTarget.h"
#include "Log/AprLogger.h"

#include <QString>

ToolpathExecutor::ToolpathExecutor(IProcessingTarget* target, QObject* parent)
    : QObject(parent)
    , m_target(target)
{
    m_stepTimer.setInterval(5);
    connect(&m_stepTimer, &QTimer::timeout, this, &ToolpathExecutor::onStep);
}

ToolpathExecutor::~ToolpathExecutor()
{
    // 析构在 DLL 内执行，确保 m_points（std::vector）的释放调用 DLL 内 CRT
    m_stepTimer.stop();
}

bool ToolpathExecutor::loadToolpath(const HardwareToolpathPoint* points, size_t count)
{
    if (m_running)
    {
        APR_WARN("[ToolpathExecutor] loadToolpath rejected: execution in progress");
        return false;
    }

    // 参数校验：空指针或零长度视为无效
    if (!points || count == 0)
    {
        APR_WARN("[ToolpathExecutor] loadToolpath rejected: null points or zero count");
        return false;
    }

    // 内部拷贝到 std::vector，STL 仅存在于 DLL 内部
    m_points.assign(points, points + count);
    m_index = 0;
    APR_INFOF("[ToolpathExecutor] Toolpath loaded: %zu points", count);
    return true;
}

bool ToolpathExecutor::start(QString* errorOut)
{
    if (m_running)
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Toolpath execution already in progress");
        }
        return false;
    }

    if (m_points.empty())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("No toolpath loaded");
        }
        return false;
    }

    if (!m_target || !m_target->isConnected())
    {
        if (errorOut)
        {
            *errorOut = QStringLiteral("Laser is not connected");
        }
        return false;
    }

    m_running = true;
    m_index = 0;
    m_target->startProcessing();
    m_stepTimer.start();
    emit sigProgressUpdated(0.0);
    APR_INFO("[ToolpathExecutor] Started toolpath execution");
    return true;
}

void ToolpathExecutor::cancel()
{
    if (!m_running)
    {
        return;
    }

    m_stepTimer.stop();
    if (m_target)
    {
        m_target->stopProcessing();
    }
    finish(false);
}

bool ToolpathExecutor::isRunning() const
{
    return m_running;
}

double ToolpathExecutor::progress() const
{
    if (m_points.empty())
    {
        return 0.0;
    }
    return static_cast<double>(m_index) * 100.0 / static_cast<double>(m_points.size());
}

void ToolpathExecutor::onStep()
{
    if (!m_running || !m_target)
    {
        return;
    }

    if (m_index >= m_points.size())
    {
        finish(true);
        return;
    }

    // 逐点下发：先设置速度/功率，再移动至目标坐标
    const HardwareToolpathPoint& pt = m_points[m_index];
    m_target->setSpeed(pt.speedMmPerSec);
    m_target->setPower(pt.powerPercent);
    m_target->moveTo(pt.x, pt.y, pt.z);

    ++m_index;
    const double pct = progress();
    m_target->setProcessingProgress(pct);
    emit sigProgressUpdated(pct);

    if (m_index >= m_points.size())
    {
        finish(true);
    }
}

void ToolpathExecutor::finish(bool success)
{
    m_stepTimer.stop();
    m_running = false;

    if (m_target)
    {
        m_target->setProcessingProgress(success ? 100.0 : m_target->processingProgress());
        if (success)
        {
            m_target->stopProcessing();
        }
    }

    emit sigProgressUpdated(success ? 100.0 : progress());
    emit sigFinished(success);
    APR_INFOF("[ToolpathExecutor] Finished (success=%d)", success ? 1 : 0);
}
