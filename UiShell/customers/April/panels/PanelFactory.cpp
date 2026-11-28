#include "PanelFactory.h"

#include <QAction>
#include <QPoint>
#include <QJsonArray>
#include <QJsonValue>
#include <QPointer>
#include <QStringList>
#include <QToolBar>
#include <QVector>

#include "UiShell/Contract/IShellHost.h"

// ---- AprilUi / UI2D 旧界面部件（Main/Src/UI/Widgets + UI/2D）----
#include "UI/Command/CommandSurfaceFlags.h"      // Cmd::hasSurface
#include "UI/Widgets/UiSceneTreePanel.h"         // SceneTreePanel
#include "UI/Widgets/UiPropertiesPanel.h"        // PropertiesPanelWidget
#include "UI/Widgets/DrawToolBarWidget.h"        // DrawToolBarWidget
#include "UI2D/Operation/CommandCatalog.h"       // LeftToolbar / TopToolbar 命令表面
#include "UI2D/StatusBar/StatusBar.h"            // StatusBar
#include "UI2D/ToolBar/TopToolBar.h"             // TopToolBar

namespace
{
    constexpr const char* kToolNameProperty = "toolName";
    constexpr const char* kSeparatorCommand = "separator";

    // profile 的 props.commands 允许字符串与对象两种写法；对象用 commandId/id 取命令名。
    QString commandIdOf(const QJsonValue& value)
    {
        if (value.isString())
            return value.toString();
        if (!value.isObject())
            return QString();

        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("separator")).toBool(false))
            return QString::fromLatin1(kSeparatorCommand);

        const QString commandId = object.value(QStringLiteral("commandId")).toString();
        return commandId.isEmpty() ? object.value(QStringLiteral("id")).toString() : commandId;
    }

    // 顶部工具栏命令顺序由 April profile 声明；老 profile 没写时回退到 2D 命令目录的
    // TopToolbar 表面，避免工具栏恢复后仍是空的。
    QStringList topToolbarCommands(const QJsonObject& props)
    {
        QStringList commands;
        const QJsonArray configured = props.value(QStringLiteral("commands")).toArray();
        for (const QJsonValue& value : configured)
        {
            const QString commandId = commandIdOf(value);
            if (!commandId.isEmpty())
                commands.append(commandId);
        }
        if (!commands.isEmpty())
            return commands;

        for (const CommandEntry2D& entry : CommandCatalog::commands())
        {
            if (entry.shortcutId && Cmd::hasSurface(entry.surfaces, CommandSurface2DValues::TopToolbar))
                commands.append(QString::fromUtf8(entry.shortcutId));
        }
        return commands;
    }

    QWidget* createDrawToolBar(const AA::PanelCreateRequest& info)
    {
        auto* bar = new DrawToolBarWidget(info.parent);
        bar->setObjectName(QStringLiteral("panel:") + info.strPanelId);

        // Select/Pan 高亮不再复制到控件属性：共享命令动作的勾选态就是唯一状态源，
        // 菜单 / 工具栏 / 右键 / 面板因此看到同一份活动工具状态。
        QPointer<QAction> panAction;
        QPointer<QAction> selectAction;
        if (info.host)
        {
            panAction = info.host->commandAction(QStringLiteral("view.pan"));
            selectAction = info.host->commandAction(QStringLiteral("tool.select"));
            if (panAction)
                panAction->setCheckable(true);
            if (selectAction)
            {
                selectAction->setCheckable(true);
                selectAction->setChecked(true);
            }
        }

        bar->setIsPanModeCallback([panAction]() {
            return panAction && panAction->isChecked();
        });
        bar->setPanModeToggleCallback([bar, host = info.host, panAction, selectAction]() {
            const bool enterPan = !(panAction && panAction->isChecked());
            if (panAction)
                panAction->setChecked(enterPan);
            if (selectAction)
                selectAction->setChecked(!enterPan);
            bar->setCurrentToolName(enterPan ? QStringLiteral("PanTool") : QStringLiteral("SelectTool"));
            if (host)
                host->invoke(enterPan ? QStringLiteral("view.pan") : QStringLiteral("tool.select"));
            return true;
        });

        QVector<QAction*> actions;
        if (info.host)
        {
            for (const CommandEntry2D& entry : CommandCatalog::commands())
            {
                if (!entry.shortcutId || !entry.toolName)
                    continue;
                if (!Cmd::hasSurface(entry.surfaces, CommandSurface2DValues::LeftToolbar))
                    continue;

                QAction* action = info.host->commandAction(QString::fromUtf8(entry.shortcutId));
                if (!action)
                    continue;

                // DrawToolBarWidget 靠 toolName 识别 Select/Pan，并同步活动工具高亮。
                action->setProperty(kToolNameProperty, QString::fromUtf8(entry.toolName));
                actions.append(action);

                QObject::connect(action, &QAction::triggered, bar,
                    [bar, action, panAction, selectAction]() {
                        if (panAction)
                            panAction->setChecked(false);
                        if (selectAction)
                            selectAction->setChecked(action == selectAction.data());
                        bar->setCurrentToolName(action->property(kToolNameProperty).toString());
                    });
            }

            if (panAction)
            {
                QObject::connect(panAction.data(), &QAction::triggered, bar,
                    [bar, panAction, selectAction]() {
                        if (panAction)
                            panAction->setChecked(true);
                        if (selectAction)
                            selectAction->setChecked(false);
                        bar->setCurrentToolName(QStringLiteral("PanTool"));
                    });
            }
        }

        bar->setCurrentToolName(QStringLiteral("SelectTool"));
        bar->setToolActions(actions);
        return bar;
    }

    QWidget* createTopToolBar(const AA::PanelCreateRequest& info)
    {
        auto* bar = new TopToolBar(info.parent);
        bar->setObjectName(QStringLiteral("panel:") + info.strPanelId);

        if (info.host)
        {
            for (const QString& commandId : topToolbarCommands(info.props))
            {
                if (commandId == QLatin1String(kSeparatorCommand))
                {
                    bar->addSeparator();
                    continue;
                }
                if (QAction* action = info.host->commandAction(commandId))
                    bar->addAction(action);
            }
        }
        return bar;
    }
}  // namespace

