#include "RightToolBar.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QFrame>
#include <QIcon>
#include <QMenu>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

namespace MS
{
    namespace
    {
        // 图标文件统一放在 MantiSoft.qrc 的 /MantiSoft/icons 前缀下；
        // 辅助名字带 Right 前缀，避免与 LeftToolBar / 源项目同名辅助符号冲突。
        const char* kRightIconPrefix = ":/MantiSoft/icons/";
        inline QString rightIconPath(const char* file)
        {
            return QString::fromLatin1(kRightIconPrefix) + QString::fromLatin1(file);
        }

        QString rtr(const char* text)
        {
            return QCoreApplication::translate("MantiSoft", text);
        }

        // 次级工具区菜单项：commandId 直接对应 Commands 里登记的命令；
        // id/label/icon 与源项目 RightToolBar 的菜单顺序一致。
        struct MenuItemDesc
        {
            const char* commandId;
            const char* label;
            const char* icon;
        };
    }

    RightToolBar::RightToolBar(QWidget* parent)
        : QWidget(parent)
    {
        // 必须与 QSS 里 QWidget#RightToolBar / QToolButton#ToolBarButton 选择器对齐
        setObjectName(QStringLiteral("RightToolBar"));
        setFixedWidth(40);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(2);

        // 顶部分隔线（源项目 addSeparator()）
        auto* topLine = new QFrame(this);
        topLine->setFrameShape(QFrame::HLine);
        topLine->setFixedHeight(1);
        layout->addWidget(topLine);

        buildButtons(layout);

        // 中部可拉伸区：把次级工具区和折叠按钮压到底部
        layout->addStretch(1);

        // 次级工具区上方的分隔线（折叠时一起隐藏）
        m_secondaryLine = new QFrame(this);
        m_secondaryLine->setFrameShape(QFrame::HLine);
        m_secondaryLine->setFixedHeight(1);
        layout->addWidget(m_secondaryLine);

        m_secondarySection = new QWidget(this);
        auto* secondaryLay = new QVBoxLayout(m_secondarySection);
        secondaryLay->setContentsMargins(0, 2, 0, 2);
        secondaryLay->setSpacing(2);
        layout->addWidget(m_secondarySection);

        buildSecondaryTools(secondaryLay);

        // 底部折叠/展开按钮（源项目 ToolBarButton + BarCollapseIcon / BarExpandIcon）
        m_expandBtn = new QToolButton(this);
        m_expandBtn->setObjectName(QStringLiteral("ToolBarButton"));
        m_expandBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_expandBtn->setFixedSize(32, 32);
        m_expandBtn->setIconSize(QSize(20, 20));
        m_expandBtn->setIcon(QIcon(rightIconPath("ExpandDown.svg")));
        m_expandBtn->setToolTip(rtr("Contract"));
        connect(m_expandBtn, &QToolButton::clicked, this, &RightToolBar::onSecondaryToggle);
        layout->addWidget(m_expandBtn);

        // 源项目底部固定 37px 留白（spacer2），保证最后一个按钮不贴底
        auto* bottomSpacer = new QWidget(this);
        bottomSpacer->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        bottomSpacer->setFixedHeight(37);
        layout->addWidget(bottomSpacer);
    }

    void RightToolBar::onSecondaryToggle()
    {
        m_bSecondaryExpanded = !m_bSecondaryExpanded;
        m_secondarySection->setVisible(m_bSecondaryExpanded);
        m_secondaryLine->setVisible(m_bSecondaryExpanded);

        m_expandBtn->setIcon(QIcon(rightIconPath(
            m_bSecondaryExpanded ? "ExpandDown.svg" : "ExpandUp.svg")));
        m_expandBtn->setToolTip(m_bSecondaryExpanded ? rtr("Contract") : rtr("Expand"));
    }

    QToolButton* RightToolBar::createToolButton(const QString& panelId, const QString& tooltip)
    {
        auto* btn = new QToolButton(this);
        // objectName 必须与 QSS 里 QToolButton#ToolBarButton 选择器对齐
        btn->setObjectName(QStringLiteral("ToolBarButton"));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(tooltip);
        btn->setProperty("panelId", panelId);
        btn->setFixedSize(32, 32);
        btn->setIconSize(QSize(20, 20));
        btn->setCheckable(true);
        // 注意：不设 setAutoExclusive —— 多个按钮可同时 checked，对应右侧面板累加显示。
        // 初始全部 unchecked（避免按钮状态和面板实际状态不一致），
        // 由 RightSidePanel / RightToolWidget 的显隐变化反向同步。

        connect(btn, &QToolButton::clicked, this, [this, btn]() {
            // 按钮 clicked 信号发出时，Qt 已经把 checked 状态翻转完毕，
            // 接收方可以直接用 btn->isChecked() 判断开/关。
            emit sigPanelButtonClicked(btn->property("panelId").toString());
            });

        m_toolButtons.append(btn);
        return btn;
    }

