#include "WorkbenchMenuManager.h"
#include "WorkbenchWindow.h"

#include "Log/SyLogger.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "UiServices.h"
#include "UiFrameworkServices.h"
#include "UI/LanguageManager.h"
#include "UI/ThemeManager.h"
#include "Ui/Dlg/LayerManagerDialog.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/Edit/LayerEditService.h"

#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QSignalBlocker>

#if BUILD_UI3D
#include "UI3D/Operation/CommandCatalog3D.h"
#endif

namespace
{
    inline QAction* setCmdId(QAction* action, const QString& cmdId)
    {
        if (action)
            action->setData(cmdId);
        return action;
    }
}

WorkbenchMenuManager::WorkbenchMenuManager(WorkbenchWindow* window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
}

void WorkbenchMenuManager::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
}

void WorkbenchMenuManager::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void WorkbenchMenuManager::setThemeService(UiThemeService* themeService)
{
    m_themeService = themeService;
}

void WorkbenchMenuManager::setFrameworkServices(const UiFrameworkServices* services)
{
    m_frameworkServices = services;
}

void WorkbenchMenuManager::setUiServices(const UiServices* services)
{
    m_uiServices = services;
}

void WorkbenchMenuManager::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
}

void WorkbenchMenuManager::setWorkbenchFactory(WorkbenchFactory factory)
{
    m_workbenchFactory = std::move(factory);
}

void WorkbenchMenuManager::setViewportZoomHandler(std::function<void(const QString&)> handler)
{
    m_viewportZoomHandler = std::move(handler);
}

void WorkbenchMenuManager::rebuildAllMenus()
{
    if (auto* mb = m_window->menuBar())
        mb->clear();
    m_menuState = {};
    buildMenus();
    bindMenuCommands();
}

void WorkbenchMenuManager::createBaseMenus()
{
    initializeMenuSkeleton();
}

void WorkbenchMenuManager::initializeMenuSkeleton()
{
    buildMenus();
}

void WorkbenchMenuManager::buildMenus()
{
    m_menuState.fileMenu = m_window->menuBar()->addMenu(m_window->tr("File"));
    m_menuState.editMenu = m_window->menuBar()->addMenu(m_window->tr("Edit"));
    m_menuState.drawMenu = m_window->menuBar()->addMenu(m_window->tr("Draw"));
    m_menuState.modifyMenu = m_window->menuBar()->addMenu(m_window->tr("Modify"));
    m_menuState.viewMenu = m_window->menuBar()->addMenu(m_window->tr("View"));
    m_menuState.algorithmMenu = m_window->menuBar()->addMenu(m_window->tr("Algorithm"));
    m_menuState.helpMenu = m_window->menuBar()->addMenu(m_window->tr("Help"));
    m_menuState.toolsMenu = m_window->menuBar()->addMenu(m_window->tr("Tools"));
    buildFileMenu();
    buildViewMenu();
    buildHelpMenu();
    QString initialWorkbenchId = m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D");
    refreshEditMenuForWorkbench(initialWorkbenchId);
    refreshDrawMenuForWorkbench(initialWorkbenchId);
    refreshModifyMenuForWorkbench(initialWorkbenchId);
    refreshAlgorithmMenuForWorkbench(initialWorkbenchId);
}

void WorkbenchMenuManager::buildFileMenu()
{
    if (!m_menuState.fileMenu)
        return;

    auto* newAction = m_menuState.fileMenu->addAction(m_window->tr("New"));
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = m_menuState.fileMenu->addAction(m_window->tr("Open..."));
    openAction->setShortcut(QKeySequence::Open);
    setCmdId(newAction, QStringLiteral("file.new"));
    setCmdId(openAction, QStringLiteral("file.open"));
    connect(newAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("file.new")));
        });
    connect(openAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("file.open")));
        });
    m_menuState.fileMenu->addSeparator();

    auto* saveAction = m_menuState.fileMenu->addAction(m_window->tr("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    auto* saveAsAction = m_menuState.fileMenu->addAction(m_window->tr("Save As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    setCmdId(saveAction, QStringLiteral("file.save"));
    setCmdId(saveAsAction, QStringLiteral("file.save_as"));
    connect(saveAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("file.save")));
        });
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("file.save_as")));
        });
    m_menuState.fileMenu->addSeparator();

    refreshFileMenuForWorkbench(m_stateCenter ? m_stateCenter->currentWorkbenchId() : QStringLiteral("2D"));

    m_menuState.fileMenu->addSeparator();
    m_menuState.recentFilesMenu = m_menuState.fileMenu->addMenu(m_window->tr("Recent Files"));
    m_window->populateRecentFilesMenu();

    m_menuState.fileMenu->addSeparator();
    auto* exitAction = m_menuState.fileMenu->addAction(m_window->tr("Exit"));
    exitAction->setShortcut(QKeySequence::Quit);
    QObject::connect(exitAction, &QAction::triggered, m_window, &QWidget::close);
}

