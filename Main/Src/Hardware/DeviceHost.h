#pragma once

/**
 * @file DeviceHost.h
 * @brief 硬件装配层：把 HAL 的「一堆零件」组装成一台可用的机器。
 *
 * ==================== 它负责什么 ====================
 *
 * HAL（Hardware 子模块）刻意只提供零件，不提供装配：
 *   - IDevice 由 DeviceRegistry 按字符串 ID 创建，但谁来 open、用什么参数？
 *   - IoPointMap 需要 attach 一个 IIoModule，但从哪台设备上取？
 *   - SafetyMonitor 需要一个 ISafetyActuator，但「切断出光」到底调哪台设备？
 *   - IoPointMap::poll / SafetyMonitor::evaluate 需要被周期驱动，
 *     而 HAL 刻意不起线程、不用 QTimer。
 *
 * 这些都是**装配问题**，答案随客户机器而变，因此属于宿主。本类就是那个宿主。
 *
 * ==================== 它是 POD 世界与 Qt 世界的边界 ====================
 *
 * HAL 用纯虚 sink 回调上报事件（跨 DLL 安全），且约定回调可能发生在
 * 适配器的工作线程里。本类把这些回调转成 Qt 信号并保证在主线程发出，
 * 于是 UI 侧可以像用普通 QObject 一样接。
 *
 * ==================== 为什么本头文件不包含任何 Hardware 头 ====================
 *
 * BUILD_HARDWARE 是可选模块。若头文件里出现 Hw:: 类型，
 * 关掉硬件模块时整个 Main 就编不过了。所以状态一律用 Qt/std 类型表达，
 * 真正的设备指针藏在 Impl 里（pimpl），实现文件按 ENABLE_HARDWARE 分两套。
 * 关掉硬件时 start() 返回 false 并给出明确原因，而不是假装成功。
 */

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>

#include "MachineProfile.h"

class DeviceHost : public QObject
{
    Q_OBJECT

public:
    explicit DeviceHost(QObject* parent = nullptr);
    ~DeviceHost() override;

    /// 本次构建是否编入了硬件支持（BUILD_HARDWARE）。false 时 start() 必然失败。
    static bool isHardwareSupportCompiled();

    /**
     * @brief 按档案装配并启动。
     * @param errorOut 失败原因（可直接展示给用户）
     *
     * 步骤：注册内置设备 → 按 deviceId 创建 → open(参数) →
     * attach IO 点位表 → 装配安全条件 → 启动 tick 定时器。
     * 任何一步失败都会回滚（销毁已创建的设备），不留半装配状态。
     */
    bool start(const MachineProfile& profile, QString& errorOut);

    /// 停止并释放设备。会先关光、把输出置安全态（由适配器的 close() 保证）。
    void stop();

    bool isRunning() const;

    /// 当前设备 ID；未启动时为空。
    QString deviceId() const;

    /// 设备显示名（厂商 + 型号），供状态栏/关于对话框使用。
    QString deviceDisplayName() const;

    /// 当前跑的是否为模拟设备。界面上必须能明确区分，避免把模拟当真机。
    bool isSimulated() const;

    /// 最近一次安全评估是否允许开工。未启动时为 false（fail-safe）。
    bool canStartProcessing() const;

    /// 最近一次评估中被违反的条件描述列表。
    QStringList safetyViolations() const;

public slots:
    /**
     * @brief 请求急停。
     *
     * 无前置状态检查：未启动也可调用（此时只记日志）。
     * 急停路径上任何「先判断再执行」都是隐患。
     */
    void requestEmergencyStop();

signals:
    /**
     * @brief 设备事件（已转到主线程）。
     * @param eventType Hw::DeviceEventType 的整数值
     * @param channel   轴号 / IO 通道号，不适用时 -1
     */
    void deviceEventReceived(int eventType, int channel, double value, const QString& message);

    /// 安全裁决发生变化时发出（只在变化时发，不是每个 tick 都发）。
    void safetyVerdictChanged(bool canStartProcessing, const QString& firstViolation);

    /// 设备连接状态变化（Hw::DeviceState 的整数值 + 可读名）。
    void deviceStateChanged(int state, const QString& stateName);

private slots:
    /// tick 定时器：推进模拟时钟 → 轮询 IO → 评估安全。
    void onTick();

    /// 设备事件的主线程落点，由事件 sink 用 QueuedConnection 投递过来。
    void onDeviceEventQueued(int eventType, int channel, double value, const QString& message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
