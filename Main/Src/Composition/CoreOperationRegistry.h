/**
 * @file CoreOperationRegistry.h
 * @brief 核心操作注册表
 */
#pragma once

class OperationBus;
class SceneEditService;
class IUndoRedoManager;
class QWidget;

class CoreOperationRegistry
{
public:
    CoreOperationRegistry(OperationBus* bus,
        SceneEditService* editService,
        IUndoRedoManager* undoManager,
        QWidget* parentWidget);

public:
    void registerAll();

private:
    void registerHelpOperations();
    void registerEditOperations();

private:
    OperationBus* m_bus;
    SceneEditService* m_editService;
    IUndoRedoManager* m_undoManager;
    QWidget* m_parentWidget;
};