void WorkbenchMenuManager::refreshFileMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.fileMenu)
        return;

    if (m_menuState.importMenu)
    {
        m_menuState.fileMenu->removeAction(m_menuState.importMenu->menuAction());
        delete m_menuState.importMenu;
        m_menuState.importMenu = nullptr;
    }
    if (m_menuState.exportMenu)
    {
        m_menuState.fileMenu->removeAction(m_menuState.exportMenu->menuAction());
        delete m_menuState.exportMenu;
        m_menuState.exportMenu = nullptr;
    }

    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    m_menuState.importMenu = m_menuState.fileMenu->addMenu(m_window->tr("Import"));
    if (is3D)
    {
#if BUILD_UI3D
        const int importBase = static_cast<int>(UI3D::MenuActionId3D::File_ImportModel);
        const int importEnd = static_cast<int>(UI3D::MenuActionId3D::File_ImportSTEP);
        for (const auto& entry : CommandCatalog3D::commands())
        {
            if (!hasSurface(entry.surfaces, CommandSurface3D::Menu))
                continue;
            int menuId = static_cast<int>(entry.menuId);
            if (menuId < importBase || menuId > importEnd)
                continue;
            auto* act = m_menuState.importMenu->addAction(m_window->tr(entry.text));
            QString cmdId = QString::fromUtf8(entry.shortcutId);
            setCmdId(act, cmdId);
            connect(act, &QAction::triggered, this, [this, cmdId]() {
                if (m_operationBus)
                    m_operationBus->run(CommandCatalog::operationForCommandId(cmdId));
                });
        }
#endif
    }
    else
    {
        const QStringList importFormats = {
            m_window->tr("DXF (*.dxf)"), m_window->tr("PLT (*.plt, *.hpgl)"), m_window->tr("STEP (*.stp, *.step)"),
            m_window->tr("SVG (*.svg)"), m_window->tr("PDF (*.pdf)")
        };
        const QStringList importCmdIds = {
            QStringLiteral("file.import_dxf"), QStringLiteral("file.import_plt"),
            QStringLiteral("file.import_step"), QStringLiteral("file.import_svg"),
            QStringLiteral("file.import_pdf")
        };
        for (int i = 0; i < importFormats.size(); ++i)
        {
            auto* act = m_menuState.importMenu->addAction(importFormats[i]);
            QString cmdId = importCmdIds[i];
            setCmdId(act, cmdId);
            connect(act, &QAction::triggered, this, [this, cmdId]() {
                if (m_operationBus)
                    m_operationBus->run(CommandCatalog::operationForCommandId(cmdId));
                });
        }
        m_menuState.importMenu->addSeparator();
        auto* importImage = m_menuState.importMenu->addAction(m_window->tr("Image..."));
        QString imgCmdId = QStringLiteral("file.import_image");
        setCmdId(importImage, imgCmdId);
        connect(importImage, &QAction::triggered, this, [this, imgCmdId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(imgCmdId));
            });
    }

    m_menuState.exportMenu = m_menuState.fileMenu->addMenu(m_window->tr("Export"));
    if (is3D)
    {
        const QStringList exportFormats = {
            m_window->tr("OBJ (*.obj)"), m_window->tr("STL (*.stl)"), m_window->tr("STEP (*.stp, *.step)"),
            m_window->tr("PDF (*.pdf)"), m_window->tr("PNG (*.png)")
        };
        const QStringList exportCmdIds = {
            QStringLiteral("file.export_obj"), QStringLiteral("file.export_stl"),
            QStringLiteral("file.export_step"), QStringLiteral("file.export_pdf"),
            QStringLiteral("file.export_png")
        };
        for (int i = 0; i < exportFormats.size(); ++i)
        {
            auto* act = m_menuState.exportMenu->addAction(exportFormats[i]);
            QString cmdId = exportCmdIds[i];
            setCmdId(act, cmdId);
            connect(act, &QAction::triggered, this, [this, cmdId]() {
                if (m_operationBus)
                    m_operationBus->run(CommandCatalog::operationForCommandId(cmdId));
                });
        }
    }
    else
    {
        const QStringList exportFormats = {
            m_window->tr("DXF (*.dxf)"), m_window->tr("SVG (*.svg)"), m_window->tr("PLT (*.plt)"),
            m_window->tr("BMP (*.bmp)"), m_window->tr("PNG (*.png)")
        };
        const QStringList exportCmdIds = {
            QStringLiteral("file.export_dxf"), QStringLiteral("file.export_svg"),
            QStringLiteral("file.export_plt"), QStringLiteral("file.export_bmp"),
            QStringLiteral("file.export_png")
        };
        for (int i = 0; i < exportFormats.size(); ++i)
        {
            auto* act = m_menuState.exportMenu->addAction(exportFormats[i]);
            QString cmdId = exportCmdIds[i];
            setCmdId(act, cmdId);
            connect(act, &QAction::triggered, this, [this, cmdId]() {
                if (m_operationBus)
                    m_operationBus->run(CommandCatalog::operationForCommandId(cmdId));
                });
        }
    }
}

