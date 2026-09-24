#include "Workbench2D.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QDockWidget>
#include <QShortcut>
#include <QSizePolicy>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QApplication>
#include <QToolBar>
#include <QWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QStatusBar>
#include <QFileInfo>
#include <QDesktopServices>

#include <QUrl>
#include <string>
#include <vector>
#include <functional>

#include "Composition/ApplicationCompositionRoot.h"
#include "SceneDocument2D.h"
#include "UiServices.h"
#include "UiStateCenter.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Services/HelpDialogService.h"
#include "UI/Services/ISelectionService.h"
#include "UI/Service/ViewCaptureService.h"
#include "UiSceneTreePanel.h"
#include "SceneTreeModel2D.h"
#include "SceneTreeBuilder2D.h"


#include "UiPropertiesPanel.h"
#include "RenderViewport2D.h"
#include "FileDropHandler.h"

#include <optional>
#include "DrawToolBarWidget.h"
#include "DrawToolSwitchRegistry.h"
#include "WorkbenchWindow.h"
#include "WorkbenchMenuManager.h"
#include "WorkbenchLayoutManager.h"

#include "ClientConfig/UiClientConfigBase.h"
#include "ClientConfig/UiConfigurationManager.h"
#include "ClientConfig/UiContextMenuService.h"
#include "ClientConfig/UiLayoutBuilder.h"

#include "UiStateBridge2D.h"
#include "RenderWidget.h"

#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationRouting.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "UI2D/Service/EntityPropertyModel2D.h"
#include "UI2D/Service/EntityPropertyEditSession2D.h"

#include "UI2D/Service/SceneMonitor.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Settings/SettingsUiCoordinator2D.h"
#include "UI2D/StatusBar/StatusBar.h"
#include "UI2D/ToolBar/RightToolBar.h"
#include "UI2D/ToolBar/TopToolBar.h"
#include "UI2D/ToolBar/TextFontToolBar.h"
#include "UI2D/DrawTools/ToolManager.h"

#include "UI/Settings/SettingsService.h"
#include "UI/DrawTools/TextEditTool.h"
#include "UI/UiMetrics.h"
#include "UI/ThemeManager.h"
#include "UI/Service/ToolBarContextManager.h"

#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/IUndoRedoManager.h"

#include "Engine/EntityIdUtils.h"
#include "Engine/SyEntity/SyEntity.h"

#include "UiStateCenter.h"

#include "Import/ImportService.h"
#include "Color/Color.hpp"
#include "Log/SyLogger.h"

// ==================== Workbench2D helpers ====================

namespace
{
    /// ISelectionService::visitSelectedIds 的收集 visitor（POD 安全，收集图元 ID）
    void collectSelectedIds(const char* id, void* context)
    {
        auto* ids = static_cast<std::vector<Eg::EntityId>*>(context);
        if (id)
        {
            if (auto eid = Eg::parseEntityId(std::string(id)))
            {
                ids->push_back(*eid);
            }
        }
    }
}  // namespace

// 场景树场景观察者：捕获绕过操作总线的直接编辑（如视口 Delete 键删除），
// 只关心场景变化，由 Workbench2D 依据图元数量变化判断是否需要重建树。
class SceneTreeSceneObserver2D final : public Eg::SceneNotifier::IObserver
{
public:
    using Callback = std::function<void()>;

    explicit SceneTreeSceneObserver2D(Callback cb)
        : m_cb(std::move(cb))
    {
    }

    void onSceneChanged() override
    {
        if (m_cb)
        {
            m_cb();
        }
    }

private:
    Callback m_cb;
};


// ============================================================
// Workbench2D 实现
QString Workbench2D::id() const
{
    return QStringLiteral("2D");
}

namespace
{
    bool workbenchFlagEnabled(const QStringList& workbenches, const QString& workbenchId)
    {
        if (workbenches.isEmpty())
        {
            return true;
        }
        for (const auto& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

bool Workbench2D::isCommandRegistered(const QString& commandId) const
{
    return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
}

void Workbench2D::dispatchCommand(const QString& commandId, const QVariantMap& params)
{
    if (!m_commands.operationBus)
    {
        SY_WARNF("[Workbench2D] Cannot dispatch command without OperationBus: %s", qPrintable(commandId));
        return;
    }

    // 优先走 OperationRouting::dispatch：与工具栏/右键/快捷键同一条路径，
    // 保证 Edit_Rotate/Align 的 angle/mode 参数、Move/Mirror 的对话框分发完全一致。
    const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
    if (menuId != static_cast<UI::MenuActionId>(0))
    {
        SY_DEBUGF("[Workbench2D] Dispatch command='%s' menuId=%d source=Menu",
            qPrintable(commandId),
            static_cast<int>(menuId));
        OperationRouting::dispatch(menuId, m_commands.operationBus, OperationSource::Menu, params);
        return;
    }

    // 无法精确定位菜单项（工具名、无目录条目的命令）时回退到 OperationId 直接分发
    const OperationId operation = CommandCatalog::operationForCommandId(commandId);
    if (operation == OperationId::None)
    {
        // 绘图工具菜单项以 toolName（如 "LineTool"）作为命令 ID 分发时的兜底：
        // 用 operationForToolName 解析到已注册的 Tool_* 操作，经 OperationBus 激活视口对应工具，
        // 与左侧工具栏（命令中枢的工具 QAction）落到同一条分发/联动路径。
        const OperationId toolOperation = CommandCatalog::operationForToolName(commandId);
        if (toolOperation == OperationId::None)
        {
            SY_WARNF("[Workbench2D] Unknown command: %s", qPrintable(commandId));
            return;
        }
        SY_DEBUGF("[Workbench2D] Dispatch tool command='%s'", qPrintable(commandId));
        m_commands.operationBus->run(toolOperation, params, OperationSource::Menu);
        return;
    }

    SY_DEBUGF("[Workbench2D] Dispatch command='%s'", qPrintable(commandId));
    m_commands.operationBus->run(operation, params, OperationSource::Menu);
}

QString Workbench2D::commandText(const QString& commandId) const
{
    const auto operation = CommandCatalog::operationForCommandId(commandId);
    const auto* entry = CommandCatalog::findByOperation(operation);
    return entry && entry->text ? QString::fromUtf8(entry->text) : QString();
}

Workbench2D::Workbench2D() = default;
Workbench2D::~Workbench2D() = default;

QString Workbench2D::displayName() const
{
    return QObject::tr("2D Workbench");
}

bool Workbench2D::initialize(const WorkbenchServices& services)
{
    SY_INFO("[Workbench2D] initialize: starting 2D workbench initialization");

    if (!services.uiState.stateCenter || !services.uiState.interactionDispatcher)
    {
        SY_ERRORF("[Workbench2D] initialize failed: stateCenter=%p interactionDispatcher=%p",
            static_cast<void*>(services.uiState.stateCenter), static_cast<void*>(services.uiState.interactionDispatcher));
        return false;
    }
    m_uiState = services.uiState;
    m_commands = services.commands;
    m_scene = services.scene;
    m_persistence = services.persistence;
    m_view = services.view;

    // 使用应用共享 SettingsService singleton，2D/3D 逻辑一致
    m_settingsCoordinator = std::make_unique<SettingsUiCoordinator2D>(ApplicationCompositionRoot::getSettingsService());

    // 注册 2D 专属设置项，确保设置页已存在
    if (m_settingsCoordinator)
    {
        m_settingsCoordinator->init();
    }

    SY_DEBUG("[Workbench2D] initialize: 2D workbench initialized successfully");
    return true;
}

void Workbench2D::attachToWindow(WorkbenchWindow& window)
{
    m_workbenchWindow = &window;

    auto* viewport = createCentralViewport(window, nullptr);
    if (!viewport)
    {
        SY_ERROR("[Workbench2D] attachToWindow failed: createCentralViewport returned null");
        return;
    }

    window.setCentralWidget(viewport);

    auto* vp = qobject_cast<RenderViewport2D*>(viewport);
    if (vp)
    {
        m_viewport = vp;
        setupViewportServices(vp, window);
        vp->initializeTools();
        // LOD 参数调用链收敛：设置面板经 ViewRenderCoordinator 读写曲线精度
        // （initializeTools 之后 coordinator 才创建）
        if (m_settingsCoordinator)
        {
            m_settingsCoordinator->setRenderCoordinator(vp->renderCoordinator());
        }
        setupImportCallbacks(vp, window);
        vp->setActiveTool(QStringLiteral("SelectTool"));
    }

    // 属性被编辑后（已入撤销栈）延迟重建模型，避免在内联编辑器提交过程中重入
    if (auto* props = window.propertiesDock())
    {
        QObject::connect(props, &PropertiesPanelWidget::sigPropertyEdited, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                refreshPropertiesPanel();
            });
        });
    }

    // 视口动作中枢：注入当前视口，供菜单 Zoom 子菜单与右键菜单 View_* 操作统一分发。
    // 分发不经过窗口层回调 —— View_* 命令在 CoreOperationRegistry 里直接消费本中枢。
    if (m_view.viewportActionHub)
    {
        m_view.viewportActionHub->setViewport(m_viewport);
    }

    createToolbars(window);
    setupSceneTree(window);

    // 启动时从数据库加载并应用已保存的 2D 专属设置（画布/网格/标尺/捕捉）
    if (m_settingsCoordinator && m_viewport)
    {
        m_settingsCoordinator->loadAndApplySettings(
            m_viewport->renderWidget(), m_viewport->gridSnapManager(), m_view.unitManager);
    }

    // 创建 2D 状态栏 widget 并挂载到窗口
    // StatusBar 封装了坐标/选择/消息/状态信息的显示，与 3D StatusBar3D 完全独立
    if (!m_statusBar2D)
    {
        m_statusBar2D = new StatusBar(&window);
        // Position 标签弹出单位选择 → 复用与视图菜单完全相同的命令分发路径
        m_statusBar2D->setUnitManager(m_view.unitManager);
        // dispatchCommand 现在带 params，信号只给 commandId，这里用 lambda 补空参数
        connect(m_statusBar2D, &StatusBar::sigUnitCommandRequested, this, [this](const QString& commandId) {
            dispatchCommand(commandId, QVariantMap{});
        });
        SY_DEBUG("[Workbench2D] StatusBar created");
    }
    window.mountStatusBar(m_statusBar2D);

    // 命令 UI 刷新触发源集中挂载（选择/图层锁/场景变化含图元锁/撤销栈/操作完成）。
    // 放在最后：此时视口、工具栏、场景树、状态栏均已就绪，install 内的首次刷新可覆盖全部 UI。
    // 返回的句柄必须留到 deactivate 销毁：bus / layerBridge 跨工作台长寿，
    // 连线两端都不死时 Qt 不会自动回收，往返切换 N 次就会刷 N 次。
    m_uiStateConnections = UiStateBridge2D::install(
        this, m_viewport, m_commands.operationBus, m_scene.layerManagerBridge, m_sceneMonitor);
}

