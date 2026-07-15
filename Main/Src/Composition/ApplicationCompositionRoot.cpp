#include "ApplicationCompositionRoot.h"

#include "../UI/UiCommandDispatcher.h"
#include "../UI/UiCommandHandler.h"
#include "../UI/UiLayoutService.h"
#include "../UI/UiServices.h"
#include "../UI/UiShellHost.h"
#include "../UI/UiStateCenter.h"
#include "../UI/UiThemeService.h"
#include "../UI/CreateCommands.h"
#include "../UI/TransformCommands.h"
#include "../UI/SelectCommands.h"
#include "../UI/CommandHandlerAdapter.h"

#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Log/SyLogger.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"

#include <unordered_set>
#include <cmath>

#include "Ut/Mat.h"

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_commandDispatcher(std::make_unique<DefaultUiCommandDispatcher>())
    , m_undoStack(std::make_unique<DefaultUndoStack>())
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_operationBus(std::make_unique<OperationBus>())
    , m_sceneManager(std::make_unique<Eg::SceneManager>())
    , m_undoRedoManager(std::make_unique<UndoRedoManager>(m_sceneManager.get()))
    , m_sceneEditService(std::make_unique<SceneEditService>(m_sceneManager.get(), m_undoRedoManager.get()))
    , m_document2D(std::make_unique<SceneDocument2D>(m_sceneEditService.get()))
{
    // 配置命令分发器的核心依赖
    m_commandDispatcher->setStateCenter(m_stateCenter.get());
    m_commandDispatcher->setLayoutService(m_layoutService.get());
    m_commandDispatcher->setUndoStack(m_undoStack.get());

    // 组装UI服务集合，统一注入到命令分发器
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.commandDispatcher = m_commandDispatcher.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.undoStack = m_undoStack.get();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    m_commandDispatcher->setUiServices(uiServices);

    // 配置UI壳宿主的核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setCommandDispatcher(m_commandDispatcher.get());
    m_shellHost->setUndoStack(m_undoStack.get());

    // 注册所有命令处理器
    registerCommands();

    // 将 OperationBus 设为活动实例，供无上下文调用方使用
    OperationBus::setActiveInstance(m_operationBus.get());

    // 通过适配器将旧命令注册到 OperationBus
    registerCommandAdapters();

    // 注册核心编辑操作（新命令系统直接实现）
    registerCoreOperations();

    SY_INFO("[ApplicationCompositionRoot] initialized with OperationBus + CommandHandlerAdapters + CoreOperations");
}

