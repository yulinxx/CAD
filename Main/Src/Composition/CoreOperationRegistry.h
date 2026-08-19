/**
 * @file CoreOperationRegistry.h
 * @brief 核心操作注册表
 */
#pragma once

#include "UI/Service/ViewCaptureService.h"

class OperationBus;
class SceneEditService;
class IUndoRedoManager;

namespace Eg
{
    class EntityClipboard;
}

class AlgorithmRunner;
class ViewportActionHub;
class UiStateCenter;
class LayerEditService;
class UnitManager;
class QWidget;

class CoreOperationRegistry
{
public:
    CoreOperationRegistry(OperationBus* bus,
        SceneEditService* editService,
        IUndoRedoManager* undoManager,
        Eg::EntityClipboard* clipboard,
        AlgorithmRunner* algorithmRunner,
        ViewportActionHub* viewportActionHub,
        UiStateCenter* stateCenter,
        LayerEditService* layerEditService,
        UnitManager* unitManager,
        QWidget* parentWidget,
        Ui::ViewCaptureService* captureService = nullptr);

public:
    void registerAll();

private:
    void registerHelpOperations();
    void registerEditOperations();
    void registerAlgorithmOperations();
    void registerViewOperations();

private:
    OperationBus* m_bus;
    SceneEditService* m_editService;
    IUndoRedoManager* m_undoManager;
    Eg::EntityClipboard* m_clipboard;
    AlgorithmRunner* m_algorithmRunner;
    ViewportActionHub* m_viewportActionHub;
    UiStateCenter* m_stateCenter;
    LayerEditService* m_layerEditService;
    UnitManager* m_unitManager;
    QWidget* m_parentWidget{ nullptr };
    Ui::ViewCaptureService* m_captureService = nullptr;
};
