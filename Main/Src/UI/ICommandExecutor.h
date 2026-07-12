#pragma once
/**
 * @file ICommandExecutor.h
 * @brief 命令执行器窄接口
 *
 * 命令处理器只需要知道如何执行命令，不需要访问完整的服务集合。
 */
#include <string>

class ICommandExecutor
{
public:
    virtual ~ICommandExecutor() = default;

    virtual void executeCommand(const std::string& commandId) = 0;
    virtual void submitCommand() = 0;
    virtual void cancelCommand() = 0;
    virtual bool hasActiveCommand() const = 0;
    virtual std::string activeCommandId() const = 0;
};
