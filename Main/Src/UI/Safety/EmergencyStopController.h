#pragma once

/**
 * @file EmergencyStopController.h
 * @brief 急停的界面状态机：触发、物理复位校验、复位确认与只读态。
 *
 * 安全策略：安全功能由硬回路承担，本类只负责界面状态与请求编排，
 * 不承担任何安全职能，也不能作为安全功能的唯一防线。
 *
 * 状态迁移：
 *   Normal --(触发急停)--> EmergencyStopped --(物理复位 + 复位确认)--> ResetConfirmed
 *   ResetConfirmed 之后仍需一次独立的开光/开始加工动作，复位确认不会自动开光。
 *
 * 进程重启：急停闭锁写入持久化存储，重启后直接进入 EmergencyStopped，
 * 不会自动回到可出光态。
 */

#include <functional>

#include <QObject>

class DeviceHost;

class EmergencyStopController : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Normal,
        EmergencyStopped,
        ResetConfirmed
    };
    Q_ENUM(State)

    explicit EmergencyStopController(QObject* parent = nullptr);

    /**
     * @brief 注入持久化读写，供测试替换真实的 QSettings。
     * @param loadLatch  返回上次退出时是否处于急停闭锁
     * @param saveLatch  写入急停闭锁状态
     */
    EmergencyStopController(
        std::function<bool()> loadLatch, std::function<void(bool)> saveLatch, QObject* parent = nullptr);
    ~EmergencyStopController() override;

    /// 绑定设备宿主：用于监听安全裁决与设备运行状态。可传 nullptr 表示无硬件。
    void attachDeviceHost(DeviceHost* host);

    /**
     * @brief 替换设备状态来源，供测试或无设备宿主场景使用。
     * @param monitoring   是否具备硬件监测能力
     * @param inputActive  急停硬输入是否处于按下态（读不到时由调用方返回 true）
     */
    void setDeviceStateProviders(std::function<bool()> monitoring, std::function<bool()> inputActive);

    State state() const;
    bool isReadOnly() const;
    bool isPhysicalResetObserved() const;

    /// 是否具备硬件监测能力（编入硬件支持且设备已启动）。
    bool isMonitoringAvailable() const;

    /// 当前是否允许执行复位确认：急停态下且已观测到物理复位（无监测时按无风险放行）。
    bool canConfirmReset() const;

    /// 触发急停：进入只读急停态并写入持久化闭锁。不弹二次确认。
    void triggerEmergencyStop();

    /// 执行复位确认：仅清除软件闭锁与状态，不会开光。
    bool confirmReset();

    /// 主动轮询设备输入；生产环境由内部定时器驱动，测试可直接调用。
    void pollDeviceState();

signals:
    void stateChanged(EmergencyStopController::State state);
    void physicalResetObservedChanged(bool observed);
    void monitoringAvailableChanged(bool available);

private:
    void setState(State state);
    void setPhysicalResetObserved(bool observed);
    void setMonitoringAvailable(bool available);

    DeviceHost* m_host{ nullptr };
    State m_state{ State::Normal };
    bool m_physicalResetObserved{ false };
    bool m_monitoringAvailable{ false };
    /// 触发后是否见过急停输入处于按下态，用于确认确实发生过一次物理复位。
    bool m_estopInputSeenActive{ false };
    std::function<bool()> m_loadLatch;
    std::function<void(bool)> m_saveLatch;
    std::function<bool()> m_monitoringProvider;
    std::function<bool()> m_inputActiveProvider;
};
