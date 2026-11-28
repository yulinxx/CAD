#include "PanelFactory.h"

#include <QAction>
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
    constexpr const char* kPanModeProperty = "april.drawTools.panMode";
    constexpr const char* kToolNameProperty = "toolName";
    constexpr const char* kLegacyToolBarIdProperty = "legacyToolBarId";
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
        bar->setProperty(kPanModeProperty, false);

        // Select/Pan 切换与旧 Workbench2D 行为对齐：按钮点下去既切视口模式，也刷新高亮。
        bar->setIsPanModeCallback([bar]() {
            return bar->property(kPanModeProperty).toBool();
        });
        bar->setPanModeToggleCallback([bar, host = info.host]() {
            const bool enterPan = !bar->property(kPanModeProperty).toBool();
            bar->setProperty(kPanModeProperty, enterPan);
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

                QObject::connect(action, &QAction::triggered, bar, [bar, action]() {
                    bar->setProperty(kPanModeProperty, false);
                    bar->setCurrentToolName(action->property(kToolNameProperty).toString());
                });
            }

            if (QAction* panAction = info.host->commandAction(QStringLiteral("view.pan")))
            {
                QObject::connect(panAction, &QAction::triggered, bar, [bar]() {
                    bar->setProperty(kPanModeProperty, true);
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
        bar->setProperty(kLegacyToolBarIdProperty, QStringLiteral("TopToolBar"));

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

        // 旧场景树（service 注入后 setMode2D/setMode3D；这里只构造）
        if (id == QLatin1String("scene") || id == QLatin1String("sceneTree") || id == QLatin1String("sceneTreePanel"))
        {
            auto* tree = new SceneTreePanel(parent);
            tree->setObjectName(QStringLiteral("panel:") + id);
            return tree;
        }

        // 旧属性面板
        if (id == QLatin1String("inspector") || id == QLatin1String("properties"))
        {
            auto* props = new PropertiesPanelWidget(parent);
            props->setObjectName(QStringLiteral("panel:") + id);
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
}  // namespace Apr
