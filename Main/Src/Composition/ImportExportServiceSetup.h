#pragma once

#include <memory>

class UiStateCenter;
class ImportService;
class ImportDispatcher;
class ExportService;
class ExportDispatcher;
class SceneEditService;
class LayerManager;
class LayerPersistenceBridge;
class PersistenceService;

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

namespace Ui
{
    class ViewCaptureService;
}

struct UiServices;

class ImportExportServiceSetup
{
public:
    ImportExportServiceSetup(UiStateCenter* stateCenter,
        ImportService* importService,
        ImportDispatcher* importDispatcher,
        ExportService* exportService,
        ExportDispatcher* exportDispatcher,
        Eg::SceneManager* sceneManager,
        Eg::SceneManager3D* sceneManager3D,
        SceneEditService* editService,
        LayerManager* layerManager,
        LayerPersistenceBridge* layerPersistence,
        PersistenceService* persistence,
        Ui::ViewCaptureService* captureService);

    void setup(UiServices& uiServices);

private:
    void registerReaders();
    void registerWriters();
    void wireCallbacks();
    void connectProgressSignals(UiServices& uiServices);

    UiStateCenter* m_stateCenter;
    ImportService* m_importService;
    ImportDispatcher* m_importDispatcher;
    ExportService* m_exportService;
    ExportDispatcher* m_exportDispatcher;
    Eg::SceneManager* m_sceneManager;
    Eg::SceneManager3D* m_sceneManager3D;
    SceneEditService* m_editService;
    LayerManager* m_layerManager;
    LayerPersistenceBridge* m_layerPersistence;
    PersistenceService* m_persistence;
    Ui::ViewCaptureService* m_captureService;
};
