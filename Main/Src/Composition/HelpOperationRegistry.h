#pragma once

#include "OpRegistryTypes.h"

class OperationBus;
class QWidget;
class HelpDialogService;
class WorkbenchWindow;

/**
 * @class HelpOperationRegistry
 * @brief 帮助操作注册器 — 关于/设置/文档/快捷键等帮助操作
 *
 * 从 CoreOperationRegistry 拆分而来（2026-09-08）。
 */
class HelpOperationRegistry
{
public:
    explicit HelpOperationRegistry(OperationBus* bus, QWidget* parentWidget);

    void registerAll();

private:
    OperationBus* m_bus;
    QWidget* m_parentWidget;
};
