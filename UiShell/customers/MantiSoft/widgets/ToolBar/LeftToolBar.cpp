#include "LeftToolBar.h"

#include <functional>

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "UiShell/Contract/IShellHost.h"

namespace MS
{
    // 图标文件统一放在 MantiSoft.qrc 的 /MantiSoft/icons 前缀下；
    // 辅助名字加 Left 前缀，避免 Unity build 与 RightToolBar 的同名辅助符号冲突。
    namespace
    {
        const char* kIconPrefix = ":/MantiSoft/icons/";
        inline QString leftIconPath(const char* file)
        {
            return QString::fromLatin1(kIconPrefix) + QString::fromLatin1(file);
        }
    }

    namespace
    {
        // 组合按钮菜单的一项：id 只在本客户内部用于回传，label/icon 与旧版 LeftToolBar 对齐。
        struct LeftMenuItemDesc
        {
            const char* id;
            const char* label;
            const char* icon;
        };

        QString ltr(const char* text)
        {
            return QCoreApplication::translate("MantiSoft", text);
        }
    }  // namespace

    // 工具栏语义名 → 命令 id。只登记确实存在命令的项；没有对应命令的返回空串，
    // 避免误调别的命令。左侧栏与工具栏共用这一份映射。
    QString commandIdForToolId(const QString& toolId)
    {
        static const QHash<QString, QString> kMap = {
            { QStringLiteral("select"),      QStringLiteral("draw.select") },
            { QStringLiteral("pen.line"),      QStringLiteral("draw.line.line") },
            { QStringLiteral("pen.ellipse"),   QStringLiteral("draw.line.ellipse") },
            { QStringLiteral("pen.circle"),    QStringLiteral("draw.line.circle") },
            { QStringLiteral("pen.polygon"),   QStringLiteral("draw.line.polygon") },
            { QStringLiteral("pen.rectangle"), QStringLiteral("draw.line.rectangle") },
            { QStringLiteral("pen.star"),      QStringLiteral("draw.line.star") },
            { QStringLiteral("pen.point"),     QStringLiteral("draw.line.point") },
            { QStringLiteral("text"),        QStringLiteral("draw.text") },
            { QStringLiteral("qrcode"),      QStringLiteral("draw.qrcode") },
            { QStringLiteral("array"),         QStringLiteral("draw.array.matrix") },
            { QStringLiteral("circularArray"), QStringLiteral("draw.array.circle") },
            { QStringLiteral("offset"),      QStringLiteral("draw.offset") },
            { QStringLiteral("bool.weld"),      QStringLiteral("draw.bool.weld") },
            { QStringLiteral("bool.intersect"), QStringLiteral("draw.bool.intersect") },
            { QStringLiteral("bool.union"),     QStringLiteral("draw.bool.union") },
            { QStringLiteral("bool.subAB"),     QStringLiteral("draw.bool.subAB") },
            { QStringLiteral("bool.subBA"),     QStringLiteral("draw.bool.subBA") },
            { QStringLiteral("nodeEdit"),    QStringLiteral("draw.nodeEdit") },
            { QStringLiteral("chamfer.round"),  QStringLiteral("draw.chamfer.round") },
            { QStringLiteral("chamfer.linear"), QStringLiteral("draw.chamfer.linear") },
            { QStringLiteral("startPoint"),  QStringLiteral("draw.startPoint") },
            { QStringLiteral("link.lead"),   QStringLiteral("draw.link.lead") },
            { QStringLiteral("link.bridge"), QStringLiteral("draw.link.bridge") },
            { QStringLiteral("link.micro"),  QStringLiteral("draw.link.micro") },
            { QStringLiteral("link.coedge"), QStringLiteral("draw.link.coedge") },
            { QStringLiteral("bmp.vector"),  QStringLiteral("draw.bitmap.vec") },
            { QStringLiteral("bmp.trim"),    QStringLiteral("draw.bitmap.trim") },
            { QStringLiteral("bmp.proc"),    QStringLiteral("draw.bitmap.process") },
        };
        return kMap.value(toolId);
    }

