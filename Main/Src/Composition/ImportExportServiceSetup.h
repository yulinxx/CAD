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
struct NativeDocumentSnapshot;

/// 采集原生 .apr 的加工语义快照：图层表、硬件 Profile 值集、每层工艺对象
/// （ProcessData）。导出与回归测试共用同一入口，避免测试里另写一份采集逻辑。
NativeDocumentSnapshot aprSetupCaptureMachiningSnapshot(LayerManager* layerManager);

/// 导入/导出装配上下文：用命名成员替代 11 个位置参数。
/// 相邻参数存在同构类型（两个 Import*、两个 Export*、两个 SceneManager*），
/// 位置传参一旦写错顺序编译器不会报错，只会运行时行为错乱；命名成员赋值
/// 把"谁依赖谁"显式化，消除这类静默 bug。
struct ImportExportSetupContext
{
    UiStateCenter* stateCenter{ nullptr };
    ImportService* importService{ nullptr };
    ImportDispatcher* importDispatcher{ nullptr };
    ExportService* exportService{ nullptr };
    ExportDispatcher* exportDispatcher{ nullptr };
    Eg::SceneManager* sceneManager{ nullptr };
    Eg::SceneManager3D* sceneManager3D{ nullptr };
    SceneEditService* editService{ nullptr };
    LayerManager* layerManager{ nullptr };
    PersistenceService* persistence{ nullptr };
    Ui::ViewCaptureService* captureService{ nullptr };
};

/// 导入/导出服务装配（P2-10 从组合根提取）：
/// 读取器/写入器注册 + 服务注入 + 进度信号接线。纯装配逻辑，无自身生命周期。
class ImportExportServiceSetup
{
public:
    explicit ImportExportServiceSetup(const ImportExportSetupContext& context);

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
