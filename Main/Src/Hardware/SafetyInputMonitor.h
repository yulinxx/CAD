#pragma once

/**
 * @file SafetyInputMonitor.h
 * @brief 双通道安全输入的最小监测抽象。
 *
 * ==================== 本层只做监测与请求，安全功能由硬回路承担 ====================
 *
 * 软件永远不可能比继电器快，也不该被当成安全功能。本层存在的意义是：
 *   - 把「两路安全触点读数不一致」这类**硬回路已经失效**的征兆暴露出来，
 *     让操作员在真正出事故之前看到报警；
 *   - 在软件侧发起关光请求，并在反馈通道上核对请求是否真的被执行。
 *
 * 真正的切断仍然由急停继电器/安全门开关串联的硬回路完成。
 * 因此本层不做任何「软件允许出光所以就安全」的判断，
 * 也不替代 SafetyMonitor 的安全条件判定。
 *
 * ==================== 交叉校验规则 ====================
 *
 * 同一安全功能由两路独立触点输入（常闭，串在硬回路里）。两路读数必须一致：
 *   - 一路闭合一路断开 → 触点焊接、断线、或接线错误，判为交叉校验故障；
 *   - 两路都断开 → 安全需求成立，请求关光；
 *   - 请求关光后反馈通道仍在有效电平时限内不消失 → 请求未被真正执行，判为反馈故障。
 *
 * 头文件不包含 Hardware 头：BUILD_HARDWARE=OFF 时 Main 仍要能编译。
 */

#include <QString>

class SafetyInputMonitor
{
public:
    /// 监测结论。
    enum class State
    {
        NotConfigured = 0,   ///< 未配置双通道，本层不介入
        Normal = 1,
        DemandActive = 2,    ///< 安全需求成立（两路都断开），已请求关光
        CrossCheckFault = 3, ///< 两路读数不一致
        FeedbackFault = 4,   ///< 关光请求发出但反馈未响应
    };

    /// 反馈通道应当在请求关光后多久内消失；超过即判为反馈故障。
    static constexpr int kFeedbackTimeoutMs = 200;

    /**
     * @brief 配置双通道与反馈。
     * @param channelAPoint 第一路输入的逻辑点位名，空串表示不启用本层
     * @param channelBPoint 第二路输入的逻辑点位名
     * @param feedbackPoint 关光执行反馈的逻辑点位名；空串表示不做反馈校验
     */
    void configure(const QString& channelAPoint, const QString& channelBPoint, const QString& feedbackPoint);

    bool isConfigured() const;

    /**
     * @brief 评估一拍。
     * @param channelAActive 第一路读数（已按 activeLow 归一，true = 安全需求成立）
     * @param channelBActive 第二路读数
     * @param feedbackActive 反馈读数（true = 关光尚未执行到位）
     * @param nowMs          单调时间基准（毫秒）
     */
    State evaluate(bool channelAActive, bool channelBActive, bool feedbackActive, qint64 nowMs);

    /// 本层是否要求上层立即关光。任何故障态都为真。
    bool laserOffRequested() const;

    /// 最近一次判定的可读原因，供日志与界面展示。
    QString lastReason() const;

    /// 清空内部状态（重新配置或设备重启后调用）。
    void reset();

    static QString stateName(State state);

private:
    QString m_channelA;
    QString m_channelB;
    QString m_feedback;

    State m_state = State::NotConfigured;
    QString m_lastReason;

    bool m_requestOff = false;
    bool m_requestIssued = false;
    qint64 m_requestAtMs = 0;
};