void WorkbenchMenuManager::buildViewMenu()
{
    if (!m_menuState.viewMenu)
        return;

    m_menuState.viewMenu->addSeparator();

    m_menuState.workbench2DAction = m_menuState.viewMenu->addAction(m_window->tr("Switch to 2D"));
    m_menuState.workbench2DAction->setCheckable(true);
    QObject::connect(m_menuState.workbench2DAction, &QAction::triggered, this, [this]() {
        m_window->triggerWorkbench(QStringLiteral("2D"));
        });

    m_menuState.workbench3DAction = m_menuState.viewMenu->addAction(m_window->tr("Switch to 3D"));
    m_menuState.workbench3DAction->setCheckable(true);
    QObject::connect(m_menuState.workbench3DAction, &QAction::triggered, this, [this]() {
        m_window->triggerWorkbench(QStringLiteral("3D"));
        });

    m_menuState.viewMenu->addSeparator();

    m_menuState.layerMenu = m_menuState.viewMenu->addMenu(m_window->tr("Layer"));
    auto* layerMgr = m_menuState.layerMenu->addAction(m_window->tr("Layer Manager..."));
    QObject::connect(layerMgr, &QAction::triggered, this, [this]() {
        if (m_uiServices && m_uiServices->layerEditService)
        {
            auto* w = qobject_cast<WorkbenchWindow*>(m_window);
            if (w)
                LayerManagerDialog::showDialog(m_uiServices->layerEditService, w);
        }
        });
    m_menuState.layerMenu->addSeparator();
    auto* newLayer = m_menuState.layerMenu->addAction(m_window->tr("New Layer"));
    QObject::connect(newLayer, &QAction::triggered, this, [this]() {
        if (!m_uiServices || !m_uiServices->layerEditService)
            return;
        int id = m_uiServices->layerEditService->createLayer();
        if (id >= 0)
            SY_INFOF("[WorkbenchMenuManager] New layer created, id=%d", id);
        else
            SY_ERRORF("[WorkbenchMenuManager] Failed to create layer, id=%d", id);
        });
    auto* delLayer = m_menuState.layerMenu->addAction(m_window->tr("Delete Layer"));
    QObject::connect(delLayer, &QAction::triggered, this, [this]() {
        if (!m_uiServices || !m_uiServices->layerEditService || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        m_uiServices->layerEditService->deleteLayer(currentId);
        });

    m_menuState.layerMenu->addSeparator();
    auto* layerCtxMenu = new QMenu(m_window->tr("More Layer Operations"), m_window);
    m_menuState.layerMenu->addMenu(layerCtxMenu);
    auto* renameLayer = layerCtxMenu->addAction(m_window->tr("Rename Layer"));
    QObject::connect(renameLayer, &QAction::triggered, this, [this]() {
        if (!m_uiServices || !m_uiServices->layerEditService || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        auto* w = qobject_cast<WorkbenchWindow*>(m_window);
        if (!w)
            return;
        bool ok = false;
        QString newName = QInputDialog::getText(w, m_window->tr("Rename Layer"),
            m_window->tr("New name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !newName.isEmpty())
        {
            m_uiServices->layerEditService->renameLayer(currentId, newName.toStdString());
            SY_INFOF("[WorkbenchMenuManager] Layer renamed: id=%d", currentId);
        }
        });

    auto* toggleLock = layerCtxMenu->addAction(m_window->tr("Toggle Lock"));
    QObject::connect(toggleLock, &QAction::triggered, this, [this]() {
        if (!m_uiServices || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        bool locked = m_uiServices->layerManager->isLayerLocked(currentId);
        m_uiServices->layerManager->setLayerLocked(currentId, !locked);
        });

    auto* toggleVisible = layerCtxMenu->addAction(m_window->tr("Toggle Visibility"));
    QObject::connect(toggleVisible, &QAction::triggered, this, [this]() {
        if (!m_uiServices || !m_uiServices->layerManager)
            return;
        int currentId = m_uiServices->layerManager->currentLayerId();
        if (currentId < 0)
            return;
        bool visible = m_uiServices->layerManager->isLayerVisible(currentId);
        m_uiServices->layerManager->setLayerVisible(currentId, !visible);
        });

    m_menuState.viewMenu->addSeparator();

    m_menuState.unitMenu = m_menuState.viewMenu->addMenu(m_window->tr("Unit"));
    m_menuState.unitActionGroup = new QActionGroup(m_window);
    m_menuState.unitActionGroup->setExclusive(true);

    const struct
    {
        const char* text; const char* cmdId; bool checked;
    } units[] = {
{ "mm", "view.unit_mm", true },
{ "cm", "view.unit_cm", false },
{ "inch", "view.unit_inch", false }
    };
    for (const auto& u : units)
    {
        auto* act = m_menuState.unitMenu->addAction(m_window->tr(u.text));
        act->setCheckable(true);
        act->setChecked(u.checked);
        m_menuState.unitActionGroup->addAction(act);
        QString cmdId = QString::fromLatin1(u.cmdId);
        setCmdId(act, cmdId);
        connect(act, &QAction::triggered, this, [this, cmdId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(cmdId));
            });
    }

    m_menuState.viewMenu->addSeparator();

    m_menuState.gridSnapMenu = m_menuState.viewMenu->addMenu(m_window->tr("Grid && Snap"));
    auto* showGrid = m_menuState.gridSnapMenu->addAction(m_window->tr("Show Grid"));
    showGrid->setCheckable(true);
    QObject::connect(showGrid, &QAction::toggled, this, [this](bool checked) {
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("gridVisible"), checked } });
        });
    auto* snapEnabled = m_menuState.gridSnapMenu->addAction(m_window->tr("Snap Enabled"));
    snapEnabled->setCheckable(true);
    QObject::connect(snapEnabled, &QAction::toggled, this, [this](bool checked) {
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("snapEnabled"), checked } });
        });
    auto* orthoMode = m_menuState.gridSnapMenu->addAction(m_window->tr("Ortho Mode"));
    orthoMode->setCheckable(true);
    QObject::connect(orthoMode, &QAction::toggled, this, [this](bool checked) {
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("orthoMode"), checked } });
        });
    auto* angleSnap = m_menuState.gridSnapMenu->addAction(m_window->tr("Angle Snap"));
    angleSnap->setCheckable(true);
    QObject::connect(angleSnap, &QAction::toggled, this, [this](bool checked) {
        if (m_stateCenter)
            m_stateCenter->setMetadata({ { QStringLiteral("angleSnap"), checked } });
        });

    m_menuState.viewMenu->addSeparator();

    m_menuState.zoomMenu = m_menuState.viewMenu->addMenu(m_window->tr("Zoom"));
    auto* zoomIn = m_menuState.zoomMenu->addAction(m_window->tr("Zoom In"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    QObject::connect(zoomIn, &QAction::triggered, this, [this]() {
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_in"));
        });
    auto* zoomOut = m_menuState.zoomMenu->addAction(m_window->tr("Zoom Out"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    QObject::connect(zoomOut, &QAction::triggered, this, [this]() {
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_out"));
        });
    m_menuState.zoomMenu->addSeparator();
    auto* zoomFit = m_menuState.zoomMenu->addAction(m_window->tr("Zoom to Fit"));
    zoomFit->setShortcut(QStringLiteral("Ctrl+F"));
    QObject::connect(zoomFit, &QAction::triggered, this, [this]() {
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_fit"));
        });
    auto* zoomSel = m_menuState.zoomMenu->addAction(m_window->tr("Zoom to Selection"));
    zoomSel->setShortcut(QStringLiteral("Ctrl+Shift+F"));
    QObject::connect(zoomSel, &QAction::triggered, this, [this]() {
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("zoom_selection"));
        });
    m_menuState.zoomMenu->addSeparator();
    auto* resetView = m_menuState.zoomMenu->addAction(m_window->tr("Reset View"));
    resetView->setShortcut(QStringLiteral("Ctrl+0"));
    QObject::connect(resetView, &QAction::triggered, this, [this]() {
        if (m_viewportZoomHandler)
            m_viewportZoomHandler(QStringLiteral("reset"));
        });
}

void WorkbenchMenuManager::refreshDrawMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.drawMenu)
        return;

    qDeleteAll(m_menuState.drawMenu->actions());

    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    const auto addDrawAction = [this](const QString& text, const QString& commandId) {
        auto* action = m_menuState.drawMenu->addAction(text);
        setCmdId(action, commandId);
        connect(action, &QAction::triggered, this, [this, commandId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(commandId));
            });
        return action;
        };

    if (is3D)
    {
#if BUILD_UI3D
        const int modelBase = static_cast<int>(UI3D::MenuActionId3D::Model_MakeBox);
        const int modelEnd = static_cast<int>(UI3D::MenuActionId3D::Model_SplitByPickPlane);
        for (const auto& entry : CommandCatalog3D::commands())
        {
            if (!hasSurface(entry.surfaces, CommandSurface3D::Menu))
                continue;
            int menuId = static_cast<int>(entry.menuId);
            if (menuId < modelBase || menuId > modelEnd)
                continue;
            addDrawAction(m_window->tr(entry.text), QString::fromUtf8(Cmd::operationIdToString(entry.operationId)));
        }
#endif
    }
    else
    {
        bool firstSeparatorAdded = false;
        for (const ToolCommandEntry& cmdEntry : CommandCatalog::toolCommands())
        {
            if (!hasSurface(cmdEntry.surfaces, CommandSurface::Menu))
                continue;
            if (!firstSeparatorAdded && cmdEntry.menuActionId != UI::MenuActionId::Draw_Select)
            {
                m_menuState.drawMenu->addSeparator();
                firstSeparatorAdded = true;
            }
            addDrawAction(m_window->tr(cmdEntry.menuText), QString::fromUtf8(cmdEntry.toolName));
        }
    }
}

