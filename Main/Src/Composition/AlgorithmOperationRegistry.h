#pragma once

#include "OpRegistryTypes.h"

class OperationBus;
class AlgorithmRunner;
class QWidget;

/**
 * @class AlgorithmOperationRegistry
 * @brief 算法操作注册器 — 填充/偏移/阵列/布尔运算等算法操作
 *
 * 从 CoreOperationRegistry 拆分而来（2026-09-08）。
 */
class AlgorithmOperationRegistry
{
public:
    AlgorithmOperationRegistry(OperationBus* bus, AlgorithmRunner* algorithmRunner, QWidget* parentWidget);

    void registerAll();

private:
    OperationBus* m_bus;
    AlgorithmRunner* m_algorithmRunner;
    QWidget* m_parentWidget;
};
