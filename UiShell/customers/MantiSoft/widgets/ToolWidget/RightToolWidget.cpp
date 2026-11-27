#include "RightToolWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace MS
{
    // —— 工具函数 ——
    static void addSeparator(QGridLayout* grid, int row, int colSpan)
    {
        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        grid->addWidget(line, row, 0, 1, colSpan);
    }

    static QPushButton* makeIconBtn(const QString& tooltip, QWidget* parent)
    {
        auto* btn = new QPushButton(parent);
        btn->setToolTip(tooltip);
        btn->setFixedSize(35, 35);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  border: 1px solid #3a3a3a; border-radius: 3px; background: #2a2a2a; color: #ddd;"
            "}"
            "QPushButton:hover { background: #353535; }"
            "QPushButton:pressed { background: #1f1f1f; }"));
        return btn;
    }

    static QPushButton* makeActionBtn(const QString& text, QWidget* parent)
    {
        auto* btn = new QPushButton(text, parent);
        btn->setFixedSize(95, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  border: 1px solid #3a3a3a; border-radius: 3px; background: #333; color: #ddd; font-size: 12px;"
            "}"
            "QPushButton:hover { background: #3d3d3d; }"));
        return btn;
    }

    // —— RightToolWidget ——

    RightToolWidget::RightToolWidget(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("RightToolWidget"));
        setAttribute(Qt::WA_StyledBackground);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        auto* scrolLayout = new QVBoxLayout;
        scrolLayout->setContentsMargins(1, 0, 1, 0);
        scrolLayout->setSpacing(0);

        m_scroll = new QScrollArea(this);
        m_scroll->setWidgetResizable(true);
        connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { this->update(); });
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        auto* scrollHost = new QWidget(m_scroll);
        m_scroll->setWidget(scrollHost);

        m_contentLayout = new QVBoxLayout(scrollHost);
        m_contentLayout->setContentsMargins(1, 10, 1, 0);
        m_contentLayout->setSpacing(5);

        scrolLayout->addWidget(m_scroll);
        setLayout(scrolLayout);

        // —— 构造期一次性创建全部四个子面板（全部 hide，由工具栏按钮显式打开）——
        const char* ids[] = { "property", "coordinate", "manual", "process" };
        using InitFn = QWidget * (RightToolWidget::*)();
        InitFn inits[] = {
            &RightToolWidget::initPropertyPanel,
            &RightToolWidget::initCoordinatePanel,
            &RightToolWidget::initManualPanel,
            &RightToolWidget::initProcessPanel,
        };

        for (int i = 0; i < 4; ++i)
        {
            auto* panel = (this->*inits[i])();
            panel->hide();
            m_panels.insert(QString::fromLatin1(ids[i]), panel);
            m_panelStates.insert(QString::fromLatin1(ids[i]), false);
            m_contentLayout->addWidget(panel);
        }

        m_contentLayout->addStretch(1);
    }

    // —— 公开 API ——

    void RightToolWidget::togglePanel(const QString& panelId)
    {
        auto it = m_panelStates.find(panelId);
        if (it == m_panelStates.end()) return;
        applyPanelVisible(panelId, !it.value());
    }

    void RightToolWidget::showPanel(const QString& panelId)
    {
        auto it = m_panelStates.constFind(panelId);
        if (it == m_panelStates.constEnd() || it.value()) return;
        applyPanelVisible(panelId, true);
    }

    void RightToolWidget::hidePanel(const QString& panelId)
    {
        auto it = m_panelStates.constFind(panelId);
        if (it == m_panelStates.constEnd() || !it.value()) return;
        applyPanelVisible(panelId, false);
    }

    void RightToolWidget::clearAllPanels()
    {
        for (auto it = m_panelStates.begin(); it != m_panelStates.end(); ++it)
            applyPanelVisible(it.key(), false);
    }

    bool RightToolWidget::isPanelVisible(const QString& panelId) const
    {
        auto it = m_panelStates.constFind(panelId);
        return it != m_panelStates.constEnd() && it.value();
    }

    QSet<QString> RightToolWidget::visiblePanelIds() const
    {
        QSet<QString> result;
        for (auto it = m_panelStates.constBegin(); it != m_panelStates.constEnd(); ++it)
            if (it.value()) result.insert(it.key());
        return result;
    }

    bool RightToolWidget::hasAnyVisible() const
    {
        for (auto it = m_panelStates.constBegin(); it != m_panelStates.constEnd(); ++it)
            if (it.value()) return true;
        return false;
    }

    void RightToolWidget::applyPanelVisible(const QString& panelId, bool visible)
    {
        auto stateIt = m_panelStates.find(panelId);
        if (stateIt == m_panelStates.end()) return;
        if (stateIt.value() == visible) return;

        stateIt.value() = visible;

        auto panelIt = m_panels.find(panelId);
        if (panelIt != m_panels.end())
            panelIt.value()->setVisible(visible);

        if (visible)
            emit sigPanelShown(panelId);
        else
            emit sigPanelHidden(panelId);
    }

    // ————————————————————————————————————————————
    // initPropertyPanel  —— 对齐源项目 RightPropertyWidget.ui
    // ————————————————————————————————————————————
    // 源项目控件树：
    //   VBoxLayout → GridLayout(gridLayout) 4列4行
    //   row0:  label "Start from:" + originComBox  |  (spacer)   | label "Job origin:" | originPosComBox(隐藏)
    //   row1:  (源项目 removeItem 后的空位)         |              | originWidgetFrame(58×58)   | (源项目隐藏)
    //   row2:  _enableSelect checkbox "Cut selected shapes only"  |  合并跨2列
    //   row3:  _originBox checkbox "Use new selected origin"      |  合并跨2列
    //
    // 源项目 initProperty() 在 originWidgetFrame 里塞 OriginWidget（3×3 九宫格按钮选图形原点）
    // ————————————————————————————————————————————
    QWidget* RightToolWidget::initPropertyPanel()
    {
        auto* panel = new QWidget(this);
        panel->setProperty("panelId", QStringLiteral("property"));

        // 外层 VBoxLayout
        auto* outerLay = new QVBoxLayout(panel);
        outerLay->setContentsMargins(0, 0, 0, 0);
        outerLay->setSpacing(6);

        // GridLayout：源项目 gridLayout，4列4行
        auto* grid = new QGridLayout;
        grid->setContentsMargins(15, 0, 15, 20);
        grid->setHorizontalSpacing(15);
        grid->setVerticalSpacing(5);

        // row 0 col 0-1: label "Start from:" + originComBox
        auto* originLbl = new QLabel(QCoreApplication::translate("MantiSoft", "Start from:"));
        originLbl->setFixedWidth(70);
        auto* originComBox = new QComboBox;
        originComBox->setFixedSize(140, 24);
        originComBox->addItem(QCoreApplication::translate("MantiSoft", "Current position"));
        originComBox->addItem(QCoreApplication::translate("MantiSoft", "Origin position"));
        originComBox->addItem(QCoreApplication::translate("MantiSoft", "Machine zero"));
        originComBox->addItem(QCoreApplication::translate("MantiSoft", "Absolute coordinate"));

        auto* row0 = new QWidget;
        auto* row0Lay = new QHBoxLayout(row0);
        row0Lay->setContentsMargins(0, 0, 0, 0);
        row0Lay->setSpacing(5);
        row0Lay->addWidget(originLbl);
        row0Lay->addWidget(originComBox);
        row0Lay->addStretch();
        grid->addWidget(row0, 0, 0, 1, 2);

        // row 0 col 2: label "Job origin:"
        auto* refLbl = new QLabel(QCoreApplication::translate("MantiSoft", "Job origin:"));
        grid->addWidget(refLbl, 0, 2);

        // row 1 col 2-3: originWidgetFrame（58×58 QFrame + 3×3 九宫格选原点）
        auto* originFrame = new QFrame;
        originFrame->setFixedSize(58, 58);
        originFrame->setFrameShape(QFrame::StyledPanel);
        originFrame->setFrameShadow(QFrame::Raised);
        grid->addWidget(originFrame, 1, 2, 1, 2, Qt::AlignTop);

        // 在 originFrame 里塞 3×3 九宫格按钮（对齐源项目 OriginWidget）
        static const char* kOrigins[9] = {
            "↖", "↑", "↗",
            "←", "●", "→",
            "↙", "↓", "↘"
        };
        auto* ogLay = new QGridLayout(originFrame);
        ogLay->setContentsMargins(2, 2, 2, 2);
        ogLay->setSpacing(1);
        for (int i = 0; i < 9; ++i)
        {
            auto* btn = new QPushButton(kOrigins[i], originFrame);
            btn->setFixedSize(17, 17);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setCheckable(true);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { border: 1px solid #555; background: #2a2a2a; color: #bbb; font-size: 10px; }"
                "QPushButton:checked { background: #3a7; color: white; border-color: #4b8; }"));
            ogLay->addWidget(btn, i / 3, i % 3);
            if (i == 4) btn->setChecked(true);  // 默认选中中心
        }

        // row 2 col 0-1: Cut selected shapes only
        auto* cutCheck = new QCheckBox(QCoreApplication::translate("MantiSoft", "Cut selected shapes only"));
        grid->addWidget(cutCheck, 2, 0, 1, 2);

        // row 3 col 0-1: Use new selected origin
        auto* newOriginCheck = new QCheckBox(QCoreApplication::translate("MantiSoft", "Use new selected origin"));
        grid->addWidget(newOriginCheck, 3, 0, 1, 2);

        outerLay->addLayout(grid);
        return panel;
    }

    // ————————————————————————————————————————————
    // initCoordinatePanel  —— 对齐源项目 RightPosWidget.ui
    // ————————————————————————————————————————————
    // 源项目控件树：
    //   VBoxLayout → HBoxLayout → GridLayout 4列9行
    //   row0: X / _disPlayX(当前) / _disPlayXX(相对) / mm
    //   row1: ──分隔线──
    //   row2: Y1 / _disPlayY1 / _disPlayYY1 / mm
    //   row3: ──分隔线──
    //   row4: Y2 / _disPlayY2 / _disPlayYY2 / mm
    //   row5: ──分隔线──
    //   row6: Z / _disPlayZ / _disPlayZZ / mm
    //   row7: ──分隔线──
    //   row8: U / _disPlayU / _disPlayUU / mm
    // ————————————————————————————————————————————
    QWidget* RightToolWidget::initCoordinatePanel()
    {
        auto* panel = new QWidget(this);
        panel->setProperty("panelId", QStringLiteral("coordinate"));

        auto* outerLay = new QVBoxLayout(panel);
        outerLay->setContentsMargins(0, 0, 0, 0);
        outerLay->setSpacing(0);

        // 内部 HBoxLayout (源项目水平容器)
        auto* innerLay = new QHBoxLayout;
        innerLay->setContentsMargins(0, 0, 0, 0);
        innerLay->setSpacing(0);

        // 核心 GridLayout
        auto* grid = new QGridLayout;
        grid->setContentsMargins(15, 15, 15, 15);
        grid->setVerticalSpacing(5);
        grid->setHorizontalSpacing(15);

        // 数值标签样式（LCD 风格，对齐源项目坐标显示）
        auto makeValLabel = [panel](const QString& text) -> QLabel* {
            auto* lbl = new QLabel(text, panel);
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lbl->setFixedHeight(20);
            lbl->setStyleSheet(QStringLiteral(
                "QLabel { color: #4fc3f7; font-family: 'Consolas', 'Courier New'; font-size: 13px; }"));
            return lbl;
            };

        auto makeUnitLabel = [panel](const QString& text) -> QLabel* {
            auto* lbl = new QLabel(text, panel);
            lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            lbl->setFixedHeight(20);
            lbl->setStyleSheet(QStringLiteral("QLabel { color: #888; font-size: 11px; }"));
            return lbl;
            };

        auto makeAxisLabel = [panel](const QString& text) -> QLabel* {
            auto* lbl = new QLabel(text, panel);
            lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            lbl->setFixedHeight(20);
            lbl->setStyleSheet(QStringLiteral("QLabel { color: #bbb; font-weight: bold; font-size: 12px; }"));
            return lbl;
            };

        // 源项目里的坐标轴：X、Y1/Y2（双 Y）、Z、U
        struct AxisRow
        {
            const char* axisLabel;
            const char* unitLabel;
            QString curVal;   // 当前值（相对机台原点）
            QString relVal;   // 相对值（相对工件原点）
        };
        AxisRow rows[] = {
            { "X",  "mm", "125.450", "0.000" },
            { "Y1", "mm",  "88.320", "0.000" },
            { "Y2", "mm",  "88.320", "0.000" },
            { "Z",  "mm",   "5.000", "0.000" },
            { "U",  "mm",   "0.000", "0.000" },
        };

        int row = 0;
        for (int i = 0; i < 5; ++i)
        {
            // 轴标签 (col 0)
            grid->addWidget(makeAxisLabel(rows[i].axisLabel), row, 0);
            // 当前值 (col 1)
            grid->addWidget(makeValLabel(rows[i].curVal), row, 1);
            // 相对值 (col 2)
            grid->addWidget(makeValLabel(rows[i].relVal), row, 2);
            // 单位 (col 3)
            grid->addWidget(makeUnitLabel(rows[i].unitLabel), row, 3);

            row++;

            // 每两行加一条分隔线（row 1, 3, 5, 7）
            if (i < 4)
            {
                addSeparator(grid, row, 4);
                row++;
            }
        }

        innerLay->addLayout(grid);
        outerLay->addLayout(innerLay);
        return panel;
    }

    // ————————————————————————————————————————————
    // initManualPanel  —— 对齐源项目 RightManualWidget.ui
    // ————————————————————————————————————————————
    // 源项目控件树：
    //   VBoxLayout_2(margins 0) → VBoxLayout(margins 15, spacing 10)
    //   item0:  inchingBox checkbox "Jog"
    //   item1:  QHBoxLayout(左 formLayout + 右 gridLayout)
    //     左 formLayout(4行): Speed / Jog distance / Z Speed / Shot power
    //     右 gridLayout(3×3 九宫方向键, 35×35按钮):
    //       ┌────────┬───────┬────────┐
    //       │ ↖leftR │  ↑up  │ ↗rightR│
    //       ├────────┼───────┼────────┤
    //       │ ←left  │ ⚡speed│ →right │
    //       ├────────┼───────┼────────┤
    //       │ Z↓zDown│  ↓down│ Z↑zUp  │
    //       └────────┴───────┴────────┘
    //   item2:  QFrame(rightToolButtonBox) → GridLayout_2(2行3列, 95×28按钮):
    //       Reset | Go to  | Shot
    //       Go origin | Go dock | Auto-focus
    // ————————————————————————————————————————————
    QWidget* RightToolWidget::initManualPanel()
    {
        auto* panel = new QWidget(this);
        panel->setObjectName(QStringLiteral("manualWidget"));
        panel->setProperty("panelId", QStringLiteral("manual"));

        // 外层 VBoxLayout
        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(15, 15, 15, 20);
        outer->setSpacing(10);

        // item0: Jog checkbox
        auto* jogCheck = new QCheckBox(QCoreApplication::translate("MantiSoft", "Jog"));
        outer->addWidget(jogCheck);

        // item1: HBoxLayout → 左表单 + 右方向键
        auto* midHBox = new QHBoxLayout;
        midHBox->setSpacing(14);

        // 左：QFormLayout（4 行参数）
        auto* pForm = new QFormLayout;
        pForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pForm->setHorizontalSpacing(5);
        pForm->setVerticalSpacing(10);
        pForm->setContentsMargins(0, 0, 0, 0);

        auto makeSpin = []() -> QDoubleSpinBox* {
            auto* sb = new QDoubleSpinBox;
            sb->setFixedHeight(24);
            sb->setDecimals(3);
            sb->setRange(0.001, 9999);
            sb->setSingleStep(0.1);
            return sb;
            };

        auto* speedLab = new QLabel(QCoreApplication::translate("MantiSoft", "Speed (mm/s):"));
        auto* axisSpeed = makeSpin(); axisSpeed->setValue(10.0);
        pForm->addRow(speedLab, axisSpeed);

        auto* jogDistLab = new QLabel(QCoreApplication::translate("MantiSoft", "Jog distance(mm):"));
        auto* jogDist = makeSpin(); jogDist->setValue(1.0); jogDist->setRange(0.01, 9999);
        pForm->addRow(jogDistLab, jogDist);

        auto* zSpeedLab = new QLabel(QCoreApplication::translate("MantiSoft", "Z Speed (mm/s):"));
        auto* zSpeed = makeSpin(); zSpeed->setValue(5.0);
        pForm->addRow(zSpeedLab, zSpeed);

        auto* shotPowerLab = new QLabel(QCoreApplication::translate("MantiSoft", "Shot power(%):"));
        auto* shotPower = makeSpin(); shotPower->setDecimals(1); shotPower->setRange(0.1, 100); shotPower->setValue(50.0);
        pForm->addRow(shotPowerLab, shotPower);

        midHBox->addLayout(pForm);

        // 右：3×3 方向九宫格
        auto* dirGrid = new QGridLayout;
        dirGrid->setSpacing(4);

        // row0: leftReset, up, rightReset
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Reset Left"), panel), 0, 0);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Up"), panel), 0, 1);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Reset Right"), panel), 0, 2);
        // row1: left, speed(中心), right
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Left"), panel), 1, 0);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Speed Toggle"), panel), 1, 1);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Right"), panel), 1, 2);
        // row2: zDown, down, zUp
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Z Down"), panel), 2, 0);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Down"), panel), 2, 1);
        dirGrid->addWidget(makeIconBtn(QStringLiteral("Z Up"), panel), 2, 2);

        midHBox->addLayout(dirGrid);
        outer->addLayout(midHBox);

        // item2: 底部 2×3 操作按钮
        auto* btnFrame = new QFrame;
        btnFrame->setContentsMargins(0, 0, 0, 0);
        auto* btnGrid = new QGridLayout(btnFrame);
        btnGrid->setContentsMargins(0, 10, 0, 0);
        btnGrid->setHorizontalSpacing(10);
        btnGrid->setVerticalSpacing(15);

        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Reset"), panel), 0, 0);
        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Go to"), panel), 0, 1);
        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Shot"), panel), 0, 2);
        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Go origin"), panel), 1, 0);
        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Go dock"), panel), 1, 1);
        btnGrid->addWidget(makeActionBtn(QCoreApplication::translate("MantiSoft", "Auto-focus"), panel), 1, 2);

        outer->addWidget(btnFrame);
        return panel;
    }

    // ————————————————————————————————————————————
    // initProcessPanel  —— 对齐源项目 RightMachinWidget.ui
    // ————————————————————————————————————————————
    // 源项目控件树：
    //   VBoxLayout(margins 0) → GridLayout 2行4列 RdToolButton
    //   row0: Start | Loop | Stop | Send file
    //   row1: Save file | Download | Origin | Border
    // ————————————————————————————————————————————
    QWidget* RightToolWidget::initProcessPanel()
    {
        auto* panel = new QWidget(this);
        panel->setObjectName(QStringLiteral("machinWidget"));
        panel->setProperty("panelId", QStringLiteral("process"));

        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto* grid = new QGridLayout;
        grid->setContentsMargins(15, 10, 15, 10);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);

        auto makeMachinBtn = [panel](const QString& text) -> QPushButton* {
            auto* btn = new QPushButton(text, panel);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  border: 1px solid #3a3a3a; border-radius: 4px; background: #2e2e2e; color: #ddd;"
                "  padding: 8px 12px; font-size: 12px;"
                "}"
                "QPushButton:hover { background: #383838; border-color: #555; }"
                "QPushButton:pressed { background: #242424; }"));
            return btn;
            };

        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Start")), 0, 0);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Loop")), 0, 1);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Stop")), 0, 2);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Send file")), 0, 3);

        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Save file")), 1, 0);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Download")), 1, 1);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Origin")), 1, 2);
        grid->addWidget(makeMachinBtn(QCoreApplication::translate("MantiSoft", "Border")), 1, 3);

        outer->addLayout(grid);
        return panel;
    }
}  // namespace MS