bool Workbench2D::showSettingsDialog(QWidget* /*parent*/)
{
    SY_DEBUGF("[Workbench2D] showSettingsDialog: m_settingsCoordinator=%p m_viewport=%p",
        static_cast<void*>(m_settingsCoordinator.get()),
        static_cast<void*>(m_viewport));

    if (!m_settingsCoordinator || !m_viewport)
    {
        SY_WARNF("[Workbench2D] showSettingsDialog: failed - coordinator or viewport is null");
        return false;
    }

    // 2D 捕捉设置由视口持有的 GridSnapManager 驱动（coordinator 已做空指针保护）
    RenderWidget* widget = m_viewport->renderWidget();
    // 快捷键页的数据来自配置驱动菜单的台账（菜单管理器持有），2D/3D 同一份实现
    IShortcutSettingsModel* shortcutModel = m_workbenchWindow && m_workbenchWindow->menuManager()
        ? m_workbenchWindow->menuManager()->shortcutSettingsModel()
        : nullptr;
    return m_settingsCoordinator->showSettingsDialog(
        widget, m_viewport->gridSnapManager(), m_view.unitManager, shortcutModel);
}

void Workbench2D::saveCurrentSettings()
{
    if (!m_settingsCoordinator || !m_viewport)
    {
        return;
    }
    m_settingsCoordinator->saveCurrentSettings(
        m_viewport->renderWidget(), m_viewport->gridSnapManager(), m_view.unitManager);
}

QWidget* Workbench2D::createCentralViewport(WorkbenchWindow& window, PropertiesPanelWidget* properties)
{
    Q_UNUSED(properties);
    auto* viewport = new RenderViewport2D();
    return viewport;
}

void Workbench2D::setupViewportServices(RenderViewport2D* vp, WorkbenchWindow& window)
{
    Q_UNUSED(window);
    vp->setSelectionService(m_scene.selectionService);
    vp->setInteractionDispatcher(m_uiState.interactionDispatcher);
    vp->setOperationBus(m_commands.operationBus);
    vp->setLayerManager(m_scene.layerManager);

    // P1: 视口通过信号通知上层，不直接持有编辑服务

    if (m_scene.sceneEditService)
    {
        QObject::connect(
            vp, &RenderViewport2D::entitySubmitRequested, [service = m_scene.sceneEditService](Eg::SyEntity* e) {
                if (e)
                {
                    service->addEntityFromPointer(e, "Draw");
                }
            });

        // 场景变更监控：捕获拖拽/交互式修改、图元锁定/可见性等非操作总线路径的场景变更。
        // 订阅由 UiStateBridge2D::install 统一挂载（见 attachToWindow 末尾）。
        if (auto* scene = m_scene.sceneEditService->sceneManager())
        {
            m_sceneMonitor = new SceneMonitor(this);
            m_sceneMonitor->watch(scene);
        }
    }

    vp->setDocument(m_scene.document2D);

    // 全局快捷键：Delete/Backspace 删除选中、Ctrl+A 全选、Esc 取消选择。
    // 采用窗口级 QShortcut（不依赖渲染控件焦点）：macOS 下 QOpenGLWidget 不会自动获焦，
    // 且选择可能来自场景树面板而非画布，全局快捷键可保证无论焦点在画布还是场景树都能生效。
    // 处于文本编辑（行编辑/多行文本）时不拦截，避免破坏正常输入。
    const auto editingText = []() -> bool {
        QWidget* fw = QApplication::focusWidget();
        return fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw) || qobject_cast<QPlainTextEdit*>(fw));
    };
    const auto deleteSelectedShapes = [this, editingText](bool forward) {
        if (editingText())
        {
            return;
        }
        // 画布上正在编辑文字：⌫ / ⌦ 属于字符级删除，不能落到「删除选中图元」。
        // editingText() 只认 QLineEdit / QTextEdit 这类 Qt 控件，TextEditTool 的
        // 光标在视口里（焦点控件是 QOpenGLWidget），它拦不住 —— 必须问视口。
        //   forward=false（⌫）→ 删选区或光标前一个字符
        //   forward=true （⌦）→ 删选区或光标后一个字符
        if (m_viewport && m_viewport->handleTextDeleteRequest(forward))
        {
            return;
        }
        // 绘制中的 Delete / Backspace 先归绘图工具：撤销上一个已确定的落点。
        // 样条 / NURBS / 多段线这类要点多次的图元，点错一个不该整条重画；
        // 这也不是主 Undo 栈 —— 只退当前正在构造的图元的输入点，已提交图元不动。
        //
        // 这一跳与 ESC 那跳同理且更硬：Delete / Backspace 是 Qt::ApplicationShortcut，
        // 按键在送达视口 widget 之前就被快捷键系统消费，BaseTool::onKeyPress 里的
        // 回退分支在绘图态根本走不到。
        if (m_viewport && m_viewport->handleStepBackRequest())
        {
            return;
        }
        // 统一经 OperationBus 跑 Edit_Delete，与菜单 / 工具栏 / 右键完全同一条路径
        // （handler 见 CoreOperationRegistry::registerAll 的 Edit_Delete，落到
        // SceneEditService::deleteSelected）。3D 侧 setup3DDeleteShortcuts 也是这么做的。
        //
        // 曾经这里直连 sceneEditService 并额外做 selectionService->clear() +
        // requestFullRefresh()，两样都不需要且有害：
        //   - 选择表：SceneManager::extractEntitiesById 内部已 m_selection.remove()，
        //     额外 clear 会在删除被锁定拒绝时把用户的选择一起清掉；
        //   - 全量刷新：删除会 notifySceneChanged()，视口按增量路径重建即可，
        //     菜单删除一直没有全刷也从没出现残影。
        if (m_commands.operationBus)
        {
            SY_DEBUG("[Workbench2D] Delete shortcut activated, running Edit_Delete operation");
            m_commands.operationBus->run(OperationId::Edit_Delete, {}, OperationSource::Shortcut);
        }
    };
    const auto clearSelectionShapes = [this, editingText]() {
        if (editingText())
        {
            return;
        }
        // ESC 是分级语义，第一优先级在视口：
        //   绘制中 → 丢弃当前图元、留在绘图工具；绘图工具空闲 → 退回选择工具。
        // 视口消费掉就到此为止，不要顺手再清一次选择 —— 否则「画到一半按 ESC」
        // 会连用户之前的选中集一起清掉。
        //
        // 这一跳是必需的而不是冗余：ESC 走的是窗口级 QShortcut，Qt 的快捷键在
        // 键事件送达聚焦控件**之前**就消费掉了，视口的 ViewportInputRouter 根本
        // 拿不到 KeyPress（Delete / Backspace 的注释里描述的是同一机制）。
        if (m_viewport && m_viewport->handleEscapeRequest())
        {
            return;
        }
        if (m_scene.selectionService)
        {
            m_scene.selectionService->clear();
        }
        if (m_viewport)
        {
            m_viewport->requestFullRefresh();
        }
    };

    // 这几个全局快捷键必须走 window.registerShortcut 登记，否则工作台切换时
    // clearAllShortcuts 删不到它们（它只回收登记过的）：切到 3D 后 2D 的 Delete
    // 仍然活着，一边在 3D 界面里删 2D 场景的图元，一边和 3D 注册的 Delete 同键冲突，
    // Qt 判 ambiguous 后两个都不触发。3D 侧（setup3DDeleteShortcuts）一直是这么做的。
    //
    // Delete / Backspace 必须设 Qt::ApplicationShortcut，与 3D 侧一致：
    // 这样按键在送达视口 widget 之前就被快捷键系统消费掉，画布内的工具/输入路由
    // 不会再拿到同一次按键 —— 「一次按键删两次」在结构上就不可能发生，而不是靠
    // 各条路径自己的选中集判定去互相躲。
    //
    // 两个键都删选中图元（macOS 上 ⌫ 发 Key_Backspace，Key_Delete 要按 Fn+⌫；
    // 只绑 Key_Delete 等于让 Mac 用户没法用最顺手的那个键）。区别只在文本编辑态：
    // forward=false 删光标前，forward=true 删光标后，判定见 deleteSelectedShapes。
    //
    // ⚠️ 单一所有者：这两条不能与菜单项声明的键序列重复。base.json 里
    // `edit.delete` 曾带 "shortcut": "Delete"，与下面的 Key_Delete 撞成两个
    // ApplicationShortcut 接收者，Qt 判 ambiguous 后两边都不触发 —— 表现就是
    // 「按删除键没反应」。UiLayoutBuilder::buildShortcuts 的 m_menuShortcutKeys
    // 守卫只拦配置里的 shortcuts 节，拦不到这里裸建的 QShortcut，所以该键序列
    // 必须从菜单配置里摘掉（菜单项仍由 CommandActionHub 提供，只是不带快捷键）。
    auto* deleteSc = new QShortcut(QKeySequence(Qt::Key_Delete), &window);
    deleteSc->setContext(Qt::ApplicationShortcut);
    QObject::connect(deleteSc, &QShortcut::activated, this, [deleteSelectedShapes]() {
        deleteSelectedShapes(/*forward=*/true);
    });
    window.registerShortcut(deleteSc);
    auto* backspaceSc = new QShortcut(QKeySequence(Qt::Key_Backspace), &window);
    backspaceSc->setContext(Qt::ApplicationShortcut);
    QObject::connect(backspaceSc, &QShortcut::activated, this, [deleteSelectedShapes]() {
        deleteSelectedShapes(/*forward=*/false);
    });
    window.registerShortcut(backspaceSc);
    auto* selectAllSc = new QShortcut(QKeySequence::SelectAll, &window);
    QObject::connect(selectAllSc, &QShortcut::activated, this, [this, editingText]() {
        if (!editingText() && m_commands.operationBus)
        {
            m_commands.operationBus->run(OperationId::Edit_SelectAll, {}, OperationSource::Shortcut);
        }
    });
    window.registerShortcut(selectAllSc);
    auto* escSc = new QShortcut(QKeySequence(Qt::Key_Escape), &window);
    QObject::connect(escSc, &QShortcut::activated, this, clearSelectionShapes);
    window.registerShortcut(escSc);

    // F12 截图
    auto* captureSc = new QShortcut(QKeySequence(Qt::Key_F12), &window);
    QObject::connect(captureSc, &QShortcut::activated, this, [this, editingText]() {
        if (editingText() || !m_commands.operationBus)
        {
            return;
        }
        m_commands.operationBus->run(OperationId::View_Capture, {}, OperationSource::Shortcut);
    });
    window.registerShortcut(captureSc);

    // 状态回调：将视口状态写入状态中心
    if (m_uiState.stateCenter)
    {
        vp->setStatusCallback([stateCenter = m_uiState.stateCenter](const QString& text) {
            stateCenter->setStatusPrompt(text);
        });

        // 选择变化 → 刷新命令 UI（工具栏/右键菜单/面板/状态栏）的连线统一由
        // UiStateBridge2D::install 挂载，此处不再逐个 connect，避免触发源散落漏接。

        // 鼠标移动时实时更新状态栏位置标签
        vp->setPositionCallback([stateCenter = m_uiState.stateCenter, &window](double x, double y) {
            QVariantMap meta = stateCenter->metadata();
            meta["mouseX"] = x;
            meta["mouseY"] = y;
            stateCenter->setMetadata(meta);
            window.updatePositionLabel(x, y);
        });
    }
}