    // —— 工具：创建一个标准的 32x32 ToolBar 按钮 ——
    static QToolButton* makeLeftBtn(QWidget* parent, const QString& toolId,
        const QString& tooltip, const char* iconAlias,
        bool checkable = false)
    {
        auto* btn = new QToolButton(parent);
        btn->setObjectName(QStringLiteral("ToolBarButton"));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setFixedSize(32, 32);
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(tooltip);
        btn->setProperty("toolId", toolId);
        btn->setCheckable(checkable);

        if (iconAlias)
        {
            QIcon ico(leftIconPath(iconAlias));
            if (!ico.isNull()) btn->setIcon(ico);
        }
        return btn;
    }

    // —— 工具：组合按钮（主按钮触发当前子项，右侧小三角展开菜单）——
    static QToolButton* makeLeftMenuBtn(QWidget* parent, QVBoxLayout* lay,
        const QString& toolId, const QString& tooltip,
        const char* mainIcon,
        const QVector<LeftMenuItemDesc>& items,
        const std::function<void(const QString&)>& onTrigger,
        AA::IShellHost* host = nullptr)
    {
        auto* btn = makeLeftBtn(parent, toolId, tooltip, mainIcon);

        auto* menu = new QMenu(btn);
        // 只有没有对应命令的本地项才需要本地互斥组；共享命令的互斥由命令状态（checkedWhen）保证。
        QActionGroup* group = nullptr;

        for (const LeftMenuItemDesc& it : items)
        {
            // 有命令的项直接使用共享动作：点击、勾选、置灰与菜单是同一个 QAction。
            QAction* shared = nullptr;
            if (host)
            {
                const QString commandId = commandIdForToolId(QString::fromLatin1(it.id));
                if (!commandId.isEmpty())
                    shared = host->commandAction(commandId);
            }

            if (shared)
            {
                menu->addAction(shared);
                QObject::connect(shared, &QAction::toggled, btn, [btn, shared](bool checked) {
                    if (checked) btn->setIcon(shared->icon());
                    });
                if (shared->isChecked())
                    btn->setIcon(shared->icon());
                continue;
            }

            if (!group)
            {
                group = new QActionGroup(menu);
                group->setExclusive(true);
            }

            auto* act = menu->addAction(ltr(it.label));
            if (it.icon) act->setIcon(QIcon(leftIconPath(it.icon)));
            act->setData(QString::fromLatin1(it.id));
            act->setCheckable(true);
            group->addAction(act);

            QObject::connect(act, &QAction::triggered, btn, [btn, act, onTrigger]() {
                btn->setIcon(act->icon());
                if (onTrigger) onTrigger(act->data().toString());
                });
        }

        // 本地组合项保持旧行为：默认选中第一项；共享命令项由命令状态决定。
        if (group)
        {
            if (QAction* first = group->actions().value(0))
                first->setChecked(true);
        }

        btn->setMenu(menu);
        btn->setPopupMode(QToolButton::MenuButtonPopup);  // 右侧小三角展开菜单

        // 主按钮点击：触发当前勾选的子项；没有勾选项时触发第一项。
        QObject::connect(btn, &QToolButton::clicked, btn, [btn, menu, onTrigger]() {
            QAction* act = nullptr;
            for (QAction* candidate : menu->actions())
            {
                if (candidate->isChecked())
                {
                    act = candidate;
                    break;
                }
            }
            if (!act)
                act = menu->actions().value(0);
            if (act)
            {
                btn->setIcon(act->icon());
                // 本地项仍走原来的回调；共享命令项由动作自己触发命令。
                const QString localId = act->data().toString();
                if (!localId.isEmpty())
                {
                    if (onTrigger) onTrigger(localId);
                }
                else
                {
                    act->trigger();
                }
            }
            });

        lay->addWidget(btn);
        return btn;
    }

