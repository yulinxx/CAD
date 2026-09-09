#include "CoreOperationRegistry.h"
#include "EditOperationRegistry.h"
#include "ViewOperationRegistry.h"
#include "AlgorithmOperationRegistry.h"
#include "HelpOperationRegistry.h"

CoreOperationRegistry::CoreOperationRegistry(OperationBus* bus,
    SceneEditService* editService,
    IUndoRedoManager* undoManager,
    Eg::EntityClipboard* clipboard,
    AlgorithmRunner* algorithmRunner,
    ViewportActionHub* viewportActionHub,
    UiStateCenter* stateCenter,
    LayerEditService* layerEditService,
    UnitManager* unitManager,
    QWidget* parentWidget,
    Ui::ViewCaptureService* captureService)
    : m_bus(bus)
    , m_editService(editService)
    , m_undoManager(undoManager)
    , m_clipboard(clipboard)
    , m_algorithmRunner(algorithmRunner)
    , m_viewportActionHub(viewportActionHub)
    , m_stateCenter(stateCenter)
    , m_layerEditService(layerEditService)
    , m_unitManager(unitManager)
    , m_parentWidget(parentWidget)
    , m_captureService(captureService)
{
}

void CoreOperationRegistry::registerAll()
{
    EditOperationRegistry editRegistry(m_bus, m_editService, m_undoManager, m_clipboard,
        m_algorithmRunner, m_viewportActionHub, m_stateCenter,
        m_layerEditService, m_unitManager, m_parentWidget, m_captureService);
    editRegistry.registerAll();

    ViewOperationRegistry viewRegistry(m_bus, m_viewportActionHub, m_stateCenter,
        m_layerEditService, m_unitManager, m_captureService, m_parentWidget);
    viewRegistry.registerAll();

    AlgorithmOperationRegistry algoRegistry(m_bus, m_algorithmRunner, m_parentWidget);
    algoRegistry.registerAll();

    HelpOperationRegistry helpRegistry(m_bus, m_parentWidget);
    helpRegistry.registerAll();
}

void CoreOperationRegistry::registerHelpOperations() {}
void CoreOperationRegistry::registerEditOperations() {}
void CoreOperationRegistry::registerAlgorithmOperations() {}
void CoreOperationRegistry::registerViewOperations() {}
