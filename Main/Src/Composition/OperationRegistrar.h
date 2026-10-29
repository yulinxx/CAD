#pragma once

#include <memory>

class ApplicationCompositionRoot;
class AlgorithmRunner;
class FileOperationRegistry;
class LaserOperationRegistry;

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
    ApplicationCompositionRoot& m_root;
    AlgorithmRunner* m_algorithmRunner{ nullptr };
    std::unique_ptr<FileOperationRegistry> m_fileOperationRegistry;
    std::unique_ptr<LaserOperationRegistry> m_laserOperationRegistry;
};
