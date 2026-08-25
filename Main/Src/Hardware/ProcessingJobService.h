#pragma once

/**
 * @file ProcessingJobService.h
 * @brief 加工作业服务：把「编译好的计划」跑在「装配好的设备」上。
 *
 * ==================== 它在链路里的位置 ====================
 *
 *   命令中枢（Laser_StartProcess / Pause / Stop / EmergencyStop）
 *        ↓
 *   本服务：安全门控 → 编译 → loadPlan → startPlan/startMark → 进度轮询
 *        ↓
 *   DeviceHost（已装配的设备与接口）
 *
 * ==================== 为什么不把这些塞进 DeviceHost ====================
 *
 * DeviceHost 回答的是「机器由哪些零件组成、当前安全状态如何」，
 * 生命周期跟着应用走。加工作业回答的是「这一次要加工什么、跑到哪了」，
 * 生命周期跟着一次点击走。两者混在一起的话，
 * 「设备在线但没有作业」与「作业结束但设备仍在线」这两个完全正常的状态
 * 就没法在一个对象里干净地表达，最后必然演化成一堆互相矛盾的布尔标志。
 *
 * ==================== 为什么运动卡与振镜卡要在这里收口 ====================
 *
 * 两者的执行 API 名字不同（startPlan / startMark、planProgress / markProgress）
 * 但语义一一对应。差异吸收在本服务内部的一个私有 runner 里，
 * 于是命令中枢那一侧只有一个「开始加工」，不需要判断机型。
 * 复合机（振镜 + XY 平台）同时提供两个面，此时优先用振镜 ——
 * 出光同步在振镜卡的硬件里，交给运动卡做会丢精度。
 *
 * ==================== 为什么头文件不含任何 Hardware 头 ====================
 *
 * 与 DeviceHost.h 同一条规矩：BUILD_HARDWARE=OFF 时整个 Main 仍要能编译。
 * 状态一律用 int / QString 表达（int 取值即 Hw::PlanState），
 * Hw:: 类型只出现在 #ifdef ENABLE_HARDWARE 的重载与 Impl 里。
 */

#include <memory>

#include <QObject>
#include <QString>

#include "ToolpathJobSpec.h"

class DeviceHost;

namespace Eg
{
    class SceneManager;
}

class LayerManager;

#ifdef ENABLE_HARDWARE
namespace Hw
{
    struct MotionPlanView;
}
#endif

/**
 * @brief 一次加工作业的状态机与执行者。
 *
 * 线程模型：全部在主线程。进度靠定时器轮询设备，而不是让适配器回调里改状态 ——
 * 适配器的回调线程不确定，而进度只影响显示，没必要为它引入跨线程同步。
 */
class ProcessingJobService : public QObject
{
    Q_OBJECT

public:
    /**
     * @param host 必须非空，且生命周期长于本服务（由 CompositionRoot 保证）
     */
    explicit ProcessingJobService(DeviceHost* host, QObject* parent = nullptr);
    ~ProcessingJobService() override;

    /**
     * @brief 编译并开始一次加工。
     * @param errorOut 失败原因，可直接展示给操作员
     *
     * 顺序刻意如此：**先安全门控，再编译**。
     * 反过来的话，门没关的情况下也会花几百毫秒去编译十万条指令，
     * 操作员看到的是「点了没反应，过一会儿才报门没关」。
     */
    bool startJob(const Eg::SceneManager& scene, const LayerManager* layers,
                  const ToolpathJobSpec& spec, QString& errorOut);

    /// 暂停 / 恢复 / 中止。状态不允许时返回 false 并给出原因，不静默忽略。
    bool pauseJob(QString& errorOut);
    bool resumeJob(QString& errorOut);
    bool abortJob(QString& errorOut);

    bool isRunning() const;
    bool isPaused() const;

    /// 当前（或最近一次）作业标识
    QString currentJobId() const;

    /// 最近一次编译的统计信息，无论成功失败
    ToolpathCompileResult lastCompileResult() const;

    double progressFraction() const;

    /// Hw::PlanState 的整数值 + 可读名
    int planState() const;
    QString planStateName() const;

    /**
     * @brief 立即向设备取一次进度并发出相应信号。
     *
     * 公开而不是私有槽：单元测试里没有事件循环，定时器不会响。
     * 测试需要「推进模拟时钟 → 手动 poll → 断言进度」这条确定性路径。
     */
    void pollProgress();

#ifdef ENABLE_HARDWARE
    /**
     * @brief 直接执行一份已经编译好的计划（跳过几何编译）。
     *
     * 给两类调用者用：
     *   - 单元测试：不需要造一整个 SceneManager 就能测作业状态机；
     *   - 将来的「加工文件回放」：计划已经序列化在磁盘上，无需重新编译。
     *
     * @warning view 指向的内存只需在本调用期间有效（同 loadPlan 的约定）。
     */
    bool startPlan(const Hw::MotionPlanView& view, const QString& jobId, QString& errorOut);
#endif

signals:
    /// 作业已下发并开始执行
    void jobStarted(const QString& jobId, int commandCount);

    /// 进度推进（只在 commandIndex 或 fraction 变化时发，不是每次轮询都发）
    void jobProgress(double fraction, int commandIndex, int commandCount);

    /// 作业结束。success=false 时 message 是可展示的失败原因
    void jobFinished(bool success, const QString& message);

    /// Hw::PlanState 变化
    void jobStateChanged(int state, const QString& stateName);

private slots:
    /// 安全裁决变为「不允许开工」时，正在跑的作业必须立刻暂停。
    void onSafetyVerdictChanged(bool canStartProcessing, const QString& firstViolation);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
