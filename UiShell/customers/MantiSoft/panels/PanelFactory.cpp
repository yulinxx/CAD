#include "PanelFactory.h"

#include <QAction>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "UiDialogs/SampleDialogs.h"
#include "UiShell/Contract/IShellHost.h"
#include "UiShell/Contract/Retranslatable.h"
#include "widgets/SidePanel/LeftSidePanel.h"
#include "widgets/SidePanel/RightSidePanel.h"

namespace
{
    using RetranslatableLabel = RD::Retranslatable<QLabel>;
    using RetranslatableFrame = RD::Retranslatable<QFrame>;

    // 状态栏面板：显示 IShellHost::showMessage() 推来的提示；语言切换时恢复默认文字。
    class StatusLabel final : public RD::Retranslatable<QLabel>, public RD::IStatusMessageView
    {
    public:
        explicit StatusLabel(QWidget* parent)
            : RD::Retranslatable<QLabel>(parent)
        {
        }

        void showStatusMessage(const QString& text) override
        {
            setText(text);
        }
    };

    QString panelTitle(const QString& panelId)
    {
        if (panelId == QLatin1String("top.banner"))
            return QCoreApplication::translate("MantiSoft", "MantiSoft top toolbar");
        if (panelId == QLatin1String("tools"))
            return QCoreApplication::translate("MantiSoft", "Tools");
        if (panelId == QLatin1String("inspector"))
            return QCoreApplication::translate("MantiSoft", "Properties");
        if (panelId == QLatin1String("status"))
            return QCoreApplication::translate("MantiSoft", "Ready");
        return panelId;
    }
}  // namespace

namespace MS
{
    // —— PanelFactory ——

