#include "PanelFactory.h"

#include <QString>
#include <QWidget>

// ---- AprilUi / UI2D 旧界面部件（Main/Src/UI/Widgets + UI/2D）----
#include "UI/Widgets/UiSceneTreePanel.h"      // SceneTreePanel
#include "UI/Widgets/UiPropertiesPanel.h"     // PropertiesPanelWidget
#include "UI/Widgets/DrawToolBarWidget.h"     // DrawToolBarWidget
#include "UI2D/StatusBar/StatusBar.h"         // StatusBar

namespace Apr
{
    QWidget* PanelFactory::create(const RD::PanelCreateRequest& info)
    {
        QWidget* parent = info.parent;
        const QString id = info.strPanelId;

        // 旧左侧绘图工具
        if (id == QLatin1String("tools") || id == QLatin1String("top.toolbar") || id == QLatin1String("draw"))
        {
            auto* bar = new DrawToolBarWidget(parent);
            bar->setObjectName(QStringLiteral("panel:") + id);
            return bar;
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

        // 旧状态栏（UI2D）
        if (id == QLatin1String("status"))
        {
            auto* bar = new StatusBar(parent);
            bar->setObjectName(QStringLiteral("panel:") + id);
            return bar;
        }

        // 未识别 panelId：空占位
        auto* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("panel:") + id);
        return placeholder;
    }
}  // namespace Apr