namespace Apr
{
    QWidget* PanelFactory::create(const AA::PanelCreateRequest& info)
    {
        QWidget* parent = info.parent;
        const QString id = info.strPanelId;
        const QString kind = info.props.value(QStringLiteral("kind")).toString();

        // 左侧绘图工具栏：profile 用 kind 显式指定，旧面板 id 继续按历史语义映射。
        if (kind == QLatin1String("drawTools") || id == QLatin1String("draw")
            || id == QLatin1String("drawTools") || id == QLatin1String("leftToolbar")
            || id == QLatin1String("left.toolbar") || id == QLatin1String("tools"))
        {
            return createDrawToolBar(info);
        }

        // 顶部编辑工具栏：旧面板 id "editToolbar" 与新 kind 都走这里。
        if (kind == QLatin1String("topToolbar") || id == QLatin1String("editToolbar")
            || id == QLatin1String("topToolbar"))
        {
            return createTopToolBar(info);
        }

        // 场景树：右键只报告上下文与坐标，菜单项 / 状态由外壳按 profile 命令统一构建。
        // 控件不再自己 new QAction，也不直接连业务信号；旧框架的自建菜单仅在无 host 时保留。
        if (id == QLatin1String("scene") || id == QLatin1String("sceneTree") || id == QLatin1String("sceneTreePanel"))
        {
            auto* tree = new SceneTreePanel(parent);
            tree->setObjectName(QStringLiteral("panel:") + id);
            if (info.host)
            {
                tree->setContextMenuHandler([host = info.host, tree](const QPoint& globalPos) {
                    host->showContextMenu(QStringLiteral("sceneTree"), globalPos, tree);
                });
            }

            // 场景树只报告选中 id；属性模型/编辑目标由算法适配层生成，
            // 面板与场景树都不认识引擎。以 tree 为连接上下文，面板销毁由 QPointer 兜底。
            QObject::connect(tree, &SceneTreePanel::selectionChanged, tree,
                [this](const QStringList& ids) {
                    m_lastSelectionIds = ids;
                    refreshPropertiesPanel();
                });
            return tree;
        }

        // 旧属性面板
        if (id == QLatin1String("inspector") || id == QLatin1String("properties"))
        {
            auto* props = new PropertiesPanelWidget(parent);
            props->setObjectName(QStringLiteral("panel:") + id);
            m_propertiesPanel = props;
            // 面板可能晚于场景树创建：用最近一次选择快照补齐，避免空面板一直停留。
            refreshPropertiesPanel();
            return props;
        }

        // 旧状态栏（UI2D）：坐标 sink 由客户 workbench 持有，视口刷新后仍然有效。
        if (id == QLatin1String("status"))
        {
            auto* bar = new StatusBar(parent);
            bar->setObjectName(QStringLiteral("panel:") + id);
            if (m_workbench)
            {
                QPointer<StatusBar> weakBar = bar;
                m_workbench->setPositionSink([weakBar](double x, double y) {
                    if (StatusBar* statusBar = weakBar.data())
                        statusBar->setPositionValues(x, y, QStringLiteral("mm"), true);
                });
            }
            return bar;
        }

        // 未识别 panelId：空占位
        auto* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("panel:") + id);
        return placeholder;
    }

    void PanelFactory::refreshPropertiesPanel()
    {
        PropertiesPanelWidget* panel = m_propertiesPanel.data();
        if (!panel || !m_propertyServices)
            return;

        // 每次选择变化都重建编辑会话：会话只持有图元 id，撤销后不会留下悬空指针。
        panel->setEditTarget(m_propertyServices->createEditTarget(m_lastSelectionIds));
        panel->setPropertyModel(m_propertyServices->buildPropertyModel(m_lastSelectionIds));
    }
}  // namespace Apr
