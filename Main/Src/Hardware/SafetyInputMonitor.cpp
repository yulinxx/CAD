/**
 * @file SafetyInputMonitor.cpp
 * @brief 双通道安全输入监测实现
 */
#include "SafetyInputMonitor.h"

#include <QStringList>

QString SafetyInputMonitor::stateName(State state)
{
    switch (state)
    {
    case State::NotConfigured:   return QStringLiteral("NotConfigured");
    case State::Normal:          return QStringLiteral("Normal");
    case State::DemandActive:    return QStringLiteral("DemandActive");
    case State::CrossCheckFault: return QStringLiteral("CrossCheckFault");
    case State::FeedbackFault:   return QStringLiteral("FeedbackFault");
    }
    return QStringLiteral("Unknown");
}

void SafetyInputMonitor::configure(
    const QString& channelAPoint, const QString& channelBPoint, const QString& feedbackPoint)
{
    m_channelA = channelAPoint;
    m_channelB = channelBPoint;
    m_feedback = feedbackPoint;
    reset();
}

bool SafetyInputMonitor::isConfigured() const
{
    return !m_channelA.isEmpty() && !m_channelB.isEmpty();
}

void SafetyInputMonitor::reset()
{
    m_state = isConfigured() ? State::Normal : State::NotConfigured;
    m_lastReason.clear();
    m_requestOff = false;
    m_requestIssued = false;
    m_requestAtMs = 0;
}

SafetyInputMonitor::State SafetyInputMonitor::evaluate(
    bool channelAActive, bool channelBActive, bool feedbackActive, qint64 nowMs)
{
    if (!isConfigured())
    {
        m_state = State::NotConfigured;
        m_lastReason.clear();
        m_requestOff = false;
        return m_state;
    }

    // 两路读数不一致：硬回路的双通道已经不同步，属于必须停机的故障
    if (channelAActive != channelBActive)
    {
        m_state = State::CrossCheckFault;
        m_lastReason = QStringLiteral("Safety inputs disagree (%1=%2, %3=%4)")
                           .arg(m_channelA)
                           .arg(channelAActive ? QStringLiteral("active") : QStringLiteral("inactive"))
                           .arg(m_channelB)
                           .arg(channelBActive ? QStringLiteral("active") : QStringLiteral("inactive"));
        m_requestOff = true;
        return m_state;
    }

    if (channelAActive && channelBActive)
    {
        m_state = State::DemandActive;
        m_lastReason = QStringLiteral("Safety demand active on %1 / %2").arg(m_channelA, m_channelB);
        m_requestOff = true;
        if (!m_requestIssued)
        {
            m_requestIssued = true;
            m_requestAtMs = nowMs;
        }
        return m_state;
    }

    // 两路都已恢复：检查上一次关光请求是否真的被执行
    if (m_requestIssued)
    {
        if (feedbackActive && !m_feedback.isEmpty())
        {
            if (nowMs - m_requestAtMs > kFeedbackTimeoutMs)
            {
                m_state = State::FeedbackFault;
                m_lastReason = QStringLiteral("Laser-off feedback '%1' still active %2ms after request")
                                   .arg(m_feedback)
                                   .arg(nowMs - m_requestAtMs);
                m_requestOff = true;
                return m_state;
            }
            // 仍在时限内，继续等待反馈消失
            m_state = State::DemandActive;
            m_lastReason = QStringLiteral("Waiting for laser-off feedback '%1'").arg(m_feedback);
            m_requestOff = true;
            return m_state;
        }
        m_requestIssued = false;
    }

    m_state = State::Normal;
    m_lastReason.clear();
    m_requestOff = false;
    return m_state;
}

bool SafetyInputMonitor::laserOffRequested() const
{
    return m_requestOff;
}

QString SafetyInputMonitor::lastReason() const
{
    return m_lastReason;
}