void WorkbenchMenuManager::refreshEditMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.editMenu)
        return;

    qDeleteAll(m_menuState.editMenu->actions());

    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    const auto addEditAction = [this](QMenu* menu, const QString& text, const QString& commandId) {
        auto* act = menu->addAction(text);
        setCmdId(act, commandId);
        connect(act, &QAction::triggered, this, [this, commandId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(commandId));
            });
        return act;
        };

    auto* undoAction = m_menuState.editMenu->addAction(m_window->tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    auto* redoAction = m_menuState.editMenu->addAction(m_window->tr("Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(undoAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.undo")));
        });
    connect(redoAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.redo")));
        });
    m_menuState.editMenu->addSeparator();

    addEditAction(m_menuState.editMenu, m_window->tr("Select All"), QStringLiteral("edit.select_all"));
    addEditAction(m_menuState.editMenu, m_window->tr("Invert Selection"), QStringLiteral("edit.invert_selection"));
    addEditAction(m_menuState.editMenu, m_window->tr("Deselect"), QStringLiteral("edit.deselect"));
    m_menuState.editMenu->addSeparator();

    addEditAction(m_menuState.editMenu, m_window->tr("Cut"), QStringLiteral("edit.cut"));
    addEditAction(m_menuState.editMenu, m_window->tr("Copy"), QStringLiteral("edit.copy"));
    addEditAction(m_menuState.editMenu, m_window->tr("Paste"), QStringLiteral("edit.paste"));
    m_menuState.editMenu->addSeparator();

    addEditAction(m_menuState.editMenu, m_window->tr("Delete"), QStringLiteral("edit.delete"));
    m_menuState.editMenu->addSeparator();

    if (!is3D)
    {
        addEditAction(m_menuState.editMenu, m_window->tr("Move"), QStringLiteral("edit.move"));

        m_menuState.rotateMenu = m_menuState.editMenu->addMenu(m_window->tr("Rotate"));
        addEditAction(m_menuState.rotateMenu, m_window->tr("Rotate 90 CW"), QStringLiteral("edit.rotate_90cw"));
        addEditAction(m_menuState.rotateMenu, m_window->tr("Rotate 90 CCW"), QStringLiteral("edit.rotate_90ccw"));
        addEditAction(m_menuState.rotateMenu, m_window->tr("Rotate 180"), QStringLiteral("edit.rotate_180"));

        m_menuState.mirrorMenu = m_menuState.editMenu->addMenu(m_window->tr("Mirror"));
        addEditAction(m_menuState.mirrorMenu, m_window->tr("Mirror Horizontal"), QStringLiteral("edit.mirror_horizontal"));
        addEditAction(m_menuState.mirrorMenu, m_window->tr("Mirror Vertical"), QStringLiteral("edit.mirror_vertical"));

        m_menuState.alignMenu = m_menuState.editMenu->addMenu(m_window->tr("Align"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Left"), QStringLiteral("edit.align_left"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Right"), QStringLiteral("edit.align_right"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Center H"), QStringLiteral("edit.align_center_h"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Top"), QStringLiteral("edit.align_top"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Bottom"), QStringLiteral("edit.align_bottom"));
        addEditAction(m_menuState.alignMenu, m_window->tr("Align Center V"), QStringLiteral("edit.align_center_v"));
        m_menuState.editMenu->addSeparator();

        auto* groupAction = m_menuState.editMenu->addAction(m_window->tr("Group"));
        groupAction->setCheckable(true);
        setCmdId(groupAction, QStringLiteral("edit.group"));
        connect(groupAction, &QAction::triggered, this, [this]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.group")));
            });

        auto* ungroupAction = m_menuState.editMenu->addAction(m_window->tr("Ungroup"));
        setCmdId(ungroupAction, QStringLiteral("edit.ungroup"));
        connect(ungroupAction, &QAction::triggered, this, [this]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.ungroup")));
            });

        m_menuState.editMenu->addSeparator();

        m_menuState.pathOpsMenu = m_menuState.editMenu->addMenu(m_window->tr("Path Operations"));
        addEditAction(m_menuState.pathOpsMenu, m_window->tr("Offset"), QStringLiteral("edit.offset"));
        addEditAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Union"), QStringLiteral("edit.boolean_union"));
        addEditAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Intersection"), QStringLiteral("edit.boolean_intersection"));
        addEditAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Difference"), QStringLiteral("edit.boolean_difference"));
        addEditAction(m_menuState.pathOpsMenu, m_window->tr("Boolean Xor"), QStringLiteral("edit.boolean_xor"));
    }
}

