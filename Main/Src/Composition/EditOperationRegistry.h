#pragma once

#include "OpRegistryTypes.h"
#include "UI/Service/ViewCaptureService.h"

class OperationBus;
class SceneEditService;
class IUndoRedoManager;
class AlgorithmRunner;
class ViewportActionHub;
class UiStateCenter;
class LayerEditService;
class UnitManager;
class QWidget;

/**
 * @class EditOperationRegistry
 * @brief 编辑操作注册器 — 复制/粘贴/变换/群组/修剪/延伸/贝塞尔等编辑操作
 *
 * 从 CoreOperationRegistry 拆分而来（2026-09-08），用于缩小单一注册器体积。
 */
class EditOperationRegistry
{
public:
    EditOperationRegistry(OperationBus* bus,
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

    void registerAll();

private:
    void registerClipboardOps();
    void registerTransformOps();
    void registerGroupOps();
    void registerTrimExtendOps();
    void registerBboxOps();
    void registerDiscretizeOp();
    void registerBezierOps();
    void registerArrayOp();

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
