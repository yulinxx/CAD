#include "WorkbenchActionManager.h"
#include "Services/UiFrameworkServices.h"
#include "UiStateCenter.h"
#include "UI2D/Operation/OperationBus.h"

#include <QShortcut>

WorkbenchActionManager::WorkbenchActionManager() = default;
WorkbenchActionManager::~WorkbenchActionManager() = default;

// ==================== 服务注入 ====================

void WorkbenchActionManager::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void WorkbenchActionManager::setFrameworkServices(const UiFrameworkServices& services)
{
    m_frameworkServices = services;
}

void WorkbenchActionManager::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

// ==================== 快捷键管理 ====================

void WorkbenchActionManager::registerShortcut(QShortcut* shortcut)
{
    if (shortcut)
    {
        m_registeredShortcuts.push_back(shortcut);
    }
}

void WorkbenchActionManager::unregisterShortcut(QShortcut* shortcut)
{
    auto it = std::find(m_registeredShortcuts.begin(), m_registeredShortcuts.end(), shortcut);
    if (it != m_registeredShortcuts.end())
    {
        m_registeredShortcuts.erase(it);
    }
}

void WorkbenchActionManager::clearAllShortcuts()
{
    for (auto* shortcut : m_registeredShortcuts)
    {
        delete shortcut;
    }
    m_registeredShortcuts.clear();
}

// ==================== 命令执行 ====================

bool WorkbenchActionManager::canExecuteCommand(const QString& commandId, const QString& context) const
{
    // 权限检查只做判定，不在这里扩展额外策略或副作用
    if (m_frameworkServices.canExecuteCommand)
    {
        return m_frameworkServices.canExecuteCommand(commandId, context);
    }

    // 没有权限回调时默认放行，保持框架的最小可用性
    return true;
}

// ==================== 框架通道 ====================

void WorkbenchActionManager::recordPerformance(const QString& scope, qint64 elapsedMs)
{
    if (m_frameworkServices.recordPerformance)
    {
        // 性能上报只负责透传耗时数据，不在这里做额外统计聚合
        m_frameworkServices.recordPerformance(scope, elapsedMs);
    }
}

void WorkbenchActionManager::reportFrameworkError(
    const QString& errorCode, const QString& message, const QString& context)
{
    // 错误统一走框架通道；如果没有通道，至少落到状态中心元数据里
    if (m_frameworkServices.reportError)
    {
        m_frameworkServices.reportError(errorCode, message, context);
    }
    else if (m_stateCenter)
    {
        m_stateCenter->setMetadata({ { QStringLiteral("lastErrorCode"), errorCode },
            { QStringLiteral("lastErrorMessage"), message },
            { QStringLiteral("lastErrorContext"), context } });
    }
}