void Workbench2D::setupImportCallbacks(RenderViewport2D* vp, WorkbenchWindow& window)
{
    if (!m_persistence.importService)
    {
        return;
    }

    // 拖放导入时，把图片/位图放到鼠标松开的全局坐标处（世界坐标）
    if (auto* fdh = window.fileDropHandler())
    {
        fdh->setScreenToWorldConverter([vp](const QPoint& globalPos) -> std::optional<QPointF> {
            if (!vp)
            {
                return std::nullopt;
            }
            return vp->mapGlobalToScene(globalPos);
        });
    }

    // 导入后自动 zoomToFit
    m_persistence.importService->setViewportFitCallback([vp]() {
        QTimer::singleShot(0, vp, [vp]() {
            vp->zoomToFit();
        });
    });

    // 统一显示刷新（批量刷新场景树、属性面板，避免导入后多次分离刷新回调）
    m_persistence.importService->setDisplayRefreshCallback([this]() {
        // 走结构签名门控，不要无条件全量重建。
        // 导入期的场景观察者（SceneNotifier → onSceneTreeSceneChanged）已经把本批新增
        // 按增量追加进树、并刷新了结构签名；这里再直接 refreshSceneTree()，等于把整棵树
        // （buildTopology 的 O(N) 扫描 + 换模型 + 全表 dataChanged）白做一遍。
        // 29 万图元下实测日志里连着出现「incremental +293040 rows」与「rebuilt topLevel=293040」，
        // 就是这条路径与观察者路径重复所致。
        // 签名一致时直接跳过；确实变了（观察者没覆盖到的导入路径）才防抖重建。
        refreshSceneTreeIfNeeded("importDone");
        refreshPropertiesPanel();
    });
}

void Workbench2D::setPanelHostStyle(PanelHostStyle style)
{
    if (m_panelHostStyle == style)
    {
        return;
    }
    m_panelHostStyle = style;
}

namespace
{
    /// 取出左侧绘图工具栏要展示的中枢 QAction，顺序即命令目录顺序
    ///
    /// 目录是唯一事实来源：surfaces 决定"上不上左侧栏"，中枢决定"动作长什么样、点了干什么"。
    /// 展示层（DrawToolBarWidget）只按这个顺序摆按钮。
    QVector<QAction*> buildLeftToolbarActions(CommandActionHub& hub)
    {
        QVector<QAction*> actions;
        for (const auto& entry : CommandCatalog::commands())
        {
            if (!hasSurface(entry.surfaces, CommandSurface2DValues::LeftToolbar) || !entry.toolName)
            {
                continue;
            }
            const QString toolName = QString::fromUtf8(entry.toolName);
            if (QAction* action = hub.toolAction(toolName))
            {
                actions.append(action);
            }
            else
            {
                SY_WARNF("[Workbench2D] left toolbar entry has no hub action: %s", qPrintable(toolName));
            }
        }
        return actions;
    }

    QStringList buildSupportedImportFormatsFromCatalog(const QString& workbenchId)
    {
        QStringList formats;
        const bool want3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

        struct SupportedImportDef
        {
            const char* commandId;
            bool in2D;
            bool in3D;
        };

        static const SupportedImportDef kSupportedImports[] = {
            { "file.import_dxf", true, false },
            { "file.import_plt", true, false },
            { "file.import_svg", true, false },
            { "file.import_pdf", true, false },
            { "file.import_ai", true, false },
            { "file.import_ug", true, false },
            { "file.import_image", true, false },
            { "file.import_step", true, true },
            { "file.import_model", false, true },
            { "file.import_obj", false, true },
            { "file.import_stl", false, true },
            { "file.open_step", false, true },
        };

        for (const auto& item : kSupportedImports)
        {
            if ((want3D && !item.in3D) || (!want3D && !item.in2D))
            {
                continue;
            }
            const QString cmdId = QString::fromUtf8(item.commandId);
            if (!formats.contains(cmdId))
            {
                formats.append(cmdId);
            }
        }
        return formats;
    }
}  // namespace

QVector<QAction*> Workbench2D::buildDrawToolActions()
{
    return m_commandHub ? buildLeftToolbarActions(*m_commandHub) : QVector<QAction*>{};
}

QStringList Workbench2D::buildSupportedImportFormats(const QString& workbenchId)
{
    return buildSupportedImportFormatsFromCatalog(workbenchId);
}