    QToolButton* RightToolBar::addToolButton(QVBoxLayout* lay, const QString& panelId,
        const QString& tooltip, const char* iconAlias)
    {
        auto* btn = createToolButton(panelId, tooltip);
        if (iconAlias)
        {
            const QIcon ico(rightIconPath(iconAlias));
            if (!ico.isNull())
                btn->setIcon(ico);
        }
        if (lay) lay->addWidget(btn);
        return btn;
    }

    void RightToolBar::setCurrentPanel(const QString& panelId)
    {
        // 语义：把 panelId 对应的按钮 checked，其余全部取消。
        // panelId 为空 → 全部 unchecked。
        for (QToolButton* btn : m_toolButtons)
        {
            const bool on = !panelId.isEmpty()
                && btn->property("panelId").toString() == panelId;
            btn->setChecked(on);
        }
    }

    void RightToolBar::setPanelActive(const QString& panelId, bool active)
    {
        for (QToolButton* btn : m_toolButtons)
        {
            if (btn->property("panelId").toString() == panelId)
            {
                btn->setChecked(active);
                return;
            }
        }
    }

    void RightToolBar::buildButtons(QVBoxLayout* lay)
    {
        // 源项目 RightToolBar 顶部四个面板切换按钮（State 在源项目被 HIDE_MACHINESTATE 关闭）。
        addToolButton(lay,
            QStringLiteral("property"),
            rtr("Attribute") + QStringLiteral("\n") + rtr("Show/Hide property information"),
            "Material.svg");

        addToolButton(lay,
            QStringLiteral("coordinate"),
            rtr("Coordinate") + QStringLiteral("\n") + rtr("Show/Hide position information"),
            "Coordinate.svg");

        addToolButton(lay,
            QStringLiteral("manual"),
            rtr("Manual") + QStringLiteral("\n") + rtr("Show/Hide manual control interface"),
            "Manual.svg");

        addToolButton(lay,
            QStringLiteral("process"),
            rtr("Work control") + QStringLiteral("\n") + rtr("Show/Hide process control interface"),
            "ConsoleProcessControl.svg");
    }