    LeftToolBar::LeftToolBar(AA::IShellHost* host, QWidget* parent)
        : QWidget(parent)
        , m_host(host)
    {
        // 必须与 QSS 里 #LeftToolBar 选择器对齐
        setObjectName(QStringLiteral("LeftToolBar"));
        setFixedWidth(40);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        // 靠边最左：整个 widget 0 margin；按钮容器也 0 margin
        auto* mainLay = new QVBoxLayout(this);
        mainLay->setContentsMargins(0, 0, 0, 0);
        mainLay->setSpacing(0);

        // 顶部细分隔线（旧版 renderToolBarSection 的 tb.sep.top）
        m_topLine = new QFrame(this);
        m_topLine->setFrameShape(QFrame::HLine);
        m_topLine->setFixedHeight(1);
        mainLay->addWidget(m_topLine);

        // —— 顶部主工具区：容器 0 margin 实现靠边 ——
        m_topSection = new QWidget(this);
        m_topSection->setContentsMargins(0, 0, 0, 0);
        auto* topLay = new QVBoxLayout(m_topSection);
        topLay->setContentsMargins(4, 6, 4, 0);   // 仅左右给 4px 让 32px 按钮居中在 40px 条里
        topLay->setSpacing(2);
        mainLay->addWidget(m_topSection);

        buildTopButtons(topLay);

        // —— Stretch ——
        mainLay->addStretch(1);

        // —— 分隔线 + 底部折叠区（Layer / Log）——
        m_bottomLine = new QFrame(this);
        m_bottomLine->setFrameShape(QFrame::HLine);
        m_bottomLine->setFixedHeight(1);
        mainLay->addWidget(m_bottomLine);

        m_bottomSection = new QWidget(this);
        auto* botLay = new QVBoxLayout(m_bottomSection);
        botLay->setContentsMargins(4, 4, 4, 4);
        botLay->setSpacing(2);
        mainLay->addWidget(m_bottomSection);

        buildBottomButtons(botLay);

        // 底部展开/收起按钮（旧版 BarCollapseIcon / BarExpandIcon）
        m_expandBtn = makeLeftBtn(this, QStringLiteral("expand"), ltr("Contract"),
            "ExpandDown.svg");
        connect(m_expandBtn, &QToolButton::clicked, this, &LeftToolBar::onExpandToggle);
        mainLay->addWidget(m_expandBtn);

        // 旧版底部固定 37px 留白（spacer2），保证展开/收起按钮不贴底；收起时一起隐藏
        m_bottomSpacer = new QWidget(this);
        m_bottomSpacer->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_bottomSpacer->setFixedHeight(37);
        mainLay->addWidget(m_bottomSpacer);
    }

    void LeftToolBar::onExpandToggle()
    {
        m_bBottomExpanded = !m_bBottomExpanded;
        // 与右侧 RightToolBar 语义一致：只收起底部的 Layer / Log 次级工具区，
        // 顶部绘图工具始终保持可见。
        m_bottomSection->setVisible(m_bBottomExpanded);
        m_bottomLine->setVisible(m_bBottomExpanded);
        if (m_bottomSpacer) m_bottomSpacer->setVisible(m_bBottomExpanded);

        m_expandBtn->setIcon(QIcon(leftIconPath(
            m_bBottomExpanded ? "ExpandDown.svg" : "ExpandUp.svg")));
        m_expandBtn->setToolTip(m_bBottomExpanded ? ltr("Contract") : ltr("Expand"));
    }