void WorkbenchMenuManager::refreshModifyMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.modifyMenu)
        return;

    qDeleteAll(m_menuState.modifyMenu->actions());

    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    const auto addModifyAction = [this](const QString& text, const QString& commandId) {
        auto* action = m_menuState.modifyMenu->addAction(text);
        setCmdId(action, commandId);
        connect(action, &QAction::triggered, this, [this, commandId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(commandId));
            });
        return action;
        };

    if (is3D)
    {
#if BUILD_UI3D
        const int editBase = static_cast<int>(UI3D::MenuActionId3D::Edit_TransformTranslate);
        const int editEnd = static_cast<int>(UI3D::MenuActionId3D::Edit_TransformScale);
        for (const auto& entry : CommandCatalog3D::commands())
        {
            if (!hasSurface(entry.surfaces, CommandSurface3D::Menu))
                continue;
            int menuId = static_cast<int>(entry.menuId);
            if (menuId < editBase || menuId > editEnd)
                continue;
            addModifyAction(m_window->tr(entry.text), QString::fromUtf8(Cmd::operationIdToString(entry.operationId)));
        }
        m_menuState.modifyMenu->addSeparator();
        addModifyAction(m_window->tr("Delete"), QStringLiteral("edit.delete"));
#endif
    }
    else
    {
        addModifyAction(m_window->tr("Move"), QStringLiteral("2d.move"));
        addModifyAction(m_window->tr("Rotate"), QStringLiteral("2d.rotate"));
        addModifyAction(m_window->tr("Scale"), QStringLiteral("2d.scale"));
        addModifyAction(m_window->tr("Copy"), QStringLiteral("2d.copy"));
        addModifyAction(m_window->tr("Mirror"), QStringLiteral("2d.mirror"));
        m_menuState.modifyMenu->addSeparator();
        addModifyAction(m_window->tr("Trim"), QStringLiteral("2d.trim"));
        addModifyAction(m_window->tr("Extend"), QStringLiteral("2d.extend"));
        addModifyAction(m_window->tr("Fillet"), QStringLiteral("2d.fillet"));
        addModifyAction(m_window->tr("Chamfer"), QStringLiteral("2d.chamfer"));
        m_menuState.modifyMenu->addSeparator();
        addModifyAction(m_window->tr("Delete"), QStringLiteral("2d.delete"));
    }
}

