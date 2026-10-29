#pragma once

#include <memory>

class UiStateCenter;
class ImportService;
class ImportDispatcher;
class ExportService;
class ExportDispatcher;
class SceneEditService;
class LayerManager;
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

/// 导入/导出服务装配（P2-10 从组合根提取）：
/// 读取器/写入器注册 + 服务注入 + 进度信号接线。纯装配逻辑，无自身生命周期。
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
        PersistenceService* persistence,
        Ui::ViewCaptureService* captureService);

    void setup(UiServices& uiServices);

private:
    void registerReaders();
    void registerWriters();
    void wireCallbacks();
    void connectProgressSignals();

    UiStateCenter* m_stateCenter;
    ImportService* m_importService;
    ImportDispatcher* m_importDispatcher;
    ExportService* m_exportService;
    ExportDispatcher* m_exportDispatcher;
    Eg::SceneManager* m_sceneManager;
    Eg::SceneManager3D* m_sceneManager3D;
    SceneEditService* m_editService;
    LayerManager* m_layerManager;
    PersistenceService* m_persistence;
    Ui::ViewCaptureService* m_captureService;
};