    void RightToolBar::buildSecondaryTools(QVBoxLayout* lay)
    {
        // —— 菜单按钮工具 ——
        // 与源项目一致：点击主按钮直接弹出菜单（InstantPopup），菜单项互斥行为的由命令自身决定。
        auto addMenuButton = [this, lay](const QString& tooltip, const char* mainIcon,
            const QVector<MenuItemDesc>& items) {
                auto* btn = new QToolButton(this);
                btn->setObjectName(QStringLiteral("ToolBarButton"));
                btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                btn->setFixedSize(32, 32);
                btn->setIconSize(QSize(20, 20));
                btn->setToolTip(tooltip);
                btn->setIcon(QIcon(rightIconPath(mainIcon)));
                btn->setPopupMode(QToolButton::InstantPopup);

                auto* menu = new QMenu(btn);
                auto* group = new QActionGroup(menu);
                group->setExclusive(true);
                for (const MenuItemDesc& it : items)
                {
                    QAction* act = menu->addAction(rtr(it.label));
                    if (it.icon) act->setIcon(QIcon(rightIconPath(it.icon)));
                    act->setData(QString::fromLatin1(it.commandId));
                    act->setCheckable(true);
                    group->addAction(act);
                    connect(act, &QAction::triggered, this, [this, btn, act]() {
                        // 与源项目 MenuToolBarButton 一致：主按钮图标跟随当前选中的子项
                        btn->setIcon(act->icon());
                        emit sigCommandTriggered(act->data().toString());
                        });
                }
                if (QAction* first = group->actions().value(0))
                    first->setChecked(true);
                btn->setMenu(menu);
                lay->addWidget(btn);
            };

        addMenuButton(rtr("Group"), "Group.svg",
            {
                { "edit.group.group",     "Group",        "Group.svg" },
                { "edit.group.unGroup",   "UnGroup",      "Disband.svg" },
                { "edit.group.unGroupAll","UnGroup All",  "DisbandAll.svg" },
            });

        addMenuButton(rtr("Mirror"), "VerticalMirror.svg",
            {
                { "edit.mirror.vertical", "Vertical Mirror",   "VerticalMirror.svg" },
                { "edit.mirror.horizon",  "Horizon Mirror",    "HorizontalMirroring.svg" },
                { "edit.mirror.angle",    "Angle Mirror",      "AngularMirroring.svg" },
            });

        addMenuButton(rtr("Alignment"), "AlignLeft.svg",
            {
                { "edit.align.left",     "Left-Aligned",         "AlignLeft.svg" },
                { "edit.align.right",    "Right-Aligned",        "AlignRight.svg" },
                { "edit.align.top",      "Top-Aligned",          "AlignTop.svg" },
                { "edit.align.bottom",   "Bottom-Aligned",       "AlignBottom.svg" },
                { "edit.align.horizon",  "Horizontal-Centered",  "HorizontallyCentered.svg" },
                { "edit.align.vertical", "Vertical-Centered",    "VerticallyCentered.svg" },
                { "edit.align.center",   "Center-Aligned",       "CenterAlignmentGeneral.svg" },
            });

        addMenuButton(rtr("Same Spacing"), "MandatoryEqualWidth.svg",
            {
                { "edit.equi.width",     "Width Equal",           "MandatoryEqualWidth.svg" },
                { "edit.equi.height",    "Height Equal",          "MandatoryContour.svg" },
                { "edit.equi.hDistribute","V-Center Equidistant", "VerticalCenterEquidistant.svg" },
                { "edit.equi.hCenter",   "H-Center Equidistant",  "HorizontalCenterIsometric.svg" },
                { "edit.equi.vSpace",    "Vertical Space Equal",  "VerticallyEquallySpaced.svg" },
                { "edit.equi.hSpace",    "Horizon Space Equal",   "HorizontallyEquallySpaced.svg" },
            });

        addMenuButton(rtr("Move Shapes To"), "MoveToCenterOfPage.svg",
            {
                { "edit.move.center",    "Move To Center",       "MoveToCenterOfPage.svg" },
                { "edit.move.lb",        "Move To Bottom-Left",  "MoveToTheBottomLeftCornerOfThePage.svg" },
                { "edit.move.rb",        "Move To Bottom-Right", "MoveToTheBottomRightCornerOfThePage.svg" },
                { "edit.move.lt",        "Move To Top-Left",     "MoveToTheTopLeftCornerOfThePage.svg" },
                { "edit.move.rt",        "Move To Top-Right",    "MoveToTheTopRightCornerOfThePage.svg" },
                { "edit.moveHead.center", "Move To Laser Head",  "MoveTheLaserHeadPosition.svg" },
                { "edit.move.left",      "Move To Left",         "MoveLeftGeneral.svg" },
                { "edit.move.right",     "Move To Right",        "MoveRightGeneral.svg" },
                { "edit.move.top",       "Move To Top",          "MoveTopGeneral.svg" },
                { "edit.move.bottom",    "Move To Bottom",       "MoveBottomGeneral.svg" },
            });

        addMenuButton(rtr("Zoom"), "ZoomInToPage.svg",
            {
                { "edit.zoom.page",     "Zoom To Page",  "ZoomInToPage.svg" },
                { "edit.zoom.selected", "Zoom Selected", "PartialZoom.svg" },
                { "edit.zoom.in",       "Zoom In",       "Enlarge.svg" },
                { "edit.zoom.out",      "Zoom Out",      "ZoomOut.svg" },
                { "edit.zoom.all",      "Show All",      "FitToCanvasRegular.svg" },
            });

        // —— Measurement 单按钮（源项目 btn_Measure → Edit.Measure）——
        auto* measureBtn = new QToolButton(this);
        measureBtn->setObjectName(QStringLiteral("ToolBarButton"));
        measureBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        measureBtn->setFixedSize(32, 32);
        measureBtn->setIconSize(QSize(20, 20));
        measureBtn->setToolTip(rtr("Measurement"));
        measureBtn->setIcon(QIcon(rightIconPath("Measurement.svg")));
        connect(measureBtn, &QToolButton::clicked, this, [this]() {
            emit sigCommandTriggered(QStringLiteral("edit.measure"));
            });
        lay->addWidget(measureBtn);
    }
}  // namespace MS