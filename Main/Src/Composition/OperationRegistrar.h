#pragma once

#include <memory>

class ApplicationCompositionRoot;
class AlgorithmRunner;
class FileOperationRegistry;
class LaserOperationRegistry;

class HardwareProfileManager;
class LayerManager;
struct ToolpathJobSpec;

namespace Eg
{
    struct MachineFrameConfig;
}

/**
 * @brief 加工启动时构造作业规格的真实取参路径。
 *
 * 图层基础参数：文档 ProcessData（.apr v2 回灌）优先，否则回退 HardwareProfile。
 * 穿孔等工艺细节：只存在于文档 ProcessModel 里，按图层解析后填入；
 * 没有细节的图层不填，保持「继承/关闭」，绝不用默认值填充。
 *
 * 独立成函数是为了让测试能走真实取参路径 —— 只测 MotionPlanCompiler
 * 的静态调用发现不了「入口没接线」。
 */
ToolpathJobSpec buildToolpathJobSpec(const LayerManager* layerManager,
    const Eg::MachineFrameConfig& frame);

/**
 * @class OperationRegistrar
 * @brief 操作注册器（P2-10 从组合根提取）：把各模块操作注册到 OperationBus。
 *
 * 持有 FileOperationRegistry / LaserOperationRegistry：它们注册进总线的 lambda
 * 捕获了注册表 this（见 LaserOperationRegistry.h 的生命周期说明），
 * 注册表必须比总线的派发更长寿 —— 组合根把本对象声明为最后一个成员（最先析构），
 * 与此前两注册表位于组合根成员末位的时序一致。
 */
class OperationRegistrar
{
public:
    /// @param root 组合根（借用；本对象由组合根持有，生命周期不会超出）
    /// @param algorithmRunner 惰性创建的算法执行器（组合根先行创建后传入）
    OperationRegistrar(ApplicationCompositionRoot& root, AlgorithmRunner* algorithmRunner);

    /// 析构必须在 .cpp 定义：成员是注册表的 unique_ptr，销毁需要完整类型
    ~OperationRegistrar();

    /// 注册核心/文件/加工/占位操作到 OperationBus
    void registerAll();

private:
    /// 图层 → 工艺 profile 映射的参数仓库；注入 LayerManager 后
    /// layerHardwareProfile() 才能返回非空值，specProvider 才能构造 ToolpathJobSpec.layerParams
    std::unique_ptr<HardwareProfileManager> m_hwProfileManager;

    ApplicationCompositionRoot& m_root;
    AlgorithmRunner* m_algorithmRunner{ nullptr };
    std::unique_ptr<FileOperationRegistry> m_fileOperationRegistry;
    std::unique_ptr<LaserOperationRegistry> m_laserOperationRegistry;
};
