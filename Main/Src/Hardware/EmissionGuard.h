#pragma once

/**
 * @file EmissionGuard.h
 * @brief 出光使能生命周期状态机。
 *
 * ==================== 为什么需要这一层 ====================
 *
 * 振镜卡适配器（Scanlab / Ruida）在 startMark/startPlan 里强制要求
 * setEmissionEnabled(true) 已经打开，否则直接返回 Interlock。
 * 而全仓只有安全链在关光，没有任何地方在开工时打开使能，
 * 结果是「真机点开始加工必然失败」。
 *
 * 反过来，关光路径也不能散落在各处：安全链关光、作业结束关光、急停关光、
 * 故障关光，任何一条漏掉都意味着无人看管的出光。因此把「谁能开光、
 * 什么时候必须关光」收敛成一个显式状态机，而不是若干布尔标志。
 *
 * ==================== 关键约束 ====================
 *
 * 安全链一旦关光就进入 Latched（闭锁），**禁止自动恢复**：
 * 必须由操作员显式确认（confirmRearm）才允许再次开光。
 * 自动恢复的本意是「让机器自己爬起来」，而在出光场景里它等价于
 * 「门还开着，激光自己又亮了」。
 *
 * 头文件不包含任何 Hardware 头：BUILD_HARDWARE=OFF 时 Main 仍要能编译。
 */

#include <memory>

#include <QString>
#include <QStringList>

#ifdef ENABLE_HARDWARE
namespace Hw
{
    class ILaserSource;
}
#endif

/**
 * @brief 出光状态。
 *
 * Off      未开光，允许开工
 * Armed    已受理开工请求，正在落实硬件使能（瞬时态）
 * Enabled  已开光，加工作业进行中
 * Latched  闭锁：安全链/急停/故障关光，必须显式确认后方可再次开光
 */
enum class EmissionState
{
    Off = 0,
    Armed = 1,
    Enabled = 2,
    Latched = 3,
};

/// 状态的可读名（日志与断言用，不面向最终用户）
QString emissionStateName(EmissionState state);

/**
 * @brief 出光使能守卫。
 *
 * 线程约定：内部加锁保护状态，可由主线程与设备轮询回调调用；
 * 安全链关光不得被排队或丢弃，因此关光路径不做任何前置状态检查。
 */
class EmissionGuard
{
public:
    EmissionGuard();
    ~EmissionGuard();

    EmissionGuard(const EmissionGuard&) = delete;
    EmissionGuard& operator=(const EmissionGuard&) = delete;

    /**
     * @brief 绑定出光目标。传 nullptr 解除绑定。
     *
     * 指针归 DeviceHost 所有，本类不接管生命周期。
     */
#ifdef ENABLE_HARDWARE
    void attachLaser(Hw::ILaserSource* laser);
#else
    void attachLaser(void* laser);
#endif

    /// 当前状态。
    EmissionState state() const;

    /// 是否处于允许出光的状态（只有 Enabled 为真）。
    bool isEmissionPermitted() const;

    /**
     * @brief 开工：Off -> Armed -> Enabled。
     * @param errorOut 失败原因
     *
     * Latched 状态下一律拒绝，错误信息明确要求显式确认。
     */
    bool beginJob(QString& errorOut);

    /// 正常收工：Enabled/Armed -> Off。可重复调用。
    void endJob();

    /// 中止（用户点停止）：立即关光并回到 Off。
    void abort(const QString& reason);

    /// 安全链关光：立即关光并进入 Latched，禁止自动恢复。
    void safetyCutoff(const QString& reason);

    /// 急停：语义同安全链关光，只是原因不同。
    void emergencyStop(const QString& reason);

    /// 设备故障（过温/断线等）：立即关光并进入 Latched。
    void fault(const QString& reason);

    /**
     * @brief 显式确认并解除闭锁：Latched -> Off。
     *
     * 只解除闭锁，不开光；开光仍需走 beginJob。
     */
    bool confirmRearm(QString& errorOut);

    /// 设备停机后的强制复位：关光并回到 Off（含清除闭锁）。
    void reset();

    /// 状态迁移历史，格式 "旧状态>新状态:原因"，供测试断言固定次序。
    QStringList transitionLog() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
