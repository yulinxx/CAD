#pragma once
/**
 * @file TransactionUndoCommand.h
 * @brief 事务级撤销命令
 *
 * 将 DocumentTransaction 包装为 UndoCommand，使撤销/重做基于事务回放而非图元快照。
 * 使用方式：
 * @code
 *   DocumentTransaction txn(sceneManager);
 *   txn.begin();
 *   txn.addEntity(...);
 *   txn.commit();
 *   undoStack->push(new TransactionUndoCommand("绘制线段", std::move(txn)));
 * @endcode
 */
#include "IUndoService.h"
#include "Engine2D/Core/DocumentTransaction.h"

/**
 * @class TransactionUndoCommand
 * @brief 包装 DocumentTransaction 的 UndoCommand
 *
 * 撤销时回滚事务，重做时重放事务。
 * 替代 SnapshotUndoCommand 的快照模式，实现真正的事务级撤销。
 */
class TransactionUndoCommand : public UndoCommand
{
public:
    TransactionUndoCommand(const QString& text, Eg::DocumentTransaction txn)
        : UndoCommand(text)
        , m_transaction(std::move(txn))
    {
    }

    void undo() override
    {
        m_transaction.undo();
    }

    void redo() override
    {
        m_transaction.redo();
    }

    Eg::DocumentTransaction& transaction()
    {
        return m_transaction;
    }

private:
    Eg::DocumentTransaction m_transaction;
};