void Workbench2D::createToolbars(WorkbenchWindow& window)
{
    // 接通「UI 入口 → OperationBus → 视口」：把绘图工具的 Tool_* 操作注册到操作总线，
    // 触发后经总线转发表驱动的 LambdaOperation 激活视口对应工具。
    // 必须在视口（m_viewport）创建完成后再注册，故放在这里而非组合根。
    DrawToolSwitchRegistry(m_commands.operationBus, &m_viewport).registerAll();

    // View → Grid & Snap 菜单的真正生效点：把 stateCenter 元数据映射到网格显隐。
    // 菜单/操作只翻转 metadata(gridVisible)，此处作为唯一消费者同步到视口网格渲染。
    if (m_uiState.stateCenter && m_viewport)
    {
        const auto applyGridVisibleFromMetadata = [stateCenter = m_uiState.stateCenter, vp = m_viewport]() {
            // gridVisible 只由 View → Grid 菜单写入，键不存在时不能当 false 用
            const QVariant value = stateCenter->metadata().value(QStringLiteral("gridVisible"));
            if (!value.isValid())
            {
                return;
            }
            if (auto* renderWidget = vp->renderWidget())
            {
                if (auto* env = renderWidget->sceneEnvironment())
                {
                    env->setGridVisible(value.toBool());
                    env->notifyChanged();
                }
            }
        };
        m_gridVisibilityMetadataConn = QObject::connect(
            m_uiState.stateCenter, &UiStateCenter::metadataChanged, this, applyGridVisibleFromMetadata);

        // 用场景当前（已由设置应用过）的可见性给元数据播种，之后菜单勾选态与画布才一致。
        if (auto* renderWidget = m_viewport->renderWidget())
        {
            if (auto* env = renderWidget->sceneEnvironment())
            {
                QVariantMap meta = m_uiState.stateCenter->metadata();
                if (!meta.value(QStringLiteral("gridVisible")).isValid())
                {
                    meta.insert(QStringLiteral("gridVisible"), env->settings().grid.visible);
                    m_uiState.stateCenter->setMetadata(meta);
                }
            }
        }
        applyGridVisibleFromMetadata();
    }

    // CommandActionHub：管理所有 QAction 的创建与绑定
    m_commandHub = std::make_unique<CommandActionHub>();
    m_commandHub->setMainWindow(&window);
    m_commandHub->setOperationBus(m_commands.operationBus);
    // 单一数据源：一次遍历选中的图元集合，统一算出 count / 锁定(图层+图元) / 可编辑 /
    // 类型直方图 / 分组 / 贝塞尔，注入给命令中枢
    m_commandHub->setSelectionContextProvider([selectionService = m_scene.selectionService,
                                                  layerManager = m_scene.layerManager,
                                                  sceneEditService = m_scene.sceneEditService]() -> SelectionContext {
        SelectionContext result;
        if (!selectionService || !layerManager || !sceneEditService)
        {
            return result;
        }
        Eg::SceneManager* scene = sceneEditService->sceneManager();
        if (!scene)
        {
            return result;
        }
        struct Ctx
        {
            Eg::SceneManager* scene;
            LayerManager* layers;
            SelectionContext* out;
        };
        Ctx ctx{ scene, layerManager, &result };
        selectionService->visitSelectedIds(
            [](const char* id, void* v) {
                auto* c = static_cast<Ctx*>(v);
                auto eid = Eg::parseEntityId(std::string(id));
                if (!eid)
                {
                    return;
                }
                Eg::SyEntity* e = c->scene->findEntityById(*eid);
                if (!e)
                {
                    return;
                }
                c->out->selectionCount++;
                const bool layerLocked = c->layers->isLayerLocked(c->layers->getEntityLayer(e));
                const bool entityLocked = e->locked();
                if (layerLocked)
                {
                    c->out->anyLockedLayer = true;
                }
                if (entityLocked)
                {
                    c->out->anyLockedEntity = true;
                }
                if (!layerLocked && !entityLocked)
                {
                    c->out->anyEditable = true;
                }
                // 检查隐藏状态
                if (!e->visible())
                {
                    c->out->anyHidden = true;
                }
                switch (e->eType)
                {
                case Eg::EType::TEXT:
                    c->out->typeMask |= static_cast<uint32_t>(SelectionTypeBit::Text);
                    break;
                case Eg::EType::QR_CODE:
                    c->out->typeMask |= static_cast<uint32_t>(SelectionTypeBit::Qr);
                    break;
                case Eg::EType::IMAGE:
                    c->out->typeMask |= static_cast<uint32_t>(SelectionTypeBit::Bitmap);
                    break;
                case Eg::EType::LINE:
                case Eg::EType::ARC:
                case Eg::EType::CIRCLE:
                case Eg::EType::ELLIPSE:
                case Eg::EType::SMARTLINE:
                case Eg::EType::POLYGON:
                case Eg::EType::SPLINE:
                case Eg::EType::BEZIER:
                case Eg::EType::BEZIER2:
                    c->out->typeMask |= static_cast<uint32_t>(SelectionTypeBit::Vector);
                    break;
                default:
                    c->out->typeMask |= static_cast<uint32_t>(SelectionTypeBit::Other);
                    break;
                }
                if (e->group())
                {
                    c->out->groupOn = true;
                }
            },
            &ctx);
        result.hasSelection = result.selectionCount > 0;
        // 分组按钮：需有选中图元 且 未命中任意锁定（图层锁或图元锁）且 未隐藏，
        // 与 RequiresUnlockedSelection 的双锁语义保持一致。
        result.groupEnabled =
            result.hasSelection && !(result.anyLockedLayer || result.anyLockedEntity || result.anyHidden);
        // 贝塞尔切换按钮：当前无独立语义，保持禁用（与重构前未赋值行为一致）
        result.bezierEnabled = false;
        return result;
    });
    // 注入撤销/重做状态提供器（OperationBus context 在生产路径不填充 undoManager）
    //
    // 这里必须在接线时就把漏注入喊出来：provider 的返回值会在
    // CommandActionHub::captureSnapshot 里**覆盖** snapshot.canUndo / canRedo，
    // 捕到 nullptr 就等于把两者钉死为 false，而 edit.undo / edit.redo 的
    // RequiresUndo / RequiresRedo 判定随之永远不通过 —— 菜单和工具栏的撤销/重做
    // 一直置灰，置灰的 QAction 连 Ctrl+Z / Ctrl+Y 都不触发。整条链路没有任何
    // 报错，表现只是「撤销完全没反应」，极难往接线上查（2026-08-31 实际踩过：
    // ApplicationCompositionRoot::assembleUiServices 漏了这一项）。
    if (!m_commands.undoManager)
    {
        SY_WARN("[Workbench2D] UiServices::undoManager is null, Undo/Redo actions will stay disabled");
    }
    m_commandHub->setUndoRedoProvider([undoManager = m_commands.undoManager]() -> UndoRedoState {
        UndoRedoState state;
        if (undoManager)
        {
            state.canUndo = undoManager->canUndo();
            state.canRedo = undoManager->canRedo();
        }
        return state;
    });
    // 注入剪贴板内容提供器（Paste 按钮启用状态，实时反映剪贴板是否已复制图元）
    m_commandHub->setClipboardProvider([clipboard = m_scene.clipboard]() -> bool {
        return clipboard && clipboard->hasContent();
    });
    m_commandHub->rebuildAllActions();

    // 左侧绘图工具面板：必须在中枢建好动作之后创建 —— 面板只是中枢 QAction 的展示壳，
    // 早于 rebuildAllActions 创建就一个按钮都拿不到。
    auto* drawWidget = new DrawToolBarWidget(&window);

    // 设置 Select/Pan toggle 回调。必须在 setToolActions() 之前：setToolActions 内部会
    // 立即 rebuildButtons()，而 Select 按钮的切换逻辑依赖这两个回调是否已就绪；
    // 若晚于 setToolActions 设置，重建时回调为空，Select 按钮会退化为普通按钮，
    // 点击只会 trigger 一次 SelectTool，永远进不了 Pan。
    drawWidget->setIsPanModeCallback([this]() {
        return m_viewport && m_viewport->isPanModeEnabled();
    });
    drawWidget->setPanModeToggleCallback([this]() {
        if (m_viewport)
        {
            m_viewport->setPanModeEnabled(!m_viewport->isPanModeEnabled());
            return true;
        }
        return false;
    });

    const QVector<QAction*> drawToolActions = buildDrawToolActions();
    drawWidget->setToolActions(drawToolActions);

    // 监听 Pan 模式变化，更新 Select 按钮图标和状态
    if (m_viewport)
    {
        connect(m_viewport, &RenderViewport2D::panModeChanged, drawWidget, &DrawToolBarWidget::setPanMode);
    }

    SY_DEBUGF("[Workbench2D] Draw tool panel built: tools=%d host=%s",
        static_cast<int>(drawToolActions.size()),
        m_panelHostStyle == PanelHostStyle::Dock ? "Dock" : "ToolBar");

    // 依据承载样式创建左侧面板（Draw Tools）
    if (m_panelHostStyle == PanelHostStyle::Dock)
    {
        window.registerDockWidget(QObject::tr("Draw Tools"), drawWidget, Qt::LeftDockWidgetArea);
    }
    else
    {
        auto* leftToolBar = new QToolBar(QObject::tr("Draw Tools"), &window);
        leftToolBar->setObjectName(QStringLiteral("DrawToolBar"));
        leftToolBar->setMovable(false);
        window.addToolBar(Qt::LeftToolBarArea, leftToolBar);
        leftToolBar->addWidget(drawWidget);
    }

    // 视口 → UI 的勾选态回写（Esc 回到 SelectTool、工具用完自动返回等）。
    // 勾选态由中枢的 QActionGroup 单点维护，展示层不再各自记 activeTool。
    // 配置驱动的 Draw 菜单项是不可勾选的普通动作，不参与高亮，只共用同一条派发链。
    QObject::connect(
        m_viewport, &RenderViewport2D::activeToolChanged, m_commandHub.get(), &CommandActionHub::setActiveToolAction);
    m_commandHub->setActiveToolAction(m_viewport->activeToolName());

    // 工具切换时同步 DrawToolBarWidget 的高亮状态
    QObject::connect(m_viewport, &RenderViewport2D::activeToolChanged, drawWidget, &DrawToolBarWidget::setCurrentToolName);

    // 文字编辑工具栏绑定：工具切换到 TextEditTool 时，绑定到 TextFontToolBar
    // 以便在进入编辑时自动刷新字体面板信息
    QObject::connect(m_viewport, &RenderViewport2D::activeToolChanged, this, [this](const QString& toolName) {
        SY_DEBUGF("[Workbench2D] activeToolChanged: %s, m_textFontToolBarWidget=%p, m_viewport=%p",
            qPrintable(toolName),
            reinterpret_cast<void*>(m_textFontToolBarWidget),
            reinterpret_cast<void*>(m_viewport));
        if (toolName == QStringLiteral("TextEditTool") && m_textFontToolBarWidget && m_viewport)
        {
            auto* toolMgr = m_viewport->toolManager();
            if (toolMgr)
            {
                ITool* tool = toolMgr->getTool(QStringLiteral("TextEditTool"));
                if (tool && tool->isTextEditTool())
                {
                    // safe to cast since isTextEditTool() returns true
                    auto* textEditTool = static_cast<TextEditTool*>(tool);
                    m_textFontToolBarWidget->bindTool(textEditTool);
                    SY_DEBUGF("[Workbench2D] TextFontToolBar bound to TextEditTool");

                    // 设置编辑状态变化回调，以便在进入/退出编辑时切换工具栏上下文
                    textEditTool->setEditingStateChangedCallback([this](bool editing) {
                        SY_DEBUGF("[Workbench2D] editingStateChanged: editing=%d, m_contextManager=%p",
                            editing,
                            reinterpret_cast<void*>(m_contextManager.get()));
                        if (m_contextManager)
                        {
                            // 进入编辑状态时切换到 TextEditing 上下文，显示字体面板
                            // 退出编辑状态时切换回 Default 上下文
                            const ToolBarContext targetCtx =
                                editing ? ToolBarContext::TextEditing : ToolBarContext::Default;
                            if (m_contextManager->currentContext() != targetCtx)
                            {
                                m_contextManager->setCurrentContext(targetCtx);
                                SY_DEBUGF("[Workbench2D] Context switched to %d", static_cast<int>(targetCtx));
                            }
                        }
                    });
                }
            }
        }
    });

    // 顶部工具栏（编辑命令）— 必须先创建，再由 ContextManager 填充 actions
    m_topToolBar = new TopToolBar(&window);
    m_topToolBar->setObjectName(QStringLiteral("TopToolBar"));
    m_topToolBar->setCommandActionHub(m_commandHub.get());
    window.addToolBar(Qt::TopToolBarArea, m_topToolBar);

    // 文字编辑字体工具栏（双击文字进入编辑会话时显示字体族/字号/粗斜下划线）
    // 必须先创建，再注册到 ContextManager
    m_textFontToolBar = new QToolBar(QObject::tr("Text Font"), &window);
    m_textFontToolBar->setObjectName(QStringLiteral("TextFontToolBar"));
    m_textFontToolBar->setMovable(false);
    m_textFontToolBar->setIconSize(QSize(UiMetrics::toolbarIconSizeSmall(), UiMetrics::toolbarIconSizeSmall()));
    m_textFontToolBarWidget = new TextFontToolBar(m_textFontToolBar);
    m_textFontToolBar->addWidget(m_textFontToolBarWidget);
    window.addToolBar(Qt::TopToolBarArea, m_textFontToolBar);
    m_textFontToolBar->setVisible(false);

    // 初始化工具栏上下文管理器
    m_contextManager = std::make_unique<ToolBarContextManager>();

    // 注册 Default 上下文（通用编辑命令）
    m_contextManager->registerContext(ToolBarContext::Default,
        {
            ToolBarContext::Default,
            tr("Edit"),
            {
                { "",
                    {
                        { "edit.undo", tr("Undo"), ":/ui/common/Icons/Actions/undo.svg" },
                        { "edit.redo", tr("Redo"), ":/ui/common/Icons/Actions/redo.svg" },
                    } },
                { "",
                    {
                        { "edit.mirror_horizontal", tr("Mirror H"), ":/ui/common/Icons/Actions/mirror_h.svg" },
                        { "edit.mirror_vertical", tr("Mirror V"), ":/ui/common/Icons/Actions/mirror_v.svg" },
                    } },
                { "",
                    {
                        { "edit.align_left", tr("Align Left"), ":/ui/common/Icons/Actions/align_left.svg" },
                        { "edit.align_right", tr("Align Right"), ":/ui/common/Icons/Actions/align_right.svg" },
                        { "edit.align_center_h", tr("Align Center H"), ":/ui/common/Icons/Actions/align_center_h.svg" },
                        { "edit.align_top", tr("Align Top"), ":/ui/common/Icons/Actions/align_top.svg" },
                        { "edit.align_bottom", tr("Align Bottom"), ":/ui/common/Icons/Actions/align_bottom.svg" },
                        { "edit.align_center_v", tr("Align Center V"), ":/ui/common/Icons/Actions/align_center_v.svg" },
                    } },
                { "",
                    {
                        { "edit.select_all", tr("Select All"), ":/ui/common/Icons/Actions/select_all.svg" },
                        { "edit.invert_selection", tr("Invert Selection"), ":/ui/common/Icons/Actions/invert_selection.svg" },
                        { "edit.deselect", tr("Deselect"), ":/ui/common/Icons/Actions/deselect.svg" },
                    } },
                { "",
                    {
                        { "edit.copy", tr("Copy"), ":/ui/common/Icons/Actions/copy.svg" },
                        { "edit.paste", tr("Paste"), ":/ui/common/Icons/Actions/paste.svg" },
                        { "edit.delete", tr("Delete"), ":/ui/common/Icons/Actions/delete.svg" },
                    } },
                { "",
                    {
                        { "edit.group", tr("Group"), ":/ui/common/Icons/Actions/group.svg", true },
                    } },
            },
        });

    // 注册 TextEditing 上下文（文字编辑工具栏）
    // 注意：不注册到 TopToolBar，因为字体/字号等控件已经在 TextFontToolBar 自定义工具栏中显示
    m_contextManager->registerContext(ToolBarContext::TextEditing,
        {
            ToolBarContext::TextEditing, tr("Text Format"), {}  // 空 action 列表，控件通过 registerCustomToolBar 显示
        });

    // 注册 QRCodeEditing 上下文（二维码编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::QREditing,
        {
            ToolBarContext::QREditing,
            tr("QR Code"),
            {
                { tr("Content"),
                    {
                        { "QR_Content", tr("Content") },
                        { "QR_ErrorCorrection", tr("Error Correction") },
                    } },
                { tr("Appearance"),
                    {
                        { "QR_Size", tr("Size") },
                        { "QR_Foreground", tr("Foreground") },
                        { "QR_Background", tr("Background") },
                    } },
                { tr("Advanced"),
                    {
                        { "QR_Logo", tr("Logo") },
                    } },
            },
        });

    // 注册 BitmapEditing 上下文（位图编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::BitmapEditing,
        {
            ToolBarContext::BitmapEditing,
            tr("Bitmap"),
            {
                { tr("Adjust"),
                    {
                        { "Bitmap_Crop", tr("Crop") },
                        { "Bitmap_Rotate", tr("Rotate") },
                        { "Bitmap_Brightness", tr("Brightness") },
                        { "Bitmap_Contrast", tr("Contrast") },
                    } },
                { tr("Filter"),
                    {
                        { "Bitmap_Filter", tr("Filter") },
                    } },
            },
        });

    // 注册 VectorEditing 上下文（矢量编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::VectorEditing,
        {
            ToolBarContext::VectorEditing,
            tr("Vector"),
            {
                { tr("Path"),
                    {
                        { "Vector_NodeEdit", tr("Node Edit") },
                        { "Vector_Simplify", tr("Simplify") },
                        { "Vector_Boolean", tr("Boolean") },
                    } },
                { tr("Style"),
                    {
                        { "Vector_Stroke", tr("Stroke") },
                        { "Vector_Fill", tr("Fill") },
                    } },
            },
        });

    // 注册 ImageEditing 上下文（图片编辑工具栏）
    m_contextManager->registerContext(ToolBarContext::ImageEditing,
        {
            ToolBarContext::ImageEditing,
            tr("Image"),
            {
                { tr("Transform"),
                    {
                        { "Image_Crop", tr("Crop") },
                        { "Image_Rotate", tr("Rotate") },
                        { "Image_Flip", tr("Flip") },
                    } },
                { tr("Adjust"),
                    {
                        { "Image_Brightness", tr("Brightness") },
                        { "Image_Contrast", tr("Contrast") },
                    } },
                { tr("Filter"),
                    {
                        { "Image_Filter", tr("Filter") },
                    } },
            },
        });

    SY_DEBUGF("[ToolBarContextManager] Registered %d context(s)", m_contextManager->contextCount());

    // 注册自定义文字编辑工具栏
    m_contextManager->registerCustomToolBar(ToolBarContext::TextEditing, m_textFontToolBarWidget, false);

    // 绑定 TopToolBar 动作设置/清空回调
    m_contextManager->setTopToolBarActionSetter([this](const QList<ToolBarAction>& actions) {
        if (m_topToolBar)
        {
            m_topToolBar->setActions(actions);
        }
    });
    m_contextManager->setTopToolBarClearer([this]() {
        if (m_topToolBar)
        {
            m_topToolBar->clearActions();
        }
    });

    // 监听上下文切换，同步显隐自定义工具栏
    connect(m_contextManager.get(),
        &ToolBarContextManager::customToolBarVisibilityChanged,
        this,
        [this](ToolBarContext ctx, bool visible) {
            if (ctx == ToolBarContext::TextEditing && m_textFontToolBar)
            {
                m_textFontToolBar->setVisible(visible);
            }
        });

    // 默认进入 Default 上下文
    m_contextManager->setCurrentContext(ToolBarContext::Default);

    // 根据选中图元类型自动切换上下文：由 applySelectionContext 消费快照 typeMask 完成，
    // 不再单独监听 selectionChanged（避免与快照刷新产生顺序竞争）。
    if (m_viewport)
    {
        // 右键菜单请求：交给命令中枢统一构建并弹出（含选择/锁定实时联动）
        connect(m_viewport, &RenderViewport2D::contextMenuRequested, this, &Workbench2D::onViewportContextMenu);
    }
    else
    {
        SY_DEBUGF("[Workbench2D] m_viewport is null when setting up selectionChanged connection!");
    }

    // 右侧图层面板（颜色/图层），依据承载样式创建
    m_rightToolBar = new RightToolBar(&window);
    m_rightToolBar->setObjectName(QStringLiteral("RightToolBar"));
    m_rightToolBar->setProperty("uiSource", QStringLiteral("CommandCatalog + LayerManager"));
    if (m_panelHostStyle == PanelHostStyle::Dock)
    {
        window.registerDockWidget(QObject::tr("Layers"), m_rightToolBar, Qt::RightDockWidgetArea);
    }
    else
    {
        window.addToolBar(Qt::RightToolBarArea, m_rightToolBar);
    }

    if (m_scene.layerManager)
    {
        m_rightToolBar->setLayerManager(m_scene.layerManager, m_scene.layerManagerBridge);
    }

    if (m_uiState.stateCenter)
    {
        QVariantMap meta = m_uiState.stateCenter->metadata();
        meta.insert(QStringLiteral("rightPanelSource"), QStringLiteral("LayerManager"));
        m_uiState.stateCenter->setMetadata(meta);
    }

    // 单击色块 → 若已选中未锁定图元则将其移动到该图层（可撤销），并设为当前图层
    QObject::connect(m_rightToolBar, &RightToolBar::sigLayerSelected, this, [this](int layerId) {
        // 「移动选中图元到图层」等价于 edit.move_to_layer，规则是 RequiresUnlockedSelection。
        // 右侧色块是 QPushButton 而非 QAction，无法靠 enableRule 灰显，
        // 因此在这里按同一份快照做前置判定 —— 否则点色块可以绕过锁定改动被锁图元。
        // 注意：「设为当前图层」不受锁定影响，必须始终执行，故只 gate 前半段。
        const bool selectionLocked = m_commandHub && m_commandHub->currentSnapshot().anyLocked();
        if (!selectionLocked && m_scene.selectionService && m_scene.layerEditService)
        {
            std::vector<Eg::EntityId> selectedIds;
            m_scene.selectionService->visitSelectedIds(&collectSelectedIds, &selectedIds);
            if (!selectedIds.empty())
            {
                m_scene.layerEditService->assignEntitiesToLayer(selectedIds, layerId, "Move selected to layer");
            }
        }
        if (m_scene.layerManager)
        {
            m_scene.layerManager->setCurrentLayer(layerId);
        }
        // 图层变更后刷新视图和动作状态
        refreshCommandUiState();
        if (m_viewport)
        {
            m_viewport->requestFullRefresh();
        }
    });

    // 双击色块 → 打开图层管理对话框（其中设置当前图层会通过 sigCurrentLayerChanged 同步回右侧色块）
    QObject::connect(m_rightToolBar, &RightToolBar::sigLayerDoubleClicked, this, [this](int /*layerId*/) {
        if (m_scene.layerEditService)
        {
            LayerManagerDialog::showDialog(m_scene.layerEditService,
                m_commandHub ? m_commandHub->mainWindow() : nullptr,
                m_scene.layerManagerBridge);
        }
    });

    // 撤销/重做会恢复/移除/重建图元（场景拓扑变化），增量刷新可能遗漏，
    // 使用 requestLightRefresh() 经调度器节流触发重绘，避免 applyFullRefresh
    // 的全量几何重建（对 700K 实体场景极昂贵）
    if (m_commands.operationBus)
    {
        QObject::connect(
            m_commands.operationBus, &OperationBus::operationCompleted, this, [this](OperationId id, bool success) {
                if (success && (id == OperationId::Edit_Undo || id == OperationId::Edit_Redo))
                {
                    if (m_viewport)
                    {
                        m_viewport->requestLightRefresh();
                    }
                }
            });
    }

    // 命令中枢广播的选择上下文快照 → 扇出到属性面板/状态栏/场景树/工具栏上下文（单一事件总线）
    if (m_commandHub)
    {
        QObject::connect(m_commandHub.get(),
            &CommandActionHub::selectionContextChanged,
            this,
            [this](const CommandUiSnapshot& snapshot) {
                applySelectionContext(snapshot);
            });
    }
}

