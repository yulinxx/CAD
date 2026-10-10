#include "Workbench2DToolbarFactory.h"
#include "Workbench2D.h"
#include "WorkbenchWindow.h"
#include "RenderViewport2D.h"
#include "DrawToolBarWidget.h"
#include "RenderWidget.h"
#include "UI2D/DrawTools/ToolManager.h"
#include "UI2D/ToolBar/TopToolBar.h"
#include "UI2D/ToolBar/RightToolBar.h"
#include "UI2D/ToolBar/TextFontToolBar.h"
#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI/Service/ToolBarContextManager.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/ISelectionService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/EntityClipboard.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine/Edit/IUndoRedoManager.h"
#include "Engine/EntityIdUtils.h"
#include "Engine/SyEntity/SyEntity.h"
#include "UI/DrawTools/TextEditTool.h"
#include "UI/UiMetrics.h"
#include "UI2D/Dlg/LayerManagerDialog.h"
#include "Log/SyLogger.h"
#include <QAction>
#include <QToolBar>
#include <QVariantMap>

Workbench2DToolbarFactory::Workbench2DToolbarFactory(QObject* parent)
    : QObject(parent)
{
}

void Workbench2DToolbarFactory::createToolbars(WorkbenchWindow& window, RenderViewport2D* viewport, OperationBus* bus,
    UiStateCenter* stateCenter, CommandActionHub* hub, PanelHostStyle style,
    IUndoRedoManager* undoManager, Eg::EntityClipboard* clipboard,
    ISelectionService* selectionService, LayerManager* layerManager,
    SceneEditService* sceneEditService)
{
    m_panelHostStyle = style;

    // View → Grid & Snap 菜单的真正生效点：把 stateCenter 元数据映射到网格显隐。
    // 菜单/操作只翻转 metadata(gridVisible)，此处作为唯一消费者同步到视口网格渲染。
    if (stateCenter && viewport)
    {
        const auto applyGridVisibleFromMetadata = [stateCenter, viewport]() {
            // gridVisible 只由 View → Grid 菜单写入，键不存在时不能当 false 用
            const QVariant value = stateCenter->metadata().value(QStringLiteral("gridVisible"));
            if (!value.isValid())
            {
                return;
            }
            if (auto* renderWidget = viewport->renderWidget())
            {
                if (auto* env = renderWidget->sceneEnvironment())
                {
                    env->setGridVisible(value.toBool());
                    env->notifyChanged();
                }
            }
        };
        // 长命 stateCenter → 本工厂的连线捕获当次视口裸指针，必须留句柄给 shutdown 断开，
        // 否则切台后视口销毁、metadata 再变化就会解引用悬空指针
        m_gridMetadataConn =
            QObject::connect(stateCenter, &UiStateCenter::metadataChanged, this, applyGridVisibleFromMetadata);

        // 用场景当前（已由设置应用过）的可见性给元数据播种，之后菜单勾选态与画布才一致。
        if (auto* renderWidget = viewport->renderWidget())
        {
            if (auto* env = renderWidget->sceneEnvironment())
            {
                QVariantMap meta = stateCenter->metadata();
                if (!meta.value(QStringLiteral("gridVisible")).isValid())
                {
                    meta.insert(QStringLiteral("gridVisible"), env->settings().grid.visible);
                    stateCenter->setMetadata(meta);
                }
            }
        }
        applyGridVisibleFromMetadata();
    }

    // CommandActionHub：管理所有 QAction 的创建与绑定
    hub->setMainWindow(&window);
    hub->setOperationBus(bus);
    // 单一数据源：一次遍历选中的图元集合，统一算出 count / 锁定(图层+图元) / 可编辑 /
    // 类型直方图 / 分组，注入给命令中枢
    hub->setSelectionContextProvider([selectionService, layerManager, sceneEditService]() -> SelectionContext {
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
        // 分组按钮：需有选中图元且未命中任意锁定（图层锁或图元锁）且未隐藏
        result.groupEnabled =
            result.hasSelection && !(result.anyLockedLayer || result.anyLockedEntity || result.anyHidden);
        // 贝塞尔切换按钮：当前无独立语义，保持禁用（与重构前未赋值行为一致）
        result.bezierEnabled = false;
        return result;
    });
    if (!undoManager)
    {
        // provider 漏注入会让撤销/重做被钉死为置灰，接线时就要喊出来
        SY_WARN("[Workbench2DToolbarFactory] UiServices::undoManager is null, Undo/Redo actions will stay disabled");
    }
    hub->setUndoRedoProvider([undoManager]() -> UndoRedoState {
        UndoRedoState state;
        if (undoManager)
        {
            state.canUndo = undoManager->canUndo();
            state.canRedo = undoManager->canRedo();
        }
        return state;
    });
    // Paste 按钮启用状态实时反映剪贴板是否已有图元
    hub->setClipboardProvider([clipboard]() -> bool {
        return clipboard && clipboard->hasContent();
    });
    hub->rebuildAllActions();

    auto* drawWidget = new DrawToolBarWidget(&window);

    drawWidget->setIsPanModeCallback([this, viewport]() {
        return viewport && viewport->isPanModeEnabled();
    });
    drawWidget->setPanModeToggleCallback([this, viewport]() {
        if (viewport)
        {
            viewport->setPanModeEnabled(!viewport->isPanModeEnabled());
            return true;
        }
        return false;
    });

    const QVector<QAction*> drawToolActions = buildDrawToolActions(*hub);
    drawWidget->setToolActions(drawToolActions);

    if (viewport)
    {
        QObject::connect(viewport, &RenderViewport2D::panModeChanged, drawWidget, &DrawToolBarWidget::setPanMode);
    }

    SY_DEBUGF("[Workbench2DToolbarFactory] Draw tool panel built: tools=%d host=%s",
        static_cast<int>(drawToolActions.size()),
        m_panelHostStyle == PanelHostStyle::Dock ? "Dock" : "ToolBar");

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

    QObject::connect(
        viewport, &RenderViewport2D::activeToolChanged, hub, &CommandActionHub::setActiveToolAction);
    hub->setActiveToolAction(viewport->activeToolName());

    QObject::connect(viewport, &RenderViewport2D::activeToolChanged, drawWidget, &DrawToolBarWidget::setCurrentToolName);

    QObject::connect(viewport, &RenderViewport2D::activeToolChanged, this, [this, viewport](const QString& toolName) {
        if (toolName == QStringLiteral("TextEditTool") && m_textFontToolBarWidget && viewport)
        {
            auto* toolMgr = viewport->toolManager();
            if (toolMgr)
            {
                ITool* tool = toolMgr->getTool(QStringLiteral("TextEditTool"));
                if (tool && tool->isTextEditTool())
                {
                    auto* textEditTool = static_cast<TextEditTool*>(tool);
                    m_textFontToolBarWidget->bindTool(textEditTool);

                    textEditTool->setEditingStateChangedCallback([this](bool editing) {
                        if (m_contextManager)
                        {
                            const ToolBarContext targetCtx =
                                editing ? ToolBarContext::TextEditing : ToolBarContext::Default;
                            if (m_contextManager->currentContext() != targetCtx)
                            {
                                m_contextManager->setCurrentContext(targetCtx);
                            }
                        }
                    });
                }
            }
        }
    });

    m_topToolBar = new TopToolBar(&window);
    m_topToolBar->setObjectName(QStringLiteral("TopToolBar"));
    m_topToolBar->setCommandActionHub(hub);
    window.addToolBar(Qt::TopToolBarArea, m_topToolBar);

    m_textFontToolBar = new QToolBar(QObject::tr("Text Font"), &window);
    m_textFontToolBar->setObjectName(QStringLiteral("TextFontToolBar"));
    m_textFontToolBar->setMovable(false);
    m_textFontToolBar->setIconSize(QSize(UiMetrics::toolbarIconSizeSmall(), UiMetrics::toolbarIconSizeSmall()));
    m_textFontToolBarWidget = new TextFontToolBar(m_textFontToolBar);
    m_textFontToolBar->addWidget(m_textFontToolBarWidget);
    window.addToolBar(Qt::TopToolBarArea, m_textFontToolBar);
    m_textFontToolBar->setVisible(false);

    m_contextManager = std::make_unique<ToolBarContextManager>();

    m_contextManager->registerContext(ToolBarContext::Default,
        {
            ToolBarContext::Default,
            QObject::tr("Edit"),
            {
                { "",
                    {
                        { "edit.undo", QObject::tr("Undo"), ":/ui/common/Icons/Actions/undo.svg" },
                        { "edit.redo", QObject::tr("Redo"), ":/ui/common/Icons/Actions/redo.svg" },
                    } },
                { "",
                    {
                        { "edit.mirror_horizontal", QObject::tr("Mirror H"), ":/ui/common/Icons/Actions/mirror_h.svg" },
                        { "edit.mirror_vertical", QObject::tr("Mirror V"), ":/ui/common/Icons/Actions/mirror_v.svg" },
                    } },
                { "",
                    {
                        { "edit.align_left", QObject::tr("Align Left"), ":/ui/common/Icons/Actions/align_left.svg" },
                        { "edit.align_right", QObject::tr("Align Right"), ":/ui/common/Icons/Actions/align_right.svg" },
                        { "edit.align_center_h", QObject::tr("Align Center H"), ":/ui/common/Icons/Actions/align_center_h.svg" },
                        { "edit.align_top", QObject::tr("Align Top"), ":/ui/common/Icons/Actions/align_top.svg" },
                        { "edit.align_bottom", QObject::tr("Align Bottom"), ":/ui/common/Icons/Actions/align_bottom.svg" },
                        { "edit.align_center_v", QObject::tr("Align Center V"), ":/ui/common/Icons/Actions/align_center_v.svg" },
                    } },
                { "",
                    {
                        { "edit.select_all", QObject::tr("Select All"), ":/ui/common/Icons/Actions/select_all.svg" },
                        { "edit.invert_selection", QObject::tr("Invert Selection"), ":/ui/common/Icons/Actions/invert_selection.svg" },
                        { "edit.deselect", QObject::tr("Deselect"), ":/ui/common/Icons/Actions/deselect.svg" },
                    } },
                { "",
                    {
                        { "edit.copy", QObject::tr("Copy"), ":/ui/common/Icons/Actions/copy.svg" },
                        { "edit.paste", QObject::tr("Paste"), ":/ui/common/Icons/Actions/paste.svg" },
                        { "edit.delete", QObject::tr("Delete"), ":/ui/common/Icons/Actions/delete.svg" },
                    } },
                { "",
                    {
                        { "edit.group", QObject::tr("Group"), ":/ui/common/Icons/Actions/group.svg", true },
                    } },
            },
        });

    m_contextManager->registerContext(ToolBarContext::TextEditing,
        {
            ToolBarContext::TextEditing, QObject::tr("Text Format"), {}
        });

    m_contextManager->registerContext(ToolBarContext::QREditing,
        {
            ToolBarContext::QREditing,
            QObject::tr("QR Code"),
            {
                { QObject::tr("Content"),
                    {
                        { "QR_Content", QObject::tr("Content") },
                        { "QR_ErrorCorrection", QObject::tr("Error Correction") },
                    } },
                { QObject::tr("Appearance"),
                    {
                        { "QR_Size", QObject::tr("Size") },
                        { "QR_Foreground", QObject::tr("Foreground") },
                        { "QR_Background", QObject::tr("Background") },
                    } },
                { QObject::tr("Advanced"),
                    {
                        { "QR_Logo", QObject::tr("Logo") },
                    } },
            },
        });

    m_contextManager->registerContext(ToolBarContext::BitmapEditing,
        {
            ToolBarContext::BitmapEditing,
            QObject::tr("Bitmap"),
            {
                { QObject::tr("Adjust"),
                    {
                        { "Bitmap_Crop", QObject::tr("Crop") },
                        { "Bitmap_Rotate", QObject::tr("Rotate") },
                        { "Bitmap_Brightness", QObject::tr("Brightness") },
                        { "Bitmap_Contrast", QObject::tr("Contrast") },
                    } },
                { QObject::tr("Filter"),
                    {
                        { "Bitmap_Filter", QObject::tr("Filter") },
                    } },
            },
        });

    m_contextManager->registerContext(ToolBarContext::VectorEditing,
        {
            ToolBarContext::VectorEditing,
            QObject::tr("Vector"),
            {
                { QObject::tr("Path"),
                    {
                        { "Vector_NodeEdit", QObject::tr("Node Edit") },
                        { "Vector_Simplify", QObject::tr("Simplify") },
                        { "Vector_Boolean", QObject::tr("Boolean") },
                    } },
                { QObject::tr("Style"),
                    {
                        { "Vector_Stroke", QObject::tr("Stroke") },
                        { "Vector_Fill", QObject::tr("Fill") },
                    } },
            },
        });

    m_contextManager->registerContext(ToolBarContext::ImageEditing,
        {
            ToolBarContext::ImageEditing,
            QObject::tr("Image"),
            {
                { QObject::tr("Transform"),
                    {
                        { "Image_Crop", QObject::tr("Crop") },
                        { "Image_Rotate", QObject::tr("Rotate") },
                        { "Image_Flip", QObject::tr("Flip") },
                    } },
                { QObject::tr("Adjust"),
                    {
                        { "Image_Brightness", QObject::tr("Brightness") },
                        { "Image_Contrast", QObject::tr("Contrast") },
                    } },
                { QObject::tr("Filter"),
                    {
                        { "Image_Filter", QObject::tr("Filter") },
                    } },
            },
        });

    SY_DEBUGF("[ToolBarContextManager] Registered %d context(s)", m_contextManager->contextCount());

    m_contextManager->registerCustomToolBar(ToolBarContext::TextEditing, m_textFontToolBarWidget, false);

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

    QObject::connect(m_contextManager.get(),
        &ToolBarContextManager::customToolBarVisibilityChanged,
        this,
        [this](ToolBarContext ctx, bool visible) {
            if (ctx == ToolBarContext::TextEditing && m_textFontToolBar)
            {
                m_textFontToolBar->setVisible(visible);
            }
        });

    m_contextManager->setCurrentContext(ToolBarContext::Default);

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
}