void WorkbenchMenuManager::refreshAlgorithmMenuForWorkbench(const QString& workbenchId)
{
    if (!m_menuState.algorithmMenu)
        return;

    qDeleteAll(m_menuState.algorithmMenu->actions());

    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    const auto addAlgoAction = [this](const QString& text, const QString& commandId) {
        auto* act = m_menuState.algorithmMenu->addAction(text);
        setCmdId(act, commandId);
        connect(act, &QAction::triggered, this, [this, commandId]() {
            if (m_operationBus)
                m_operationBus->run(CommandCatalog::operationForCommandId(commandId));
            });
        return act;
        };

    if (is3D)
    {
#if BUILD_UI3D
        const int algoBase = static_cast<int>(UI3D::MenuActionId3D::Algo_NestingFromMesh);
        const int algoEnd = static_cast<int>(UI3D::MenuActionId3D::Process_SendReliefToLaser);
        for (const auto& entry : CommandCatalog3D::commands())
        {
            if (!hasSurface(entry.surfaces, CommandSurface3D::Menu))
                continue;
            int menuId = static_cast<int>(entry.menuId);
            if (menuId < algoBase || menuId > algoEnd)
                continue;
            addAlgoAction(m_window->tr(entry.text), QString::fromUtf8(Cmd::operationIdToString(entry.operationId)));
        }
#endif
    }
    else
    {
        addAlgoAction(m_window->tr("Fill..."), QStringLiteral("algo.fill"));
        addAlgoAction(m_window->tr("Nesting..."), QStringLiteral("algo.nesting"));
        addAlgoAction(m_window->tr("Array..."), QStringLiteral("algo.array"));
        addAlgoAction(m_window->tr("Bitmap Relief Engraving..."), QStringLiteral("algo.relief_engraving"));
    }
}

