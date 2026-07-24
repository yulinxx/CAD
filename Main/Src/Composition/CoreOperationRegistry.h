#pragma once

class OperationBus;
class SceneEditService;
class IUndoRedoManager;
class HelpDialogService;
class QWidget;

class CoreOperationRegistry
{
public:
    CoreOperationRegistry(OperationBus* bus,
                          SceneEditService* editService,
                          IUndoRedoManager* undoManager,
                          HelpDialogService* helpDialog,
                          QWidget* parentWidget);

    void registerAll();

private:
    OperationBus* m_bus;
    SceneEditService* m_editService;
    IUndoRedoManager* m_undoManager;
    HelpDialogService* m_helpDialog;
    QWidget* m_parentWidget;
};