ToolBarContext Workbench2D::determineContextFromSelection(const CommandUiSnapshot& snapshot) const
{
    // 类型事实只有一处来源：命令中枢算好的 typeMask。
    // 用入参而不是 m_commandHub->currentSnapshot()：这样本函数是纯函数，
    // 不读任何成员状态，也就不会因为「Hub 缓存有没有更新」而算出
    // 与工具栏 / 菜单 / 右键菜单不一致的上下文，顺带也不用判空 Hub。
    const uint32_t mask = snapshot.typeMask;

    const bool text = (mask & static_cast<uint32_t>(SelectionTypeBit::Text)) != 0;
    const bool qr = (mask & static_cast<uint32_t>(SelectionTypeBit::Qr)) != 0;
    const bool bitmap = (mask & static_cast<uint32_t>(SelectionTypeBit::Bitmap)) != 0;
    const bool vector = (mask & static_cast<uint32_t>(SelectionTypeBit::Vector)) != 0;
    const bool other = (mask & static_cast<uint32_t>(SelectionTypeBit::Other)) != 0;

    if (!text && !qr && !bitmap && !vector && !other)
    {
        return ToolBarContext::Default;
    }

    // 优先级：专用编辑模式 > 通用模式。混合类型时优先返回最专用的上下文。
    if (text && !qr && !bitmap && !vector)
        return ToolBarContext::TextEditing;
    if (qr && !text && !bitmap && !vector)
        return ToolBarContext::QREditing;
    if (bitmap && !text && !qr && !vector)
        return ToolBarContext::BitmapEditing;
    if (vector && !text && !qr && !bitmap)
        return ToolBarContext::VectorEditing;

    // 混合：优先最专用
    if (text)
        return ToolBarContext::TextEditing;
    if (qr)
        return ToolBarContext::QREditing;
    if (bitmap)
        return ToolBarContext::BitmapEditing;
    if (vector)
        return ToolBarContext::VectorEditing;

    return ToolBarContext::Default;
}