    QWidget* PanelFactory::create(const RD::PanelCreateRequest& info)
    {
        // —— 左侧工具栏 + 展开面板：MantiSoft 自己的 LeftToolBar / LeftToolWidget 组合 ——
        if (info.strPanelId == QLatin1String("tools"))
        {
            auto* panel = new MS::LeftSidePanel(info.host, info.parent);
            panel->setObjectName(QStringLiteral("panel:") + info.strPanelId);
            RD::markPanel(panel, QStringLiteral("panel"), QStringLiteral("mantisoft"));
            return panel;
        }

        // —— 右侧工具栏 + 硬件面板：Property / Coordinate / Manual / Process ——
        if (info.strPanelId == QLatin1String("inspector"))
        {
            auto* panel = new MS::RightSidePanel(info.host, info.parent);
            panel->setObjectName(QStringLiteral("panel:") + info.strPanelId);
            RD::markPanel(panel, QStringLiteral("panel"), QStringLiteral("mantisoft"));
            return panel;
        }

        if (info.strPanelId == QLatin1String("status"))
        {
            // —— 底部状态栏（仿源项目 StatusBar）——
            // 结构：消息区（左） | 弹性 stretch | 当前工具 | 坐标显示 | 缩放比例（右）
            // 低耦合：StatusLabel 继续实现 IStatusMessageView 接收宿主 showMessage；
            // 其余标签仅做 UI 占位，后续由 Mode / 业务模块按需注入真实数据源。
            auto* bar = new QWidget(info.parent);
            bar->setObjectName(QStringLiteral("panel:") + info.strPanelId);
            RD::markPanel(bar, QStringLiteral("panelStatus"), QStringLiteral("mantisoft"));

            auto* hLay = new QHBoxLayout(bar);
            hLay->setContentsMargins(8, 2, 8, 2);
            hLay->setSpacing(10);

            // 左侧：消息提示区（实现 IStatusMessageView，接收 IShellHost::showMessage）
            auto* msgLabel = new StatusLabel(bar);
            msgLabel->setMinimumWidth(200);
            msgLabel->setText(panelTitle(info.strPanelId));
            hLay->addWidget(msgLabel, 1);  // 消息区占主要弹性空间

            // 右侧：当前绘图工具
            auto* toolLabel = new QLabel(bar);
            toolLabel->setObjectName(QStringLiteral("status.tool"));
            toolLabel->setText(QCoreApplication::translate("MantiSoft", "Tool: Select"));
            toolLabel->setStyleSheet(QStringLiteral("color: #888; padding: 0 6px;"));
            toolLabel->setMinimumWidth(110);
            toolLabel->setAlignment(Qt::AlignCenter);
            hLay->addWidget(toolLabel);

            // 右侧：坐标显示（占位）
            auto* coordLabel = new QLabel(bar);
            coordLabel->setObjectName(QStringLiteral("status.coord"));
            coordLabel->setText(QStringLiteral("X: 0.00  Y: 0.00"));
            coordLabel->setStyleSheet(QStringLiteral("color: #888; padding: 0 6px;"));
            coordLabel->setMinimumWidth(140);
            coordLabel->setAlignment(Qt::AlignCenter);
            hLay->addWidget(coordLabel);

            // 右侧：缩放比例（占位）
            auto* zoomLabel = new QLabel(bar);
            zoomLabel->setObjectName(QStringLiteral("status.zoom"));
            zoomLabel->setText(QStringLiteral("100%"));
            zoomLabel->setStyleSheet(QStringLiteral("color: #888; padding: 0 6px;"));
            zoomLabel->setMinimumWidth(60);
            zoomLabel->setAlignment(Qt::AlignCenter | Qt::AlignRight);
            hLay->addWidget(zoomLabel);

            // 语言切换时，消息区重置默认文字（业务模块会重新 showMessage）；
            // 右侧 QLabel 是纯 UI 占位（QLabel 没有 onRetranslate 成员），
            // 后续业务模块接真实数据源时，会在自己的 onRetranslate 里更新它们。
            msgLabel->onRetranslate([msgLabel, panelId = info.strPanelId]() {
                msgLabel->setText(panelTitle(panelId));
                });

            return bar;
        }

        auto* frame = new RetranslatableFrame(info.parent);
        frame->setObjectName(QStringLiteral("panel:") + info.strPanelId);
        RD::markPanel(frame, QStringLiteral("panel"), QStringLiteral("mantisoft"));

        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        auto* title = new QLabel(panelTitle(info.strPanelId), frame);
        RD::markPanel(title, QStringLiteral("panelTitle"), QStringLiteral("mantisoft"));
        layout->addWidget(title);
        frame->onRetranslate([title, panelId = info.strPanelId]() {
            title->setText(panelTitle(panelId));
            });

        auto* hint = new QLabel(QStringLiteral("mantisoft / ") + info.strPanelId, frame);
        layout->addWidget(hint);

        if (info.strPanelId == QLatin1String("inspector"))
        {
            auto* widthLabel = new QLabel(QCoreApplication::translate("MantiSoft", "Width:  100.0 mm"), frame);
            layout->addWidget(widthLabel);
            auto* heightLabel = new QLabel(QCoreApplication::translate("MantiSoft", "Height:  60.0 mm"), frame);
            layout->addWidget(heightLabel);
            frame->onRetranslate([widthLabel, heightLabel]() {
                widthLabel->setText(QCoreApplication::translate("MantiSoft", "Width:  100.0 mm"));
                heightLabel->setText(QCoreApplication::translate("MantiSoft", "Height:  60.0 mm"));
                });
        }

        layout->addStretch(1);
        return frame;
    }

    // —— CenterView ——

    QWidget* CenterView::createCenterView(QWidget* parent)
    {
        auto* view = new RetranslatableLabel(parent);
        view->setText(QCoreApplication::translate("MantiSoft", "MantiSoft Central Design Area"));
        view->setObjectName(QStringLiteral("center.view"));
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(420, 300);
        RD::markPanel(view, QStringLiteral("centerView"), QStringLiteral("mantisoft"));
        view->onRetranslate([view]() {
            view->setText(QCoreApplication::translate("MantiSoft", "MantiSoft Central Design Area"));
            });
        return view;
    }
}  // namespace MS