QVector<QAction*> Workbench2DToolbarFactory::buildDrawToolActions(CommandActionHub& hub)
{
    // 目录是唯一事实来源：surfaces 决定"上不上左侧栏"，中枢决定"动作长什么样、点了干什么"
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
            SY_WARNF("[Workbench2DToolbarFactory] left toolbar entry has no hub action: %s", qPrintable(toolName));
        }
    }
    return actions;
}

ToolBarContext Workbench2DToolbarFactory::determineContextFromSelection(const CommandUiSnapshot& snapshot) const
{
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

    if (text && !qr && !bitmap && !vector)
        return ToolBarContext::TextEditing;
    if (qr && !text && !bitmap && !vector)
        return ToolBarContext::QREditing;
    if (bitmap && !text && !qr && !vector)
        return ToolBarContext::BitmapEditing;
    if (vector && !text && !qr && !bitmap)
        return ToolBarContext::VectorEditing;

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

void Workbench2DToolbarFactory::shutdown()
{
    // metadata 连线捕获的视口随窗口内容一起销毁，必须先断开，杜绝切台后的悬空回调
    QObject::disconnect(m_gridMetadataConn);
    m_gridMetadataConn = {};
    m_topToolBar = nullptr;
    m_rightToolBar = nullptr;
    m_textFontToolBar = nullptr;
    m_textFontToolBarWidget = nullptr;
    m_contextManager.reset();
}
