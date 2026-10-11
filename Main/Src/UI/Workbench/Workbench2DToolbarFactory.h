#pragma once

#include <QToolBar>
#include <QVector>
#include <QObject>
#include <memory>

#include "UI/Service/ToolBarContextManager.h"

class QAction;
class CommandActionHub;
class TopToolBar;
class RightToolBar;
class TextFontToolBar;
class RenderViewport2D;
class WorkbenchWindow;
class OperationBus;
class UiStateCenter;
class IUndoRedoManager;
class ISelectionService;
class LayerManager;
class SceneEditService;
class QWidget;

namespace Eg
{
    class EntityClipboard;
}

struct CommandUiSnapshot;

/// 2D 左右面板（Draw Tools / Layers）的承载样式
enum class PanelHostStyle
{
    Toolbar,  ///< 固定工具栏（QToolBar 停靠在左右两侧）
    Dock      ///< Dock 停靠面板（QDockWidget，默认）
};

class Workbench2DToolbarFactory : public QObject
{
    Q_OBJECT
public:
    explicit Workbench2DToolbarFactory(QObject* parent = nullptr);

    void createToolbars(WorkbenchWindow& window, RenderViewport2D* viewport, OperationBus* bus,
        UiStateCenter* stateCenter, CommandActionHub* hub, PanelHostStyle style,
        IUndoRedoManager* undoManager, Eg::EntityClipboard* clipboard,
        ISelectionService* selectionService, LayerManager* layerManager,
        SceneEditService* sceneEditService);

    /// 按命令目录顺序取出左侧绘图工具栏要展示的中枢 QAction
    QVector<QAction*> buildDrawToolActions(CommandActionHub& hub);
    ToolBarContext determineContextFromSelection(const CommandUiSnapshot& snapshot) const;

    TopToolBar* topToolBar() const { return m_topToolBar; }
    RightToolBar* rightToolBar() const { return m_rightToolBar; }
    QToolBar* textFontToolBar() const { return m_textFontToolBar; }
    TextFontToolBar* textFontToolBarWidget() const { return m_textFontToolBarWidget; }
    ToolBarContextManager* contextManager() const { return m_contextManager.get(); }

    void shutdown();

private:
    TopToolBar* m_topToolBar{ nullptr };
    QToolBar* m_textFontToolBar{ nullptr };
    TextFontToolBar* m_textFontToolBarWidget{ nullptr };
    RightToolBar* m_rightToolBar{ nullptr };
    std::unique_ptr<ToolBarContextManager> m_contextManager;
    PanelHostStyle m_panelHostStyle{ PanelHostStyle::Dock };
    /// 网格显隐 metadata 连线，shutdown 时必须断开（lambda 捕获当次视口裸指针）
    QMetaObject::Connection m_gridMetadataConn;
};
