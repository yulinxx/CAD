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
#include "EmissionGuard.h"
#include "SafetyInputMonitor.h"

#ifdef ENABLE_HARDWARE
// 只前向声明，不包含 Hardware 头：本文件在 BUILD_HARDWARE=OFF 时同样要能编译，
// 而前向声明足够让上层（加工作业服务）拿到带类型的接口指针。
namespace Hw
{
    class IDevice;
    class IMotionCard;
    class IGalvoCard;
    class ILaserSource;
    class IIoModule;
}
#endif

/// 报警来源。聚合后交给上层，避免 UI 去区分厂商细节。
enum class AlarmSource
{
    Motion = 0,  ///< 轴报警/限位
    Laser = 1,   ///< 激光器故障位
    Safety = 2,  ///< 双通道安全输入
};

/// 一条聚合后的报警事件（宿主侧 POD，不跨模块边界）。
struct AlarmEvent
{
    AlarmSource source = AlarmSource::Motion;
    int axis = -1;      ///< 轴号，不适用时 -1
    int code = 0;       ///< 机器可读码，日志/遥测用它而不是文案
    QString message;    ///< 给现场看的原因
};

/// 机器级状态。HAL 的 DeviceState 只有开/关/故障，宿主还需要表达「回零中/可开工」。
enum class MachineState
{
    Offline = 0,
    Opening = 1,
    Homing = 2,
    Ready = 3,
    Alarm = 4,
};


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

    /// 机器级状态（由宿主聚合轴/激光/安全输入得到）。
    MachineState machineState() const;
    QString machineStateName() const;

    /// 当前生效的报警列表（文案形式），供状态栏与排障面板使用。
    QStringList activeAlarms() const;

    /**
     * @brief 当前设备是否支持运行中暂停。
     *
     * 文件传输型设备（Ruida 生成 .rd 后离线执行）没有运行中控制通道，
     * 暂停按钮必须置灰而不是点了继续出光。这里给出明确声明，
     * UI 不得自行猜测。
     */
    bool supportsRuntimePause() const;

    /// 出光使能守卫。作业服务通过它落实开工/收工/关光次序。
    EmissionGuard& emissionGuard();

    /// 双通道安全输入的最近一次判定（只读，供状态栏展示）。
    SafetyInputMonitor::State safetyInputState() const;

    /**
     * @brief 显式确认解除出光闭锁。
     *
     * 安全链关光后必须由操作员确认才能再次开光；本函数只解闭锁，不开光。
     */
    bool acknowledgeEmissionLatch(QString& errorOut);

    /**
     * @brief 当前机器档案里的机器坐标系（台面尺寸 + 原点角点）
     *
     * 加工链路的**唯一坐标换算**依赖它：换算在 `MotionPlanCompiler` 的最后一步发生，
     * 配置由这里 → `ToolpathJobSpec::machineFrame` → 编译器一路传下去。
     *
     * 未启动设备时返回默认配置（恒等变换）。
     */
    Eg::MachineFrameConfig machineFrameConfig() const;

    /// 最近一次安全评估是否允许开工。未启动时为 false（fail-safe）。
    bool canStartProcessing() const;

    /// 最近一次评估中被违反的条件描述列表。
    QStringList safetyViolations() const;

    /**
     * @brief 急停硬输入当前是否处于按下态。
     *
     * 未启动设备或点位不可读时返回 true（fail-safe），
     * 调用方据此判断物理复位是否已完成，不得把「读不到」当成「已复位」。
     */
    bool isEmergencyStopInputActive() const;

    /// 软件侧急停闭锁是否仍生效。软件闭锁只用于提示与状态展示，不承担安全职能。
    /// 设备未启动时按下急停同样会置位（供 UI 显示「仍处闭锁」）。
    bool isSoftwareEmergencyActive() const;

    /// 清除软件侧急停闭锁。物理急停未释放时安全判定仍会禁止开工。
    /// 无前置条件：离线急停也必须能被显式复位，否则闭锁永远清不掉。
    void clearSoftwareEmergencyStop();

    /**
     * @brief 手动推进一拍：推进模拟时钟 → 轮询 IO → 评估安全 → 按需发信号。
     * @param elapsedMs 距上一拍的时长（毫秒）；<=0 时不推进模拟时钟，但仍会轮询与评估
     *
     * 公开而不是只有私有的定时器槽：单元测试里没有事件循环，QTimer 不会响，
     * 而模拟设备的时间完全靠宿主推进。测试需要
     * 「tick(50) → 断言状态」这条确定性路径，而不是 sleep 之后碰运气。
     * 生产路径仍由 tick 定时器调用，两者走的是同一段代码。
     */
    void tick(qint64 elapsedMs);


