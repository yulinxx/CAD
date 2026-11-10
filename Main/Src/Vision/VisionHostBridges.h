#pragma once

/**
 * @file VisionHostBridges.h
 * @brief 宿主侧对 Vis::HostBridges 的生产实现：位姿、平台运动、振镜打点、光源。
 *
 * ==================== 为什么在 Main 而不在 Vision ====================
 *
 * Vision 只声明"我需要宿主提供什么"，它不允许反向依赖 Hardware（根 CMake
 * 里两者同层，互相依赖成环）。装配点唯一且在 Main：本文件把 DeviceHost
 * 手里的 Hw::IMotionCard / Hw::IGalvoCard / Hw::IIoModule 包成 Vision 认识的
 * 四个接口，由 ApplicationCompositionRoot 在装配期注入。
 *
 * 未注入时 Vision 的标定与飞拍一律返回 NotSupported，本层的存在就是为了
 * 让那条路径真正可用，而不是把 NotSupported 换成静默降级。
 *
 * ==================== 线程约束 ====================
 *
 * - StagePoseProvider 的 poseAt 会被采集线程按帧时间戳调用，因此环形缓冲
 *   必须自持锁；采样在 GUI 线程的定时器里完成。
 * - 其余三个接口只在标定向导与作业准备阶段调用，均为 GUI 线程。
 * - 本文件不包含任何 Hardware 头：DeviceHost 的公开出口只在 ENABLE_HARDWARE
 *   下存在，所以实现文件按 ENABLE_HARDWARE 分两套。关掉硬件模块时本类
 *   仍然可构造，所有调用返回 NotSupported。
 */

#ifdef ENABLE_VISION

#include <array>
#include <cstdint>
#include <mutex>

#include <QObject>

#include "Vision/Calib/IWorkpieceTransformSink.h"
#include "Vision/Core/HostBridges.h"

class DeviceHost;
class SceneEditService;
class QTimer;
namespace Eg
{
class SceneManager;
}

namespace VisHost
{
    /**
     * @brief 位姿环形缓冲 + 按时间戳线性插值。
     *
     * 只保留最近 kHistoryCapacity 个采样：飞拍的回溯窗口是"从曝光到取图"的
     * 几百毫秒，更早的位姿没有使用场景，无限增长只会吃掉内存。
     */
    class PoseHistory
    {
    public:
        static constexpr int32_t kCapacity = 512;

        /// 追加重采样。时间戳必须单调递增；回退的时间戳被丢弃。
        void push(int64_t timestampUs, const Vis::Pose2D& pose);

        /// 最新一个采样。缓冲为空返回 false。
        bool latest(Vis::Pose2D* out) const;

        /// 指定时刻插值。超出缓冲覆盖范围返回 false，不返回边界近似值。
        bool at(int64_t timestampUs, Vis::Pose2D* out) const;

        void clear();

        int32_t count() const;

    private:
        struct Sample
        {
            int64_t timestampUs = 0;
            Vis::Pose2D pose;
        };

        mutable std::mutex m_mutex;
        std::array<Sample, kCapacity> m_samples{};
        int32_t m_count = 0;
        int32_t m_head = 0;  ///< 下一个写入位置
        int32_t m_tail = 0;  ///< 最旧样本位置
    };

    /**
     * @brief 用运动卡位置采样喂满 PoseHistory。
     *
     * 采样周期由定时器驱动，而不是在 poseAt 里现读：飞拍要求"曝光时刻"的位姿，
     * 事后现读只能得到"现在"，高速运动下可以差出几个毫米。
     */
    class StagePoseProvider : public QObject, public Vis::IStagePoseProvider
    {
        Q_OBJECT

    public:
        explicit StagePoseProvider(DeviceHost* host, QObject* parent = nullptr);
        ~StagePoseProvider() override;

        /// 启动采样。intervalMs <= 0 时按默认 5 ms。
        void start(int32_t intervalMs = 5);
        void stop();
        bool running() const;

        bool currentPose(Vis::Pose2D* out) const override;
        bool poseAt(int64_t timestampUs, Vis::Pose2D* out) const override;

    private slots:
        void onSample();

    private:
        DeviceHost* m_host = nullptr;
        PoseHistory m_history;
        bool m_running = false;
        int32_t m_intervalMs = 5;
        QTimer* m_timer = nullptr;
        bool m_axisMapped = false;
        int32_t m_axisX = 0;
        int32_t m_axisY = 1;
        int32_t m_axisRotary = -1;  ///< -1 表示没有旋转轴
    };

