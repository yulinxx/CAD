#pragma once

/**
 * @file LaserOperationRegistry.h
 * @brief 把加工命令接到命令总线：Laser_StartProcess / PauseProcess / StopProcess / EmergencyStop。
 *
 * ==================== 为什么单独一个注册器 ====================
 *
 * 与 FileOperationRegistry / CoreOperationRegistry 同一层次的东西。
 * 加工命令的依赖（DeviceHost + ProcessingJobService）和文件/编辑命令完全不相交，
 * 塞进现有注册器只会让那两个类多背两个它们用不到的指针。
 *
 * ==================== 为什么错误提示走回调而不是直接弹框 ====================
 *
 * 注册器里若直接 QMessageBox，单元测试跑到「安全门拦住」这条分支时
 * 会弹出一个没人点的模态框，测试直接挂住。
 * 因此错误出口做成 std::function：生产环境注入弹框，测试注入收集器。
 *
 * ==================== 为什么作业规格也走回调 ====================
 *
 * 「这一次用什么工艺参数」的来源现在还没定（按图层取参数那条链路是断的，
 * 见 ToolpathJobSpec.h 的说明）。做成回调后：今天由 CompositionRoot 给一份默认值，
 * 将来接上参数面板或 HardwareProfileManager 时只换这个回调，
 * 命令注册与作业服务都不用动。
 */

#include <functional>

#include <QString>

#include "../Hardware/ToolpathJobSpec.h"

class OperationBus;
class DeviceHost;
class ProcessingJobService;
class LayerManager;

namespace Eg
{
    class SceneManager;
}

/**
 * @brief 加工命令注册器的依赖。
 */
struct LaserOperationConfig
{
    OperationBus* bus = nullptr;
    DeviceHost* deviceHost = nullptr;
    ProcessingJobService* jobService = nullptr;
    Eg::SceneManager* sceneManager = nullptr;
    LayerManager* layerManager = nullptr;

    /// 作业规格来源。为空时使用 ToolpathJobSpec 的默认值
    std::function<ToolpathJobSpec()> specProvider;

    /// 失败提示出口。为空时只写日志
    std::function<void(const QString& message)> errorReporter;
};

/**
 * @brief 注册加工相关操作。
 *
 * 生命周期：本对象必须活得比 OperationBus 长 —— 注册进去的 lambda 捕获了 this。
 * 由 ApplicationCompositionRoot 以 unique_ptr 持有（与 FileOperationRegistry 一致）。
 */
class LaserOperationRegistry
{
public:
    explicit LaserOperationRegistry(const LaserOperationConfig& config);

    void registerAll();

private:
    /// 统一的失败出口：写日志 + 走 errorReporter
    void reportError(const char* tag, const QString& message) const;

    /// 组装本次作业的规格（走 specProvider，缺省时给默认值）
    ToolpathJobSpec buildSpec() const;

    LaserOperationConfig m_config;
};