void WorkbenchMenuManager::buildHelpMenu()
{
    if (!m_menuState.helpMenu)
        return;

    auto* docsAction = m_menuState.helpMenu->addAction(m_window->tr("Documentation"));
    setCmdId(docsAction, QStringLiteral("help.docs"));
    connect(docsAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("help.docs")));
        });

    auto* shortcutAction = m_menuState.helpMenu->addAction(m_window->tr("Keyboard Shortcuts"));
    shortcutAction->setShortcut(Qt::Key_F1);
    setCmdId(shortcutAction, QStringLiteral("help.shortcuts"));
    connect(shortcutAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("help.shortcuts")));
        });
    m_menuState.helpMenu->addSeparator();

    auto* settingsAction = m_menuState.helpMenu->addAction(m_window->tr("Settings..."));
    setCmdId(settingsAction, QStringLiteral("help.settings"));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("help.settings")));
        });
    m_menuState.helpMenu->addSeparator();

    m_menuState.languageMenu = m_menuState.helpMenu->addMenu(m_window->tr("Language"));
    auto* langGroup = new QActionGroup(m_window);
    langGroup->setExclusive(true);
    const auto langs = LM->supportedLanguages();
    const auto currentLang = LM->currentLanguage();
    for (const auto& lang : langs)
    {
        auto* act = m_menuState.languageMenu->addAction(LM->languageName(lang));
        act->setCheckable(true);
        act->setChecked(lang == currentLang);
        langGroup->addAction(act);
        connect(act, &QAction::triggered, this, [lang]() {
            LM->setLanguage(lang);
            });
    }
    if (m_languageChangedConn)
        disconnect(m_languageChangedConn);
    m_languageChangedConn = connect(LM, &LanguageManager::languageChanged, this, [this](AppLanguage newLang) {
        if (!m_menuState.languageMenu)
            return;
        for (QAction* act : m_menuState.languageMenu->actions())
        {
            if (!act->isCheckable())
                continue;
            const auto langs = LM->supportedLanguages();
            int idx = m_menuState.languageMenu->actions().indexOf(act);
            if (idx >= 0 && idx < langs.size())
            {
                QSignalBlocker blocker(act);
                act->setChecked(langs[idx] == newLang);
            }
        }
        });

    m_menuState.helpThemeMenu = m_menuState.helpMenu->addMenu(m_window->tr("Theme"));
    auto* themeGroup = new QActionGroup(m_window);
    themeGroup->setExclusive(true);
    const auto themes = TM->supportedThemes();
    const auto currentTheme = TM->currentTheme();
    for (const auto& theme : themes)
    {
        auto* act = m_menuState.helpThemeMenu->addAction(TM->themeName(theme));
        act->setCheckable(true);
        act->setChecked(theme == currentTheme);
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [theme]() {
            TM->setTheme(theme);
            });
    }
    if (m_themeChangedConn)
        disconnect(m_themeChangedConn);
    m_themeChangedConn = connect(TM, &ThemeManager::themeChanged, this, [this](AppTheme newTheme) {
        if (!m_menuState.helpThemeMenu)
            return;
        for (QAction* act : m_menuState.helpThemeMenu->actions())
        {
            if (!act->isCheckable())
                continue;
            const auto themes = TM->supportedThemes();
            int idx = m_menuState.helpThemeMenu->actions().indexOf(act);
            if (idx >= 0 && idx < themes.size())
            {
                QSignalBlocker blocker(act);
                act->setChecked(themes[idx] == newTheme);
            }
        }
        });

    m_menuState.helpMenu->addSeparator();

    auto* aboutAction = m_menuState.helpMenu->addAction(m_window->tr("About"));
    setCmdId(aboutAction, QStringLiteral("help.about"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("help.about")));
        });
}

