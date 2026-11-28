#pragma once

/**
 * @file IUiCommandDispatcher.h
 * @brief 命令分发器抽象接口（从 UiLayoutBuilder.h 抽出）
 *
 * 适配现有 CommandCatalog + OperationBus 体系，同时允许单元测试注入假实现。
 * 抽为独立头是为了让消费者（如 WorkbenchMenuManager）不必包含具体的
 * UiLayoutBuilder 头。
 */

#include <QString>
#include <QVariantMap>

class IUiCommandDispatcher
{
public:
    virtual ~IUiCommandDispatcher() = default;

    /// 命令是否已注册
    virtual bool isCommandRegistered(const QString& commandId) const = 0;

    /// 分发命令（唯一入口）
    /// @param params 调用方给出的参数（如最近文件项的 path），实现负责透传到操作总线；
    ///        实现里的 enrichParams 只能补齐缺失项，不得整体覆盖。
    virtual void dispatch(const QString& commandId, const QVariantMap& params) = 0;

    /// 无参分发的便捷写法。非虚，只是转调上面那一条通路，不构成第二个入口。
    void dispatch(const QString& commandId)
    {
        dispatch(commandId, QVariantMap{});
    }
};