void Workbench2D::setupSceneTree(WorkbenchWindow& window)
{
    // 尝试复用 skeleton's 面板，确保 2D/3D 切换时使用同一实例
    // 如果 skeleton's 面板类型不匹配，我们需要创建一个新的统一面板
    m_scenePanel2D = window.sceneTreeDock();
    if (!m_scenePanel2D || qobject_cast<SceneTreePanel*>(m_scenePanel2D) == nullptr)
    {
        // skeleton's 面板不存在或类型不匹配，创建我们的 SceneTreePanel
        auto* panel = new SceneTreePanel(&window);
        panel->setObjectName(QStringLiteral("SceneTreeDock"));
        auto* dock = window.registerDockWidget(QObject::tr("Scene"), panel, Qt::LeftDockWidgetArea);
        if (dock)
        {
            dock->setObjectName(QStringLiteral("SceneDock"));
            // 限制 Scene 面板宽度：最小 180、最大 300
            dock->setMinimumWidth(180);
            dock->setMaximumWidth(300);
        }
        m_scenePanel2D = panel;
    }

    auto* panel = m_scenePanel2D;

    // 面板（UI）→ 引擎（业务）：用户操作通过算法层写回引擎
    connect(panel, &SceneTreePanel::selectionChanged, this, &Workbench2D::applySceneTreeSelection);
    connect(panel, &SceneTreePanel::visibilityToggled, this, &Workbench2D::toggleEntityVisibility);
    connect(panel, &SceneTreePanel::renameRequested, this, &Workbench2D::renameEntity);
    connect(panel, &SceneTreePanel::deleteRequested, this, &Workbench2D::deleteSceneTreeSelection);
    connect(panel, &SceneTreePanel::batchVisibilityRequested, this, &Workbench2D::setSceneTreeVisibility);
    connect(panel, &SceneTreePanel::batchLockRequested, this, &Workbench2D::setSceneTreeLock);

    // 引擎/场景（业务）→ 面板（UI）：变化后刷新展示与选中高亮
    if (m_viewport)
    {
        connect(m_viewport, &RenderViewport2D::selectionChanged, this, &Workbench2D::syncSceneTreeSelection);
    }
    if (m_commands.operationBus)
    {
        // 延迟到事件循环下一轮：撤销信号可能在场景树模型的 setData() 内同步发出
        // （如重命名入撤销栈），此时重建模型会删掉正在回调的模型对象，
        // 编辑器提交后视图持有悬空模型，表现为「重命名过一次后无法再双击编辑」。
        connect(m_commands.operationBus, &OperationBus::undoStateChanged, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                // 增量优先：画线等纯新增走单行插入；删除/群组/改名回退全量。
                // 改名等非结构变更经变更集里的非 Added 位触发全量快径 dataChanged。
                applySceneTreeIncremental("undoStateChanged");
            });
        });
        connect(m_commands.operationBus, &OperationBus::operationCompleted, this, [this](OperationId, bool success) {
            if (!success)
            {
                return;
            }
            // 统一按结构签名（图元增删 + 群组拓扑修订）决定是否重建场景树：
            // 选择类（全选/清除/反选）与纯几何类（移动/旋转/镜像/对齐/Nudge）操作
            // 都不改树的行集合，重建（buildTopology + 换模型 + 全表 dataChanged）纯属
            // 白做功；删除/粘贴/成组等结构变化签名不同，才会走防抖重建。
            // 选中高亮由 syncSceneTreeSelection 单独维护，不依赖重建。
            // 改名等「不入结构签名但要刷新行文本」的操作由上方 undoStateChanged 兜底。
            QTimer::singleShot(0, this, [this]() { refreshSceneTreeIfNeeded("opCompleted"); });
        });
    }

    // 引擎场景变更兜底：任何直接修改（如视口 Delete 键删除、导入清空等）
    // 都会经 SceneNotifier 通知这里；由结构签名（图元增删 + 群组拓扑）判断是否
    // 需要重建，防抖后统一重建，避免 Scene 列表残留、也避免拖动时反复重建。
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    if (scene)
    {
        if (!m_sceneTreeRefreshTimer)
        {
            m_sceneTreeRefreshTimer = new QTimer(this);
            m_sceneTreeRefreshTimer->setSingleShot(true);
            m_sceneTreeRefreshTimer->setInterval(150);
            connect(m_sceneTreeRefreshTimer, &QTimer::timeout, this, [this]() {
                applySceneTreeIncremental("timer");
            });
        }
        if (!m_sceneTreeObserver)
        {
            m_sceneTreeObserver = std::make_unique<SceneTreeSceneObserver2D>([this]() {
                onSceneTreeSceneChanged();
            });
        }
        scene->addObserver(m_sceneTreeObserver.get());
    }

    // 初始填充
    refreshSceneTree();
}

void Workbench2D::onSceneTreeSceneChanged()
{
    // 结构签名没变（例如只是拖了几何）就不重建：原来这里按「图元数量」判断，
    // 但群组拓扑变化不改数量，会让新建/解散的群组在树上残留。
    // 增量优先：纯新增只追加顶层行；删除/群组/改名等回退全量。
    applySceneTreeIncremental("sceneObserver");
}

void Workbench2D::refreshSceneTree()
{
    if (!m_scenePanel2D)
    {
        return;
    }
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    // 拓扑：O(N) 一次生成紧凑索引；群组成员与元数据均懒加载
    const SceneTreeTopology2D topology = SceneTreeBuilder2D::buildTopology(scene);
    m_scenePanel2D->setMode2D(
        topology,
        [scene, layers = m_scene.layerManager](qint64 id, bool isGroup) {
            return SceneTreeBuilder2D::rowMeta(scene, layers, SceneTreeRow2D{ id, isGroup });
        },
        [scene](qint64 groupId) {
            return SceneTreeBuilder2D::groupMembers(scene, groupId);
        });

    // 记录本轮重建时的结构签名：之后的 sceneChanged 只要签名没变就不必再建
    m_lastSceneTreeEntityCount = scene ? scene->getEntityCount() : 0;
    m_lastSceneTreeStructureRevision = scene ? scene->structureRevision() : 0;
    m_lastSceneTreeTopologyRevision = scene ? scene->groupManager().topologyRevision() : 0;

    // 全量重建已消费掉当前全部变更：推进增量游标，之后增量只读真正的新变更
    m_sceneTreeCursor = scene ? scene->currentRevision() : m_sceneTreeCursor;

    // 打点：上报本轮重建的触发来源，并立刻清零（避免后续重建误用旧标记）。
    const char* src = m_sceneTreeRefreshSource ? m_sceneTreeRefreshSource : "misc";
    m_sceneTreeRefreshSource = nullptr;
    SY_INFOF("[Workbench2D] scene tree rebuilt src=%s: topLevel=%d entities=%zu structRev=%llu topoRev=%llu",
        src,
        static_cast<int>(topology.topLevel.size()),
        m_lastSceneTreeEntityCount,
        static_cast<unsigned long long>(m_lastSceneTreeStructureRevision),
        static_cast<unsigned long long>(m_lastSceneTreeTopologyRevision));
}

void Workbench2D::refreshSceneTreeIfNeeded(const char* src)
{
    if (!m_scenePanel2D)
    {
        return;
    }
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }

    // 结构签名：图元数量 + 增删修订 + 群组拓扑修订。拖动图元只推进几何修订，
    // 不会命中这里，因此不会把万级场景的树反复重建。
    const std::size_t count = scene->getEntityCount();
    const uint64_t structureRev = scene->structureRevision();
    const uint64_t topologyRev = scene->groupManager().topologyRevision();
    if (count == m_lastSceneTreeEntityCount && structureRev == m_lastSceneTreeStructureRevision &&
        topologyRev == m_lastSceneTreeTopologyRevision)
    {
        return;
    }

    // 先记下已消费的签名再启表：否则连续 sceneChanged（拖动中）每次都会重启单次定时器，
    // 重建被无限推迟；重建函数结束后会把签名再刷成最新值。
    m_lastSceneTreeEntityCount = count;
    m_lastSceneTreeStructureRevision = structureRev;
    m_lastSceneTreeTopologyRevision = topologyRev;

    // 打点：记住是哪个来源发起的本轮重建（首个请求者赢，后续 gated 调用不会覆盖）。
    if (src && !m_sceneTreeRefreshSource)
    {
        m_sceneTreeRefreshSource = src;
    }

    if (m_sceneTreeRefreshTimer)
    {
        // 防抖：批量增删（导入/阵列）合并成一次重建
        m_sceneTreeRefreshTimer->start();
    }
    else
    {
        refreshSceneTree();
    }
}

void Workbench2D::applySceneTreeIncremental(const char* src)
{
    if (!m_scenePanel2D || !m_scene.sceneEditService)
    {
        return;
    }

    // 重入保护：本函数在「分类变更 → 追加行」期间会触发嵌套的场景通知
    // （模型信号 → 场景写回 → SceneNotifier → onSceneTreeSceneChanged），
    // 而嵌套调用读到的 m_sceneTreeCursor 还是外层尚未推进的旧值，
    // 于是把同一批变更又整表扫一遍、还会再追加一遍行（29 万图元下即为一次完整重扫）。
    // 外层正在消费的就是这批变更，嵌套调用不再重复处理。
    //
    // 但不能直接丢弃：嵌套期间新产生的变更若无人再通知就会漏掉。
    // 因此投递一次延后重跑，等到下一轮事件循环时外层已返回、游标已推进，
    // 那时只会读到真正的新变更。收敛性：重跑若读到空变更且签名一致会直接返回，不再投递。
    if (m_sceneTreeIncrementalBusy)
    {
        QTimer::singleShot(0, this, [this]() { applySceneTreeIncremental("deferred"); });
        return;
    }

    // 下面分支多（多个 return），用作用域守卫统一复位，避免漏写导致后续调用被永久丢弃
    struct BusyGuard
    {
        bool& flag;
        explicit BusyGuard(bool& f)
            : flag(f)
        {
            flag = true;
        }
        ~BusyGuard() { flag = false; }
    } busyGuard(m_sceneTreeIncrementalBusy);

    Eg::SceneManager* scene = m_scene.sceneEditService->sceneManager();
    if (!scene)
    {
        return;
    }

    // 锁定态翻转不写变更流（recordChange 不覆盖锁定），增量判据会跳过；
    // 置位强制标志时直接全量重建，行锁图标随 setTopology 快径 dataChanged 刷新。
    if (m_sceneTreeForceRefresh)
    {
        m_sceneTreeForceRefresh = false;
        if (src)
        {
            m_sceneTreeRefreshSource = src;
        }
        refreshSceneTree();
        return;
    }

    // 群组拓扑变化（建组/解散/成员变动）无法用「追加顶层行」表达：新图元可能进了组、
    // 群组行本身也会变，回退防抖全量重建。
    if (scene->groupManager().topologyRevision() != m_lastSceneTreeTopologyRevision)
    {
        refreshSceneTreeIfNeeded(src);
        return;
    }

    // 读上次游标以来的变更。游标太旧（变更日志被压实截断）或场景被整体重置时
    // readChanges 返回 false，此时无法可靠重建增量，推进游标后回退全量。
    Eg::SceneChangeSet set;
    if (!scene->readChanges(m_sceneTreeCursor, set))
    {
        m_sceneTreeCursor = scene->currentRevision();
        refreshSceneTreeIfNeeded(src);
        return;
    }
    m_sceneTreeCursor = set.toRevision;

    if (set.changes.empty())
    {
        // 无新变更：签名一致（已被全量/增量消费）则无事可做；不一致（如锁定
        // 切换后手动启动的定时器）则全量刷新行内容。
        const std::size_t count = scene->getEntityCount();
        if (count != m_lastSceneTreeEntityCount || scene->structureRevision() != m_lastSceneTreeStructureRevision)
        {
            if (src)
            {
                m_sceneTreeRefreshSource = src;
            }
            refreshSceneTree();
        }
        return;
    }

    // 纯「非结构」变更（几何/样式/选中）：场景树的行集合、分组、图标都不变，
    // 无需任何刷新。否则移动/缩放图元时每帧的 GeometryChanged 都会落到下方
    // hasNonAdd 分支触发全量 rebuild（O(场景规模)），万级场景拖动即卡死。
    bool hasTreeRelevant = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Removed) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::StructureChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::LayerChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::VisibilityChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::LockChanged))
        {
            hasTreeRelevant = true;
            break;
        }
    }
    if (!hasTreeRelevant)
    {
        // 只有 GeometryChanged / StyleChanged / SelectionChanged：树无关，消费掉即可。
        // 游标已在上方推进到 toRevision，这里无需再动。
        return;
    }

    // 分类变更集：只有「全部是 Added 且图元当前仍在场景」才可增量；出现 Removed、
    // 改名/几何等非结构变更，或 Added 但已离场（同批删除的竞态），一律回退全量。
    // 注意撤销删除会把图元加回来（Added + Removed 位同时置上），必须按
    // 「当前是否在场景里」判定，不能只信 kinds 位。
    QVector<SceneTreeRow2D> added;
    added.reserve(static_cast<int>(set.changes.size()));
    bool hasNonAdd = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (!Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added))
        {
            hasNonAdd = true;
            break;
        }
        if (!scene->findEntityById(ch.entityId))
        {
            hasNonAdd = true;
            break;
        }
        added.push_back({ static_cast<qint64>(ch.entityId), false });
    }

    if (hasNonAdd)
    {
        if (src)
        {
            m_sceneTreeRefreshSource = src;
        }
        refreshSceneTree();
        return;
    }

    if (added.isEmpty())
    {
        return;  // 理论不可达：变更集非空且全部是 Added 时至少一条
    }

    // 纯新增：增量追加顶层行（O(新增数)），不 reset 模型，视图展开/滚动状态保留
    m_scenePanel2D->appendTopLevelRows(added);

    // 与 refreshSceneTree() 尾部一致地更新结构签名（不推进游标：已在上面推进到 toRevision）
    m_lastSceneTreeEntityCount = scene->getEntityCount();
    m_lastSceneTreeStructureRevision = scene->structureRevision();
    m_lastSceneTreeTopologyRevision = scene->groupManager().topologyRevision();

    SY_DEBUGF("[Workbench2D] scene tree incremental src=%s: +%lld rows entities=%zu structRev=%llu",
        src ? src : "misc",
        added.size(),
        m_lastSceneTreeEntityCount,
        static_cast<unsigned long long>(m_lastSceneTreeStructureRevision));
}

