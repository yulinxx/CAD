#include "UiBuiltinPanels.h"

#include "UiClientContext.h"
#include "UiFeatureGate.h"
#include "UiPanelRegistry.h"
#include "UiPropertiesPanel.h"
#include "UiSceneTreePanel.h"

#include "Log/SyLogger.h"

#include <QCoreApplication>
#include <QLabel>
#include <QSizePolicy>
#include <QWidget>

namespace
{
    /// 状态栏文本槽位：统一样式（左右留白 + 不参与布局拉伸）
    QLabel* makeStatusLabel(QWidget* parent, const QString& text)
    {
        auto* label = new QLabel(text, parent);
        label->setContentsMargins(6, 0, 6, 0);
        return label;
    }
}  // namespace

void registerBuiltinUiPanels(UiPanelRegistry& registry)
{
    // ==================== Dock 面板 ====================

    registry.registerPanel(QStringLiteral("SceneTreePanel"), [](QWidget* parent) {
        return static_cast<QWidget*>(new SceneTreePanel(parent));
    });
    registry.registerPanel(QStringLiteral("PropertiesPanel"), [](QWidget* parent) {
        return static_cast<QWidget*>(new PropertiesPanelWidget(parent));
    });

    // ==================== 状态栏槽位 ====================
    // 这些是**框架级**常驻指示器，与各工作台自己的 StatusBarBase 子类互不冲突：
    // 工作台状态栏由 WorkbenchWindow::mountStatusBar 挂载，负责坐标/选择/消息；
    // 这里的槽位负责跨工作台恒定的信息（客户标识、授权状态等）。

    // 客户标识：定制交付场景下现场排查的第一手信息——
    // 「界面不对」往往是客户配置没生效，看一眼这里就能确认。
    registry.registerPanel(QStringLiteral("ClientIndicator"), [](QWidget* parent) {
        const QString clientId = UiClientContext::instance().clientId();
        auto* label = makeStatusLabel(parent,
            QCoreApplication::translate("UiStatusBar", "Client: %1").arg(clientId));
        label->setToolTip(QCoreApplication::translate("UiStatusBar", "Active UI client configuration: %1").arg(clientId));
        return static_cast<QWidget*>(label);
    });

    // 授权状态：无限制模式显示 Full，否则显示已授权功能数量
    registry.registerPanel(QStringLiteral("LicenseIndicator"), [](QWidget* parent) {
        const UiFeatureGate& gate = UiFeatureGate::instance();
        const QString text = gate.isUnrestricted()
            ? QCoreApplication::translate("UiStatusBar", "License: Full")
            : QCoreApplication::translate("UiStatusBar", "License: %1 features")
                  .arg(gate.licensedFeatures().size());
        auto* label = makeStatusLabel(parent, text);
        if (!gate.isUnrestricted())
        {
            label->setToolTip(gate.licensedFeatures().join(QStringLiteral(", ")));
        }
        return static_cast<QWidget*>(label);
    });

    // 空白填充：把后续槽位推到右侧，供客户调整状态栏排布
    registry.registerPanel(QStringLiteral("Spacer"), [](QWidget* parent) {
        auto* spacer = new QWidget(parent);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return static_cast<QWidget*>(spacer);
    });

    // 通用文本槽位：内容为空，由框架按 objectName（JSON 中的 id）查找后写入文本
    registry.registerPanel(QStringLiteral("MessageLabel"), [](QWidget* parent) {
        return static_cast<QWidget*>(makeStatusLabel(parent, QString()));
    });

    SY_DEBUGF("[UiBuiltinPanels] Registered %d builtin panel/slot factories: %s",
        static_cast<int>(registry.registeredPanelIds().size()),
        qPrintable(registry.registeredPanelIds().join(QLatin1Char(','))));
}