    // —— 顶部工具按钮区：Source LeftToolBar 完整移植 ——
    void LeftToolBar::buildTopButtons(QVBoxLayout* lay)
    {
        // ===== 1. Select / Pan =====
        // Select 绑定共享命令动作：勾选/置灰/图标全部由命令状态驱动，与菜单同一份状态。
        auto* selectBtn = makeLeftBtn(m_topSection, "select", ltr("Select"),
            "SelectTool.svg", true);
        if (QAction* selectAction = m_host ? m_host->commandAction(QStringLiteral("draw.select")) : nullptr)
            selectBtn->setDefaultAction(selectAction);
        else
            selectBtn->setChecked(true);
        lay->addWidget(selectBtn);

        // Pan 暂时没有对应命令，保持本地独占；后续补命令后再改为动作驱动。
        auto* panBtn = makeLeftBtn(m_topSection, "pan", ltr("Pan/Drag View"),
            "HandOpenGeneral.svg", true);
        panBtn->setAutoExclusive(true);
        lay->addWidget(panBtn);
        connect(panBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("pan"));
            });

        // ===== 2. Pen 组合按钮（Draw Line / Ellipse / Circle / Polygon / Rectangle / Star / Point）=====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("pen"), ltr("Drawing"), "Pen.svg",
            {
                { "pen.line",      "Draw Line",      "Pen.svg" },
                { "pen.ellipse",   "Draw Ellipse",   "Ellipse.svg" },
                { "pen.circle",    "Draw Circle",    "Circle.svg" },
                { "pen.polygon",   "Draw Polygon",   "Polygon.svg" },
                { "pen.rectangle", "Draw Rectangle", "Rectangle.svg" },
                { "pen.star",      "Draw Star",      "Star.svg" },
                { "pen.point",     "Draw Point",     "Point.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); },
            m_host);

        // ===== 3. Text =====
        auto* textBtn = makeLeftBtn(m_topSection, "text", ltr("Text"), "Text.svg");
        lay->addWidget(textBtn);
        connect(textBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("text"));
            });

        // ===== 4. QR Code 组合按钮 =====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("qrcode"),
            ltr("QR Code") + QStringLiteral("\n")
            + ltr("Input text to generate QR code or SN Code"),
            "QrCode.svg",
            {
                { "qrcode",  "QR Code", "QrCode.svg" },
                { "barcode", "SN Code", "OneDimensionCode.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 5. Art Library =====
        auto* artBtn = makeLeftBtn(m_topSection, "artLibrary", ltr("Art library"),
            "ArtLibrary.svg");
        lay->addWidget(artBtn);
        connect(artBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("artLibrary"));
            });

        // ===== 6. Array 组合按钮 =====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("array"),
            ltr("Array") + QStringLiteral("\n")
            + ltr("Create a array or circular array of the selected items"),
            "RealArray.svg",
            {
                { "array",         "Array",          "RealArray.svg" },
                { "circularArray", "Circular Array", "CircularRow.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 7. Offset =====
        auto* offBtn = makeLeftBtn(m_topSection, "offset", ltr("Offset"),
            "InTheExternalExpansion.svg");
        lay->addWidget(offBtn);
        connect(offBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("offset"));
            });

        // ===== 8. Boolean 组合按钮（Weld / Intersect / Union / SubAB / SubBA）=====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("boolean"),
            ltr("Boolean") + QStringLiteral("\n")
            + ltr("Weld") + QStringLiteral("-") + ltr("Weld all selected items together") + QStringLiteral("\n")
            + ltr("Boolean Intersection") + QStringLiteral("-") + ltr("Boolean Intersection of two shapes") + QStringLiteral("\n")
            + ltr("Boolean Union") + QStringLiteral("-") + ltr("Boolean Union of two shapes") + QStringLiteral("\n")
            + ltr("Boolean Substraction(A-B)") + QStringLiteral("-") + ltr("Subtract B from A") + QStringLiteral("\n")
            + ltr("Boolean Substraction(B-A)") + QStringLiteral("-") + ltr("Subtract A from B"),
            "BooleanSumOperation.svg",
            {
                { "bool.weld",      "Weld",                     "FusionWelding.svg" },
                { "bool.intersect", "Boolean Intersection",     "BooleanAndOperations.svg" },
                { "bool.union",     "Boolean Union",            "BooleanSumOperation.svg" },
                { "bool.subAB",     "Boolean Subtraction(A-B)", "BooleanSubtractionAB.svg" },
                { "bool.subBA",     "Boolean Subtraction(B-A)", "BooleanSubtractionBA.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 9. Node Edit =====
        auto* nodeBtn = makeLeftBtn(m_topSection, "nodeEdit",
            ltr("Node Edit") + QStringLiteral("\n")
            + QStringLiteral("D&Delete-") + ltr("Delete node or line") + QStringLiteral("\n")
            + QStringLiteral("I-") + ltr("Insert node") + QStringLiteral("\n")
            + QStringLiteral("S-") + ltr("Line to curve") + QStringLiteral("\n")
            + QStringLiteral("L-") + ltr("Curve to Line") + QStringLiteral("\n")
            + QStringLiteral("C-") + ltr("Connect two node") + QStringLiteral("\n")
            + QStringLiteral("B-") + ltr("Break node") + QStringLiteral("\n")
            + QStringLiteral("M-") + ltr("Convert Node to Corner"),
            "NodeShows.svg", true);
        nodeBtn->setAutoExclusive(true);
        lay->addWidget(nodeBtn);
        connect(nodeBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("nodeEdit"));
            });

        // ===== 10. Chamfer 组合按钮 =====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("chamfer"),
            ltr("Chamfer") + QStringLiteral("\n")
            + ltr("Click to do round chamfer or linear chamfer"),
            "Chamfer.svg",
            {
                { "chamfer.round",  "Round Chamfer",  "Round.svg" },
                { "chamfer.linear", "Linear Chamfer", "Chamfer.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 11. Start Point =====
        auto* spBtn = makeLeftBtn(m_topSection, "startPoint", ltr("Edit Start Point"),
            "StartingPointToModify.svg");
        lay->addWidget(spBtn);
        connect(spBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("startPoint"));
            });

        // ===== 12. Link 组合按钮 =====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("link"),
            ltr("Link") + QStringLiteral("\n")
            + ltr("Lead Line") + QStringLiteral("-") + ltr("Click on the item to add a lead line") + QStringLiteral("\n")
            + ltr("Bridge") + QStringLiteral("-") + ltr("Click and drag the mouse to set bridge") + QStringLiteral("\n")
            + ltr("Mirco-joint") + QStringLiteral("-") + ltr("Click to add a Mirco-joint") + QStringLiteral("\n")
            + ltr("CoEdge") + QStringLiteral("-") + ltr("Merge adjacent edges of selected shapes into one edge"),
            "Lead.svg",
            {
                { "link.lead",   "Lead Line",   "Lead.svg" },
                { "link.bridge", "Bridge",      "Bridge.svg" },
                { "link.micro",  "Mirco-joint", "MicroEven.svg" },
                { "link.coedge", "CoEdge",      "TotalOfEdge.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 13. Bitmap 组合按钮 =====
        makeLeftMenuBtn(m_topSection, lay, QStringLiteral("bitmap"),
            ltr("Bitmap") + QStringLiteral("\n")
            + ltr("Image Vectorization") + QStringLiteral("-") + ltr("Vectorize image") + QStringLiteral("\n")
            + ltr("Image Trimming") + QStringLiteral("-") + ltr("Trimming image") + QStringLiteral("\n")
            + ltr("Image Processing") + QStringLiteral("-") + ltr("Image processing program"),
            "BitmapEdge.svg",
            {
                { "bmp.vector", "Image Vectorization", "BitmapEdge.svg" },
                { "bmp.trim",   "Image Trimming",      "BitmapClipping.svg" },
                { "bmp.proc",   "Image Processing",    "ImageProcessingRegular.svg" },
            },
            [this](const QString& id) { emit sigToolButtonClicked(id); });

        // ===== 14. Trim =====
        auto* trimBtn = makeLeftBtn(m_topSection, "trim", ltr("Trim"), "Trim.png");
        lay->addWidget(trimBtn);
        connect(trimBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("trim"));
            });
    }

    // —— 底部 Layer / Log ——
    void LeftToolBar::buildBottomButtons(QVBoxLayout* lay)
    {
        m_logBtn = makeLeftBtn(m_bottomSection, "log",
            ltr("Log") + QStringLiteral("\n") + ltr("view log"),
            "Log.svg", true);
        lay->addWidget(m_logBtn);
        connect(m_logBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("log"));
            });

        m_layerBtn = makeLeftBtn(m_bottomSection, "layer",
            ltr("Layer") + QStringLiteral("\n") + ltr("Open and set layer parameters"),
            "Layer.svg", true);
        lay->addWidget(m_layerBtn);
        connect(m_layerBtn, &QToolButton::clicked, this, [this]() {
            emit sigToolButtonClicked(QStringLiteral("layer"));
            });
    }

    void LeftToolBar::setPanelToolActive(const QString& toolId)
    {
        if (m_logBtn) m_logBtn->setChecked(toolId == QLatin1String("log"));
        if (m_layerBtn) m_layerBtn->setChecked(toolId == QLatin1String("layer"));
    }
}  // namespace MS