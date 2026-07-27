#pragma once
/**
 * @file IUndoService.h
 * @brief 撤销服务窄接口
 *
 * 提供撤销/重做能力，命令通过此接口管理撤销栈。
 */
#include <string>

class UndoCommand;

class IUndoService
{
public:
    virtual ~IUndoService() = default;

    virtual void push(UndoCommand* command) = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual std::string undoText() const = 0;
    virtual std::string redoText() const = 0;
};
