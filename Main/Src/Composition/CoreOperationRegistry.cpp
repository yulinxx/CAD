#include "CoreOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/FilletChamfer.h"
#include "UI/Services/HelpDialogService.h"
#include "Log/SyLogger.h"

#include <QObject>
#include <QWidget>

CoreOperationRegistry::CoreOperationRegistry(OperationBus* bus,
    SceneEditService* editService,
    IUndoRedoManager* undoManager,
    HelpDialogService* helpDialog,
    QWidget* parentWidget)
    : m_bus(bus)
    , m_editService(editService)
    , m_undoManager(undoManager)
    , m_helpDialog(helpDialog)
    , m_parentWidget(parentWidget)
{
}

void CoreOperationRegistry::registerAll()
{
    if (!m_bus || !m_editService || !m_undoManager)
        return;

    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* undoManager = m_undoManager;
    auto* helpDlg = m_helpDialog;

    // ---- 撤销/重做 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Undo, [undoManager] {
            if (undoManager && undoManager->canUndo())
                undoManager->undo();
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Redo, [undoManager] {
            if (undoManager && undoManager->canRedo())
                undoManager->redo();
        }));

    // ---- 删除 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Delete, [editService] {
            if (editService)
                editService->deleteSelected("Delete");
        }));

    // 选择、群组操作已在 EditOperations.cpp 中注册（SelectAll/ClearSelection/InvertSelection/GroupToggle），
    // 此处不再重复注册。

    // ---- 圆角 ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Fillet, [editService, helpDlg](const QVariantMap& params) {
            double radius = params.value("radius", -1.0).toDouble();
            if (radius < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    return;
                }

                bool ok = false;
                radius = HelpDialogService::getDouble(nullptr, QObject::tr("Fillet Radius"),
                    QObject::tr("Radius:"), 5.0, 0.1, 10000.0, 2, &ok);

                if (!ok || radius < 0.1)
                    return;
            }
            Eg::FilletChamfer::applyFillet(*editService, radius);
        }));

    // ---- 倒角 ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Chamfer, [editService, helpDlg](const QVariantMap& params) {
            double distance = params.value("distance", -1.0).toDouble();
            if (distance < 0.0)
            {
                auto* scene = editService->sceneManager();
                auto selected = scene->getSelectedEntities();
                if (selected.size() < 2)
                {
                    return;
                }
                bool ok = false;
                distance = HelpDialogService::getDouble(nullptr, QObject::tr("Chamfer Distance"),
                    QObject::tr("Distance:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || distance < 0.1) return;
            }
            Eg::FilletChamfer::applyChamfer(*editService, distance);
        }));

    // 视图操作（View_ZoomFit/ZoomIn/ZoomOut）已移至 PendingOperationRegistry
    // 由 PendingOperationRegistry 统一管理视图类占位操作

    // ---- 帮助操作 ----
    registerHelpOperations();
}

void CoreOperationRegistry::registerHelpOperations()
{
    if (!m_bus)
        return;

    auto& reg = m_bus->registry();
    QWidget* parentWidget = m_parentWidget;

    // ---- Help: About ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_About, [parentWidget] {
            HelpDialogService::showAboutDialog(parentWidget);
        }));

    // ---- Help: Settings ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Settings, [parentWidget] {
            HelpDialogService::showSettingsDialog(parentWidget);
        }));

    // ---- Help: Documentation ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Docs, [parentWidget] {
            HelpDialogService::showDocumentationDialog(parentWidget);
        }));

    // ---- Help: Keyboard Shortcuts ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Shortcut, [parentWidget] {
            HelpDialogService::showShortcutsDialog(parentWidget);
        }));
}