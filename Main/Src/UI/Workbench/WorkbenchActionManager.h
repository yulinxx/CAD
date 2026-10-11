#pragma once

#include <vector>
#include <functional>

#include <QString>

#include "Services/UiFrameworkServices.h"

class QShortcut;
class UiStateCenter;
class OperationBus;

/// 工作台操作管理器：管理快捷键、命令权限检查、错误上报、性能记录
/// 从 WorkbenchWindow 中拆分，遵循单一职责原则
/// 负责：快捷键生命周期、命令执行前的权限检查、框架级错误/性能通道
class WorkbenchActionManager
{
public:
    explicit WorkbenchActionManager();
    ~WorkbenchActionManager();

    // ==================== 服务注入 ====================

    /// 设置操作总线
    void setOperationBus(OperationBus* bus);
    /// 设置框架级服务（用于 error/perf/canExecute 回调）
    void setFrameworkServices(const UiFrameworkServices& services);
    /// 设置状态中心（用于错误兜底上报）
    void setStateCenter(UiStateCenter* stateCenter);

    // ==================== 快捷键管理 ====================

    /// 注册全局快捷键（由工作台调用，切换时自动清理）
    void registerShortcut(QShortcut* shortcut);
    /// 注销全局快捷键
    void unregisterShortcut(QShortcut* shortcut);
    /// 清理所有注册的快捷键
    void clearAllShortcuts();

    // ==================== 命令执行 ====================

    /// 命令执行前的统一权限检查
    bool canExecuteCommand(const QString& commandId, const QString& context) const;

    // ==================== 框架通道 ====================

    /// 记录性能耗时并统一走框架级入口
    void recordPerformance(const QString& scope, qint64 elapsedMs);
    /// 上报框架错误
    void reportFrameworkError(const QString& errorCode, const QString& message, const QString& context);

    // ==================== 数据访问 ====================

    OperationBus* operationBus() const
    {
        return m_operationBus;
    }

    /// 当前注册的快捷键数量（用于清理时的统计日志）
    int shortcutCount() const
    {
        return static_cast<int>(m_registeredShortcuts.size());
    }

private:
    /// 操作总线
    OperationBus* m_operationBus{ nullptr };
    /// 框架级服务桥接
    UiFrameworkServices m_frameworkServices;
    /// 状态中心（用于错误兜底上报）
    UiStateCenter* m_stateCenter{ nullptr };
    /// 注册的全局快捷键列表（由工作台注册，切换时统一清理）
    std::vector<QShortcut*> m_registeredShortcuts;
};