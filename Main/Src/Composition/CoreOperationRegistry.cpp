#include "CoreOperationRegistry.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/IUndoRedoManager.h"
#include "Engine2D/Edit/FilletChamfer.h"
#include "UI/Services/HelpDialogService.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"

#include <QObject>
#include <QWidget>

CoreOperationRegistry::CoreOperationRegistry(
    OperationBus* bus, SceneEditService* editService, IUndoRedoManager* undoManager, QWidget* parentWidget)
    : m_bus(bus)
    , m_editService(editService)
    , m_undoManager(undoManager)
    , m_parentWidget(parentWidget)
{
}

void CoreOperationRegistry::registerAll()
{
    if (!m_bus || !m_editService || !m_undoManager)
    {
        return;
    }

    auto& reg = m_bus->registry();
    auto* editService = m_editService;
    auto* undoManager = m_undoManager;

    // ---- 撤销/重做 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Undo, [undoManager] {
        if (undoManager && undoManager->canUndo())
        {
            undoManager->undo();
        }
    }));

    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Redo, [undoManager] {
        if (undoManager && undoManager->canRedo())
        {
            undoManager->redo();
        }
    }));

    // ---- 删除 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_Delete, [editService] {
        if (editService)
        {
            editService->deleteSelected("Delete");
        }
    }));

    // ---- 圆角 ----
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Fillet, [editService](const QVariantMap& params) {
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
                radius = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Fillet Radius"), QObject::tr("Radius:"), 5.0, 0.1, 10000.0, 2, &ok);

                if (!ok || radius < 0.1)
                {
                    return;
                }
            }
            Eg::FilletChamfer::applyFillet(*editService, radius);
        }));

    // ---- 倒角 ----
    reg.registerOperation(
        std::make_unique<ParamLambdaOperation>(OperationId::Edit_Chamfer, [editService](const QVariantMap& params) {
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
                distance = HelpDialogService::getDouble(
                    nullptr, QObject::tr("Chamfer Distance"), QObject::tr("Distance:"), 5.0, 0.1, 10000.0, 2, &ok);
                if (!ok || distance < 0.1)
                {
                    return;
                }
            }
            Eg::FilletChamfer::applyChamfer(*editService, distance);
        }));

    // ---- 编辑操作（选择/变换/剪贴板等） ----
    registerEditOperations();

    // ---- 帮助操作 ----
    registerHelpOperations();
}

void CoreOperationRegistry::registerHelpOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();
    QWidget* parentWidget = m_parentWidget;

    // ---- Help: About ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_About, [parentWidget] {
        HelpDialogService::showAboutDialog(parentWidget);
    }));

    // ---- Help: Settings ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Settings, [parentWidget] {
        // 路由到活动工作台的设置对话框：2D 工作台接管真实设置（保存到 settings.db），
        // 3D/其他未接管时退化为 HelpDialogService 的兜底提示。
        auto* window = qobject_cast<WorkbenchWindow*>(parentWidget);
        UiWorkbench* activeWb = window ? window->currentWorkbench() : nullptr;
        if (activeWb && activeWb->showSettingsDialog(parentWidget))
        {
            return;
        }
        HelpDialogService::showSettingsDialog(parentWidget);
    }));

    // ---- Help: Documentation ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Docs, [parentWidget] {
        HelpDialogService::showDocumentationDialog(parentWidget);
    }));

    // ---- Help: Keyboard Shortcuts ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Help_Shortcut, [parentWidget] {
        HelpDialogService::showShortcutsDialog(parentWidget);
    }));
}

void CoreOperationRegistry::registerEditOperations()
{
    if (!m_bus)
    {
        return;
    }

    auto& reg = m_bus->registry();

    // NOTE: EditOperations.cpp 的 OperationEdit::registerAll() 已在 ApplicationCompositionRoot
    //       中优先注册 (first-wins)，覆盖以下操作:
    //       SelectAll, ClearSelection, InvertSelection, Copy, Cut, Paste, Duplicate,
    //       Rotate, MirrorH, MirrorV, Mirror, Align, Move, Nudge,
    //       Trim, Extend, GroupToggle, BezierToggle, SplitBezier, MergeBezier,
    //       GetBbox, Discretize, Array
    // 此处仅保留 EditOperations.cpp 未覆盖的占位操作。

    // ---- 颜色/图层（占位，依赖旧框架 MiscOperations/ViewOperations） ----
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_SetColor, [] {}));
    reg.registerOperation(std::make_unique<LambdaOperation>(OperationId::Edit_MoveToLayer, [] {}));
}