void Workbench2D::syncSceneTreeSelection()
{
    if (!m_scenePanel2D)
    {
        return;
    }
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    auto selected = SceneTreeBuilder2D::selectedIds(scene);
    // SY_DEBUGF("[Workbench2D] syncSceneTreeSelection: selected count=%d", selected.size());
    m_scenePanel2D->setSelectedIds(selected);
}

void Workbench2D::applySceneTreeSelection(const QStringList& ids)
{
    if (!m_scene.selectionService)
    {
        return;
    }
    if (ids.isEmpty())
    {
        m_scene.selectionService->clear();
        return;
    }

    std::vector<std::string> storage;
    storage.reserve(ids.size());
    for (const QString& id : ids)
    {
        storage.push_back(id.toStdString());
    }
    std::vector<const char*> cids;
    cids.reserve(storage.size());
    for (const std::string& str : storage)
    {
        cids.push_back(str.c_str());
    }
    m_scene.selectionService->selectMultiple(cids.data(), cids.size());
}

void Workbench2D::toggleEntityVisibility(const QString& id, bool visible)
{
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* entity = scene->findEntityById(*eid))
    {
        entity->setVisible(visible);
        scene->notifySceneChanged();
        // 可见性不进结构签名（不是增删/拓扑变化），且树行要显示新的显隐图标，
        // 延迟到下一事件循环重建，避免连续点击导致多次完整重建
        QTimer::singleShot(0, this, [this]() { refreshSceneTree(); });
    }
}

void Workbench2D::renameEntity(const QString& id, const QString& newName)
{
    if (newName.isEmpty() || !m_scene.sceneEditService)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    Eg::SceneManager* scene = m_scene.sceneEditService->sceneManager();
    if (!scene)
    {
        return;
    }

    // 走可撤销编辑路径：重命名作为独立撤销命令入栈（前后快照）。
    // 若直接写引擎，重命名不会进入撤销栈，之后撤销任何较早的操作都会用
    // 旧快照把名字覆盖回去（表现为"撤销后名字变回原来的"）。
    const std::string name = newName.toStdString();
    m_scene.sceneEditService->mutateEntities(
        { *eid },
        [scene, entityId = *eid, name]() {
            if (auto* entity = scene->findSyEntityById(entityId))
            {
                entity->setName(name.c_str());
            }
        },
        "Rename");

    // 改名不是结构变化（签名不动），但树行要显示新名字，这里显式重建一次。
    // 撤销改名同样依赖它：undoStateChanged 走的是 refreshSceneTree()（无条件）。
    SY_DEBUGF("[Workbench2D] renameEntity: id=%lld name=%s", static_cast<long long>(*eid), name.c_str());
    refreshSceneTree();
}

void Workbench2D::deleteSceneTreeSelection(const QStringList& ids)
{
    if (ids.isEmpty() || !m_scene.sceneEditService)
    {
        return;
    }
    std::vector<Eg::EntityId> eids;
    eids.reserve(ids.size());
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (eid)
        {
            eids.push_back(*eid);
        }
    }
    if (eids.empty())
    {
        return;
    }
    m_scene.sceneEditService->deleteEntities(eids, "Delete from Scene Tree");
    // 删除后刷新树与选择（deleteEntities 为可撤销路径）
    refreshSceneTree();
    syncSceneTreeSelection();
}

void Workbench2D::setSceneTreeVisibility(const QVector<qint64>& ids, bool visible)
{
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }

    // 整数 id 直接转 EntityId，批量设置（内部经回调记录 VisibilityChanged，
    // 渲染侧走增量路径）。避免逐个 QString→std::string→parse 的开销。
    std::vector<Eg::EntityId> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<Eg::EntityId>(id));
    }
    scene->setEntitiesVisible(entityIds, visible);

    // 隐藏图元时从选择集中移除：重建为「仅保留仍可见的」
    if (!visible)
    {
        std::vector<Eg::SyEntity*> stillVisible;
        bool hadHiddenSelected = false;
        scene->forEachSelected([&stillVisible, &hadHiddenSelected](Eg::SyEntity* e) {
            if (!e)
            {
                return;
            }
            if (e->visible())
            {
                stillVisible.push_back(e);
            }
            else
            {
                hadHiddenSelected = true;
            }
        });
        if (hadHiddenSelected)
        {
            // selectEntities 是整批替换语义，空列表即清空选择，一次通知完成
            scene->selectEntities(stillVisible);
        }
    }

    // 可见性影响渲染，需触发一次场景通知（经 16ms 节流合帧）
    scene->notifySceneChanged();

    // 场景树增量刷新受影响行（复选框 + 文本），不重建拓扑、不 reset 模型
    if (m_scenePanel2D)
    {
        m_scenePanel2D->refreshRows(ids);
    }
}

void Workbench2D::setSceneTreeLock(const QVector<qint64>& ids, bool locked)
{
    Eg::SceneManager* scene = m_scene.sceneEditService ? m_scene.sceneEditService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }

    // 批量锁定（内部经回调记录 LockChanged）。锁定是纯元数据：不改几何、不影响渲染，
    // 因此这里**不**调 notifySceneChanged()，避免无谓的视口刷新。
    std::vector<Eg::EntityId> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<Eg::EntityId>(id));
    }
    scene->setEntitiesLocked(entityIds, locked);

    // Lock/Unlock 菜单项的灰显依赖 anyLocked，刷新命令状态即可。
    refreshCommandUiState();

    // 锁定态当前不在场景树中可视化（SceneTreeRowMeta2D::locked 未接入模型渲染），
    // 因此无需刷新树。若将来加锁图标，用 m_scenePanel2D->refreshRows(ids) 增量刷新。
}

void Workbench2D::onViewportContextMenu(QContextMenuEvent* event)
{
    if (!event || !m_commandHub || !m_scene.layerManager)
    {
        return;
    }

    // 基于命令中枢实时快照构建菜单（count / 锁定 / 类型 来自与工具栏相同的单一事实来源），
    // 避免右键菜单再走一套独立的选择数据源导致显隐/灰显规则漂移。
    const CommandUiSnapshot snapshot = m_commandHub->captureSnapshot(m_commandHub->mainWindow());

    // 配置驱动优先：客户 JSON 声明了 contextMenus["canvas.2d"] 时由配置接管。
    if (QMenu* configured = buildConfiguredContextMenu(QStringLiteral("canvas.2d"), snapshot.hasSelection))
    {
        // 配置化菜单的 QAction 是每次弹出新建的临时对象，不在中枢的动作表里，
        // refreshActionStates / WorkbenchMenuManager::refreshCommandStates 都触达不到。
        // 弹出前按同一份快照与同一份目录规则应用启用态，否则右键里的
        // Cut / Copy / Delete / Paste 会在空选或选中锁定图元时仍是亮态，
        // 与同名工具栏按钮的灰显状态直接矛盾。
        CommandActionHub::applySnapshotToMenu(configured, snapshot);

        // 生命周期：菜单在本作用域内建、本作用域内销毁。
        // 注：曾经这里写「因为 dispatcher 是栈对象所以不能 deleteLater」，那个前提本身是错的 ——
        // dispatcher 早在 buildConfiguredContextMenu 返回时就没了。现在分发器是工作台本体，
        // 菜单何时销毁都安全，仍就地 delete 只是为了不把临时 QAction 留给事件循环。
        configured->exec(event->globalPos());
        delete configured;
        return;
    }

    QMenu menu;
    m_commandHub->populateContextMenu(&menu, snapshot, m_scene.layerManager);
    if (menu.isEmpty())
    {
        return;
    }
    menu.exec(event->globalPos());
}