#ifdef ENABLE_HARDWARE
    /**
     * @name 已装配设备的接口出口
     *
     * 只在编入硬件支持时存在。返回的指针**归本类所有**，调用方不得 destroy，
     * 也不得跨 stop() 缓存 —— stop() 之后一律失效（返回 nullptr）。
     *
     * 之所以把接口指针暴露出来而不是在 DeviceHost 上再包一层加工 API：
     * 加工作业（loadPlan/startPlan/进度轮询）是一整套有状态的流程，
     * 塞进装配层会让本类同时负责「装配」和「作业调度」两件事。
     * 作业调度交给 ProcessingJobService，本类只回答「零件在哪」。
     *
     * 设备不具备某项能力时对应函数返回 nullptr，调用方必须判空 ——
     * 振镜一体机没有 IMotionCard，运动卡机型没有 IGalvoCard，这是常态而非异常。
     */
    ///@{
    Hw::IDevice* device() const;
    Hw::IMotionCard* motionCard() const;
    Hw::IGalvoCard* galvoCard() const;
    Hw::ILaserSource* laserSource() const;
    Hw::IIoModule* ioModule() const;
    ///@}

    /**
     * @brief 把一台独立激光器接入本机的设备组。
     *
     * 运动卡机型没有 ILaserSource 面，功率/频率/脉宽必须投递到独立的激光器设备上，
     * 否则这些参数会被静默丢弃。指针归调用方所有，本类不接管生命周期；
     * 传 nullptr 解除接入。
     */
    void attachLaser(Hw::ILaserSource* laser);

    /// 实际承担出光的激光器：接入的独立激光器优先，否则用设备自带的激光面。
    Hw::ILaserSource* routedLaserSource() const;

    /// 上述激光器的来源描述："attached" / "device" / "none"，仅用于日志。
    QString routedLaserOrigin() const;
#endif


public slots:
    /**
     * @brief 请求急停。
     *
     * 无前置状态检查：未启动也可调用。无论设备是否运行，都会立即关光、
     * 把出光守卫置为 Latched（须经 acknowledgeEmissionLatch 显式确认），
     * 并置上软件急停闭锁。急停路径上任何「先判断再执行」都是隐患。
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
    /// firstViolation 是给界面看的本地化文案，firstViolationPoint 是机器可读的点位 id
    /// （日志/遥测只能用后者，否则排查关键字会随部署语言变化）。
    void safetyVerdictChanged(
        bool canStartProcessing, const QString& firstViolation, const QString& firstViolationPoint);


    /// 设备连接状态变化（Hw::DeviceState 的整数值 + 可读名）。
    void deviceStateChanged(int state, const QString& stateName);

    /// 机器级状态变化（MachineState 的整数值 + 可读名），只在变化时发出。
    void machineStateChanged(int state, const QString& stateName);

    /**
     * @brief 新出现的报警。已恢复的报警不会重复上报。
     * @param source AlarmSource 的整数值
     */
    void alarmRaised(int source, int axis, int code, const QString& message);

    /**
     * @brief 设备即将停止（在销毁设备之前同步发出）。
     *
     * 后台下发线程必须在设备销毁前收到这个信号并退出，
     * 否则会拿着已释放的设备指针继续调用 SDK。
     */
    void deviceStopping();

private slots:
    /// tick 定时器：推进模拟时钟 → 轮询 IO → 评估安全。
    void onTick();

    /// 设备事件的主线程落点，由事件 sink 用 QueuedConnection 投递过来。
    void onDeviceEventQueued(int eventType, int channel, double value, const QString& message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