    /**
     * @brief 包 Hw::IMotionCard 的绝对定位与停稳等待。
     *
     * 只提供标定向导需要的两件事；插补、连续轨迹仍由 Main 的加工链调度。
     */
    class StageController : public Vis::IStageController
    {
    public:
        explicit StageController(DeviceHost* host);

        Vis::VisResult moveAbsolute(const Vis::Pose2D& target) override;
        Vis::VisResult waitIdle(int32_t timeoutMs) override;

    private:
        DeviceHost* m_host = nullptr;
    };

    /**
     * @brief 包 Hw::IGalvoCard 的打点能力。
     *
     * 每个点编译成 RapidTo -> LaserOn -> Dwell -> LaserOff 一条计划，
     * 一组点合成一份计划下发，避免逐点起停振镜。
     *
     * 标定点同样会真实出光，因此与正式加工共用同一条安全链：
     * DeviceHost::canStartProcessing() 门控 + EmissionGuard::beginJob/endJob
     * 生命周期（见 .cpp 里的 EmissionLease）。安全裁决不满足或出光已闭锁时
     * 返回 InvalidState，任何失败路径都不会把出光使能留在 Enabled。
     */
    class GalvoController : public Vis::IGalvoController
    {
    public:
        explicit GalvoController(DeviceHost* host);

        Vis::VisResult markPoint(double xMm, double yMm) override;
        Vis::VisResult markPattern(const Vis::PointF* points, int32_t count) override;

    private:
        Vis::VisResult markPoints(const Vis::PointF* points, int32_t count);

        DeviceHost* m_host = nullptr;
    };

    /**
     * @brief 包 Hw::IIoModule 的光源控制。
     *
     * 亮度与开关共用模拟量输出：亮度按通道量程把 0–100% 换算成工程量，
     * 开关关灯写量程下限、开灯恢复上一次亮度。光源直接挂在相机上时
     * 不使用本层，改注入相机的 ILightController 实现。
     */
    class LightController : public Vis::ILightController
    {
    public:
        explicit LightController(DeviceHost* host);

        int32_t channelCount() const override;
        Vis::VisResult setBrightness(int32_t channel, double percent) override;
        Vis::VisResult setEnabled(int32_t channel, bool enabled) override;

    private:
        /// 亮度按通道缓存：关灯写量程下限，开灯要恢复上一次亮度。
        static constexpr int32_t kMaxChannels = 64;

        DeviceHost* m_host = nullptr;
        std::array<double, kMaxChannels> m_percent{};
    };

    /**
     * @brief 把工件找正结果落到当前 2D 文档实体上。
     *
     * 变换通过 SceneEditService::transformEntities 提交，因此可撤销且与
     * 手工编辑走同一条撤销栈；revert 应用逆变换而不是调用 undo ——
     * apply 之后用户可能已经做过别的编辑，undo 会撤掉与视觉无关的操作。
     */
    class WorkpieceTransformSink : public Vis::IWorkpieceTransformSink
    {
    public:
        WorkpieceTransformSink(Eg::SceneManager* sceneManager, SceneEditService* editService);

        Vis::VisResult apply(const Vis::Sim2D& transform, Vis::Frame from, Vis::Frame to) override;
        Vis::VisResult revert() override;

    private:
        Eg::SceneManager* m_sceneManager = nullptr;
        SceneEditService* m_editService = nullptr;
        bool m_applied = false;
        Vis::Sim2D m_lastApplied;
    };

    /**
     * @brief 把五个桥对象登记到 Vision 的 C ABI 注册表。
     *
     * Vision 的 C++ 门面不导出符号，宿主只能填函数指针表 + user 指针。
     * 本函数在本文件内用桥对象的成员函数做 trampoline；指针为空的组不会注册，
     * 对应能力在 Vision 内返回 NotSupported。
     * @return Vision_SetHostBridges 的返回码为 0 时为 true。
     */
    bool registerBridges(StagePoseProvider* poseProvider,
                         StageController* stageController,
                         GalvoController* galvoController,
                         LightController* lightController,
                         WorkpieceTransformSink* transformSink);

    /// 清空 Vision 内的全部注入槽（析构桥对象之前必须调用）。
    void unregisterBridges();
}

#endif  // ENABLE_VISION