QMenu* Workbench2D::buildConfiguredContextMenu(const QString& contextMenuId, bool hasSelection)
{
    const UiConfigData* config = UiConfigurationManager::shared().configData();
    if (!UiContextMenuService::hasConfigFor(config, contextMenuId))
    {
        return nullptr;
    }

    // 分发器直接用工作台自身（UiWorkbench 实现 IUiCommandDispatcher）：
    // UiLayoutBuilder 把 dispatcher 裸指针捕进 QAction 的 triggered 闭包，闭包活到菜单析构，
    // 而菜单在调用方 exec()，此处再建局部适配器就是悬垂指针。
    return UiContextMenuService::instance().buildMenu(config, contextMenuId, this, m_commandHub->mainWindow());
}

void Workbench2D::refreshCommandUiState()
{
    if (!m_commandHub)
    {
        return;
    }

    // 一次抓取，多处消费：refreshActionStates 内部会广播 selectionContextChanged，
    // 由 applySelectionContext 扇出到属性面板/状态栏/场景树/工具栏上下文。
    m_commandHub->refreshActionStates(m_commandHub->captureSnapshot(m_commandHub->mainWindow()));
}

void Workbench2D::applySelectionContext(const CommandUiSnapshot& snapshot)
{
    // 选择上下文的**唯一扇出点**：入参这份快照就是全部事实来源。
    // 新增需要随选择/锁定联动的组件时，在这里加一次推送，组件自身只渲染、不判定；
    // 反过来，这里任何一处再去问场景或问 Hub 缓存，都会把「规则漂移」放回来。

    // 属性面板：10Hz 节流，避免拖动期间每帧重建（60fps → 10fps）
    // 使用 singleShot 延迟 100ms，同一窗口内多次调用只执行一次
    QTimer::singleShot(100, this, [this]() { refreshPropertiesPanel(); });

    // 状态栏选择指示器：直接用快照里的 selectionCount，不再二次遍历场景
    if (m_statusBar2D)
    {
        const int n = snapshot.selectionCount;
        m_statusBar2D->setSelectionInfo(n, tr("Selected: %1").arg(n));
    }

    // 场景树右键菜单：与视口右键菜单共用同一份 hasSelection / anyLocked 判定，消除规则漂移。
    // 注意：锁定态当前不在树中可视化，因此 setCommandState 只用于更新右键菜单灰显。
    if (m_scenePanel2D)
    {
        m_scenePanel2D->setCommandState(snapshot.hasSelection, snapshot.anyLocked());
    }

    // 菜单栏：菜单项是独立于命令中枢创建的 QAction（config-driven 与 legacy 两条构建路径），
    // 中枢的 refreshActionStates 触达不到，故在此按同一份快照统一刷新启用态。
    if (m_workbenchWindow)
    {
        if (WorkbenchMenuManager* menus = m_workbenchWindow->menuManager())
        {
            menus->refreshCommandStates(snapshot);
        }
    }

    // 顶部工具栏上下文：按同一份快照的 typeMask 切换（Text/QR/Bitmap/Vector/Default）
    if (m_contextManager)
    {
        const ToolBarContext newCtx = determineContextFromSelection(snapshot);
        if (m_contextManager->currentContext() != newCtx)
        {
            m_contextManager->setCurrentContext(newCtx);
        }
    }
}

void Workbench2D::refreshPropertiesPanel()
{
    // 属性面板是可选的 UI：配置驱动时可能不存在，因此先探测再绑定。
    if (!m_workbenchWindow)
    {
        return;
    }
    auto* props = m_workbenchWindow->propertiesDock();
    if (!props)
    {
        return;
    }

    // 读取当前选中图元 id（数据来源：引擎场景）
    std::vector<Eg::EntityId> entityIds;
    if (m_scene.sceneEditService)
    {
        if (auto* scene = m_scene.sceneEditService->sceneManager())
        {
            for (Eg::SyEntity* e : scene->getSelectedEntities())
            {
                if (e)
                {
                    entityIds.push_back(e->id);
                }
            }
        }
    }

    // 创建编辑会话（算法层）：持有图元 id，负责按需解析图元、应用修改并集成撤销。
    auto session = std::make_shared<EntityPropertyEditSession2D>(m_scene.sceneEditService, std::move(entityIds));

    // 数据/算法产物推送给 UI 层：模型用于展示，会话作为编辑目标。
    // 面板仅消费 PropertyModel / IPropertyEditTarget，不感知算法与引擎细节。
    props->setEditTarget(session);
    props->setPropertyModel(session->buildModel());
    // 同步锁定态：属性面板双击编辑与工具栏/右键菜单的禁用规则实时一致
    // （来自命令中枢最近一次选择上下文快照，单一事件总线驱动）
    // m_commandHub 在 deactivate() 里被 reset，而本函数可能由已投递的 QTimer::singleShot
    // 回调（属性面板编辑 → sigPropertyEdited）在切换过程中被派发到，必须判空。
    if (m_commandHub)
    {
        props->setLockState(m_commandHub->currentSnapshot().anyLocked());
    }
}

void Workbench2D::activate()
{
    // 从状态快照恢复（首次激活使用 m_initialState，后续使用 m_savedState）
    const auto& snapshot = m_savedState.currentViewMode.isEmpty() ? m_initialState : m_savedState;
    restoreFromSnapshot(snapshot);

    // 恢复工具状态：将快照中的工具 ID 应用到视口
    if (m_viewport && !snapshot.activeToolId.isEmpty())
    {
        m_viewport->setActiveTool(snapshot.activeToolId);
        SY_DEBUGF("[Workbench2D] Restored tool: %s", qPrintable(snapshot.activeToolId));
    }

    // 激活时刷新一次动作状态（撤销/重做可用性 + 选中项驱动的启用态）
    refreshCommandUiState();
}

void Workbench2D::deactivate()
{
    m_savedState = currentSnapshot();

    // 断开 metadataChanged 连接，防止切换工作台后悬空视口指针回调
    if (m_gridVisibilityMetadataConn)
    {
        QObject::disconnect(m_gridVisibilityMetadataConn);
        m_gridVisibilityMetadataConn = {};
    }

    // 断开长命对象到本工作台的所有连接。
    // operationBus / layerManagerBridge 跨工作台存活，而 Workbench2D 实例被 UiShellHost
    // 缓存复用、切换时不销毁 —— Qt 不会自动断开，attachToWindow 每次又重新 connect 一遍。
    // 不断的后果不是崩溃而是叠加：N 次 2D↔3D 往返后，一次 2D 操作会触发 N 份场景树重建
    // 与 N 份命令状态刷新。这些连接全部在 attach 期建立（含 UiStateBridge2D::install），
    // 因此按"发送者 + 接收者"整体断开是安全的，下次 attach 会重新装。
    if (m_commands.operationBus)
    {
        QObject::disconnect(m_commands.operationBus, nullptr, this, nullptr);
    }
    if (m_scene.layerManagerBridge)
    {
        QObject::disconnect(m_scene.layerManagerBridge, nullptr, this, nullptr);
    }

    // 清除 ImportService 中持有的视口回调，防止切换后悬空指针

    // ImportService 生命周期长于工作台，不清理会导致 use-after-free
    if (m_persistence.importService)
    {
        m_persistence.importService->setViewportFitCallback(nullptr);
        m_persistence.importService->setTreeRebuildCallback(nullptr);
        m_persistence.importService->setPropertyRefreshCallback(nullptr);
        m_persistence.importService->setDisplayRefreshCallback(nullptr);
    }

    // 同理：FileDropHandler 由 WorkbenchWindow 持有（跨切换永生），它的
    // 屏幕→世界换算回调按值捕获了 RenderViewport2D*。装的是 application 级事件
    // 过滤器，所以切到 3D 之后往窗口拖一张图片同样会走进来 → 解引用已析构的视口。
    if (m_workbenchWindow)
    {
        if (auto* fdh = m_workbenchWindow->fileDropHandler())
        {
            fdh->setScreenToWorldConverter(nullptr);
        }
    }

    // 清理命令动作中枢（unique_ptr 管理生命周期，reset 释放所有权）
    m_commandHub.reset();

    // 视口动作中枢：断开当前视口，避免切换工作台后悬空指针
    if (m_view.viewportActionHub)
    {
        m_view.viewportActionHub->clearViewport();
    }

    // 工具栏指针会随窗口清理而失效（clearWorkbenchContent 会 delete QToolBar）
    m_topToolBar = nullptr;
    m_rightToolBar = nullptr;
    m_textFontToolBar = nullptr;
    m_textFontToolBarWidget = nullptr;
    // 视口指针随窗口清理而失效
    m_viewport = nullptr;
    // 注销场景变更观察者并停用防抖定时器，避免切换工作台后悬空
    if (m_sceneTreeObserver && m_scene.sceneEditService)
    {
        if (auto* scene = m_scene.sceneEditService->sceneManager())
        {
            scene->removeObserver(m_sceneTreeObserver.get());
        }
    }
    m_sceneTreeObserver.reset();
    if (m_sceneTreeRefreshTimer)
    {
        m_sceneTreeRefreshTimer->stop();
        m_sceneTreeRefreshTimer->deleteLater();
        m_sceneTreeRefreshTimer = nullptr;
    }
    m_lastSceneTreeEntityCount = 0;
    m_lastSceneTreeStructureRevision = 0;
    m_lastSceneTreeTopologyRevision = 0;
    // 场景树面板指针随窗口清理而失效
    m_scenePanel2D = nullptr;

    // 场景监视器每次 attachToWindow 都会 new 一个（setupViewportServices），
    // 若不在此销毁，往返切换 N 次后会有 N 个监视器同时 watch 同一场景，
    // 一次场景变化触发 N 次 refreshCommandUiState（幂等但白做功，且随使用时长线性增长）。
    if (m_sceneMonitor)
    {
        m_sceneMonitor->deleteLater();
        m_sceneMonitor = nullptr;
    }

    // 命令 UI 连线的生命周期句柄。上面销毁监视器只断得掉 sceneChanged 那一路；
    // bus / layerBridge 是 UiServices 的成员，跨工作台长寿，接收方本对象同样长寿，
    // 两端都不死的连线 Qt 永远不会自动回收——必须显式销毁句柄整批断开。
    if (m_uiStateConnections)
    {
        m_uiStateConnections->deleteLater();
        m_uiStateConnections = nullptr;
    }

    // 工具栏上下文管理器持有的 setter/clearer lambda 捕获了已置空的 m_topToolBar；
    // 同时上下文本身（如 TextEditing）不该跨工作台保留，否则回到 2D 时
    // 工具栏会在旧上下文里重建。
    if (m_contextManager)
    {
        m_contextManager->setCurrentContext(ToolBarContext::Default);
    }
}

void Workbench2D::shutdown()
{
    deactivate();
    m_uiState = {};
    m_commands = {};
    m_scene = {};
    m_persistence = {};
    m_view = {};
}

void Workbench2D::releaseCentralWidgetGLResources(QWidget* centralWidget) const
{
    if (auto* vp = qobject_cast<RenderViewport2D*>(centralWidget))
    {
        vp->releaseGLResources();
    }
}

