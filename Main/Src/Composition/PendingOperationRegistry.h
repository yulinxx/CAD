#pragma once

class OperationBus;

/**
 * @brief 占位操作注册器 — 为尚未接入的算法/编辑/视图操作注册占位 LambdaOperation
 *
 * 占位策略：注册 LambdaOperation 打印警告，避免菜单/工具栏点击时静默无响应。
 * 接入真实实现后从各分类数组中移除；长期不实现的应从 OperationId 枚举中删除。
 */
class PendingOperationRegistry
{
public:
    explicit PendingOperationRegistry(OperationBus* bus);

    /// 注册所有占位操作
    void registerAll();

private:
    OperationBus* m_bus;
};
