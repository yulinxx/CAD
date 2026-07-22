#pragma once

#include <QString>
#include <QtGlobal>
#include <functional>

class UiStateCenter;

/**
 * @brief 框架级服务桥接接口
 *
 * 统一承载错误处理、权限判定、性能采样等横切能力，避免散落在各个 UI 组件中。
 */
struct UiFrameworkServices
{
    /// 状态中心
    UiStateCenter* stateCenter{ nullptr };
    /// 错误上报回调
    std::function<void(const QString& errorCode, const QString& message, const QString& context)> reportError;

    /// 命令权限判定回调
    std::function<bool(const QString& commandId, const QString& context)> canExecuteCommand;

    /// 性能记录回调
    std::function<void(const QString& scope, qint64 elapsedMs)> recordPerformance;
};