void WorkbenchMenuManager::initializeThemeMenuSkeleton()
{
    buildThemeMenu();
}

void WorkbenchMenuManager::buildThemeMenu()
{
    m_menuState.themeMenu = m_menuState.toolsMenu->addMenu(m_window->tr("Theme"));

    const auto addThemeAction = [this](const QString& text, const QString& themeId) {
        QAction* action = m_menuState.themeMenu->addAction(text);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, themeId]() {
            m_window->triggerTheme(themeId);
            });
        };

    addThemeAction(m_window->tr("System"), QStringLiteral("system"));
    addThemeAction(m_window->tr("Light"), QStringLiteral("light"));
    addThemeAction(m_window->tr("Dark"), QStringLiteral("dark"));
    addThemeAction(m_window->tr("Blue"), QStringLiteral("blue"));
}

void WorkbenchMenuManager::bindMenuCommands()
{
    SY_INFO("[WorkbenchMenuManager] bindMenuCommands: menu actions are now directly connected to OperationBus");
}

void WorkbenchMenuManager::bindShortcuts()
{
    auto* undoAction = new QAction(m_window->tr("Undo"), m_window);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.undo")));
        });
    m_window->addAction(undoAction);

    auto* redoAction = new QAction(m_window->tr("Redo"), m_window);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [this]() {
        if (m_operationBus)
            m_operationBus->run(CommandCatalog::operationForCommandId(QStringLiteral("edit.redo")));
        });
    m_window->addAction(redoAction);
}

void WorkbenchMenuManager::refreshWorkbenchMenuChecks(const QString& workbenchId)
{
    const bool is2D = workbenchId.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0;
    const bool is3D = workbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0;

    if (m_menuState.workbench2DAction)
    {
        m_menuState.workbench2DAction->setVisible(is3D);
        m_menuState.workbench2DAction->setChecked(false);
    }
    if (m_menuState.workbench3DAction)
    {
        m_menuState.workbench3DAction->setVisible(is2D);
        m_menuState.workbench3DAction->setChecked(false);
    }
}

void WorkbenchMenuManager::refreshThemeMenuChecks(const QString& themeId)
{
    if (!m_menuState.themeMenu)
        return;

    for (QAction* action : m_menuState.themeMenu->actions())
    {
        if (!action->isCheckable())
            continue;
        action->setChecked(action->text().compare(themeId, Qt::CaseInsensitive) == 0);
    }
}

void WorkbenchMenuManager::syncGridSnapMenuState()
{
    if (!m_stateCenter || !m_menuState.gridSnapMenu)
        return;

    for (QAction* act : m_menuState.gridSnapMenu->actions())
    {
        if (!act->isCheckable() || act->isSeparator())
            continue;

        const QString text = act->text();
        const bool checked = act->isChecked();
        if (text.contains(m_window->tr("Grid"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("gridVisible"), checked } });
        else if (text.contains(m_window->tr("Snap"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("snapEnabled"), checked } });
        else if (text.contains(m_window->tr("Ortho"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("orthoMode"), checked } });
        else if (text.contains(m_window->tr("Angle"), Qt::CaseInsensitive))
            m_stateCenter->setMetadata({ { QStringLiteral("angleSnap"), checked } });
    }
}

void WorkbenchMenuManager::refreshGridSnapMenuChecks()
{
    if (!m_stateCenter || !m_menuState.gridSnapMenu)
        return;

    const auto& metadata = m_stateCenter->snapshot().metadata;

    for (QAction* act : m_menuState.gridSnapMenu->actions())
    {
        if (!act->isCheckable() || act->isSeparator())
            continue;

        const QString text = act->text();
        bool checked = act->isChecked();

        if (text.contains(m_window->tr("Grid"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("gridVisible")))
            checked = metadata.value(QStringLiteral("gridVisible")).toBool();
        else if (text.contains(m_window->tr("Snap"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("snapEnabled")))
            checked = metadata.value(QStringLiteral("snapEnabled")).toBool();
        else if (text.contains(m_window->tr("Ortho"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("orthoMode")))
            checked = metadata.value(QStringLiteral("orthoMode")).toBool();
        else if (text.contains(m_window->tr("Angle"), Qt::CaseInsensitive)
            && metadata.contains(QStringLiteral("angleSnap")))
            checked = metadata.value(QStringLiteral("angleSnap")).toBool();

        QSignalBlocker blocker(act);
        act->setChecked(checked);
    }
}