// 注册所有命令处理器到命令分发器
// 命令处理器按功能分类：选择命令、创建命令（绘制）、变换命令（编辑）
void ApplicationCompositionRoot::registerCommands()
{
    // 选择命令
    m_commandHandlers.push_back(std::make_unique<SelectCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 创建命令 - 绘制工具
    m_commandHandlers.push_back(std::make_unique<DrawLineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CircleCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<PolylineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 阶段 1：补全基础绘图工具
    m_commandHandlers.push_back(std::make_unique<ArcCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<PolygonCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 变换命令 - 编辑操作
    m_commandHandlers.push_back(std::make_unique<MoveCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<RotateCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CopyCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 阶段 2：补全编辑操作
    m_commandHandlers.push_back(std::make_unique<DeleteCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<MirrorCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());
}

UiShellHost* ApplicationCompositionRoot::shellHost()
{
    return m_shellHost.get();
}
UiStateCenter* ApplicationCompositionRoot::stateCenter()
{
    return m_stateCenter.get();
}
UiThemeService* ApplicationCompositionRoot::themeService()
{
    return m_themeService.get();
}
UiLayoutService* ApplicationCompositionRoot::layoutService()
{
    return m_layoutService.get();
}
UiCommandDispatcher* ApplicationCompositionRoot::commandDispatcher()
{
    return m_commandDispatcher.get();
}
IInteractionDispatcher* ApplicationCompositionRoot::interactionDispatcher()
{
    return dynamic_cast<IInteractionDispatcher*>(m_commandDispatcher.get());
}
IUndoStack* ApplicationCompositionRoot::undoStack()
{
    return m_undoStack.get();
}

OperationBus* ApplicationCompositionRoot::operationBus()
{
    return m_operationBus.get();
}

// 为所有旧式 ICommandHandler 创建适配器并注册到 OperationBus
void ApplicationCompositionRoot::registerCommandAdapters()
{
    if (!m_operationBus)
        return;

    // 构建 UiServices（与构造函数保持一致）
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.commandDispatcher = m_commandDispatcher.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.undoStack = m_undoStack.get();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();

    int count = 0;
    for (const auto& handler : m_commandHandlers)
    {
        auto opId = CommandHandlerAdapter::mapCommandId(handler->commandId());
        if (opId == OperationId::None)
        {
            SY_DEBUGF("[ApplicationCompositionRoot] No OperationId mapping for handler: %s",
                handler->commandId().toUtf8().constData());
            continue;
        }

        auto adapter = std::make_unique<CommandHandlerAdapter>(handler.get(), uiServices);
        m_operationBus->registry().registerOperation(std::move(adapter));
        ++count;
    }

    SY_INFOF("[ApplicationCompositionRoot] Registered %d CommandHandlerAdapters on OperationBus", count);
}

// 注册核心编辑操作到 OperationBus（新命令系统直接实现）
void ApplicationCompositionRoot::registerCoreOperations()
{
    if (!m_operationBus || !m_sceneEditService || !m_undoRedoManager)
        return;

    auto& reg = m_operationBus->registry();
    auto* editService = m_sceneEditService.get();
    auto* undoManager = m_undoRedoManager.get();

    // ---- 撤销/重做 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Undo, [undoManager] {
            SY_INFO("[CoreOperations] Edit_Undo");
            if (undoManager && undoManager->canUndo())
                undoManager->undo();
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Redo, [undoManager] {
            SY_INFO("[CoreOperations] Edit_Redo");
            if (undoManager && undoManager->canRedo())
                undoManager->redo();
        }));

    // ---- 删除/移动/复制 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Delete, [editService] {
            SY_INFO("[CoreOperations] Edit_Delete");
            editService->deleteSelected("Delete");
        }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Move, [editService](const QVariantMap& params) {
            double dx = params.value("dx").toDouble();
            double dy = params.value("dy").toDouble();
            SY_INFOF("[CoreOperations] Edit_Move: dx=%f, dy=%f", dx, dy);
            editService->nudgeSelected(dx, dy, "Move");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_Duplicate, [editService] {
            SY_INFO("[CoreOperations] Edit_Duplicate");
            auto* scene = editService->sceneManager();
            auto selectedEntities = scene->getSelectedEntities();
            std::vector<Eg::EntityId> selectedIds;
            for (auto* e : selectedEntities)
                selectedIds.push_back(e->id);
            auto snapshots = editService->captureSnapshots(selectedIds);
            if (!snapshots.empty())
            {
                editService->addEntities(std::move(snapshots), "Duplicate");
            }
        }));

    // ---- 选择操作 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_SelectAll, [editService] {
            SY_INFO("[CoreOperations] Edit_SelectAll");
            auto* scene = editService->sceneManager();
            scene->clearSelection();
            // SceneManager 无 selectEntities(vector<EntityId>) 重载，逐个选中
            auto allIds = scene->getAllEntityIds();
            for (const auto& id : allIds)
            {
                auto* entity = scene->findEntityById(id);
                if (entity)
                    scene->selectEntity(entity);
            }
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_ClearSelection, [editService] {
            SY_INFO("[CoreOperations] Edit_ClearSelection");
            editService->sceneManager()->clearSelection();
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Edit_InvertSelection, [editService] {
            SY_INFO("[CoreOperations] Edit_InvertSelection");
            auto* scene = editService->sceneManager();
            scene->invertSelection();
        }));

    // ---- 变换操作（旋转/镜像） ----
    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Rotate, [editService](const QVariantMap& params) {
            double angleDeg = params.value("angle").toDouble();
            SY_INFOF("[CoreOperations] Edit_Rotate: angle=%f", angleDeg);
            double angleRad = angleDeg * M_PI / 180.0;
            auto* scene = editService->sceneManager();
            editService->transformSelected([angleRad, scene]() {
                auto selected = scene->getSelectedEntities();
                for (auto* e : selected)
                {
                    Ut::Mat3d mat = Ut::Mat3d::rotate(angleRad);
                    e->transform(mat);
                    scene->updateEntityBounds(e);
                }
            }, "Rotate");
        }));

    reg.registerOperation(std::make_unique<ParamLambdaOperation>(
        OperationId::Edit_Mirror, [editService](const QVariantMap& params) {
            bool horizontal = params.value("horizontal", false).toBool();
            SY_INFOF("[CoreOperations] Edit_Mirror: horizontal=%s", horizontal ? "true" : "false");
            auto* scene = editService->sceneManager();
            editService->transformSelected([horizontal, scene]() {
                auto selected = scene->getSelectedEntities();
                for (auto* e : selected)
                {
                    Ut::Mat3d mat = horizontal ? Ut::Mat3d::mirrorX() : Ut::Mat3d::mirrorY();
                    e->transform(mat);
                    scene->updateEntityBounds(e);
                }
            }, "Mirror");
        }));

    // ---- 视图操作 ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomFit, [] {
            SY_INFO("[CoreOperations] View_ZoomFit");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomIn, [] {
            SY_INFO("[CoreOperations] View_ZoomIn");
        }));

    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::View_ZoomOut, [] {
            SY_INFO("[CoreOperations] View_ZoomOut");
        }));

    SY_INFO("[ApplicationCompositionRoot] Registered core edit operations on OperationBus");
}