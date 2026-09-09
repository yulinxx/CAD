#pragma once

#include "OpRegistryTypes.h"

class OperationBus;
class ViewportActionHub;
class UiStateCenter;
class LayerEditService;
class UnitManager;
namespace Ui { class ViewCaptureService; }
class QWidget;

/**
 * @class ViewOperationRegistry
 * @brief 视图操作注册器 — 缩放/平移/网格/捕捉/图层管理器等视图操作
 *
 * 从 CoreOperationRegistry 拆分而来（2026-09-08）。
 */
class ViewOperationRegistry
{
public:
    ViewOperationRegistry(OperationBus* bus,
        ViewportActionHub* viewportActionHub,
        UiStateCenter* stateCenter,
        LayerEditService* layerEditService,
        UnitManager* unitManager,
        Ui::ViewCaptureService* captureService = nullptr,
        QWidget* parentWidget = nullptr);

    void registerAll();

private:
    OperationBus* m_bus;
    ViewportActionHub* m_viewportActionHub;
    UiStateCenter* m_stateCenter;
    LayerEditService* m_layerEditService;
    UnitManager* m_unitManager;
    Ui::ViewCaptureService* m_captureService;
    QWidget* m_parentWidget;
};
