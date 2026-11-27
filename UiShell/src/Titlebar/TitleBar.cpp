#include "TitleBar.h"

#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace RD
{
    namespace
    {
        const int kDefaultHeight = 36;
        const int kMinHeight = 24;
        const int kMaxHeight = 96;

        // 按钮字形用 Unicode 字符，避免额外图标资源
        QString glyphForButton(const QString& buttonId, bool maximized)
        {
            if (buttonId == QLatin1String("minimize"))
                return QStringLiteral("\u2212");  // 最小化：−

            if (buttonId == QLatin1String("maximize"))
                return maximized ? QStringLiteral("\u2750") : QStringLiteral("\u25A1");  // 还原 / 最大化

            return QStringLiteral("\u2715");                                             // 关闭：✕
        }

        // 提示文本随最大化状态切换，统一用 TitleBar 作为翻译上下文
        QString tooltipForButton(const QString& buttonId, bool maximized)
        {
            if (buttonId == QLatin1String("minimize"))
                return QCoreApplication::translate("TitleBar", "Minimize");  // 最小化

            if (buttonId == QLatin1String("maximize"))
                return maximized ? QCoreApplication::translate("TitleBar", "Restore")    // 还原
                : QCoreApplication::translate("TitleBar", "Maximize");  // 最大化

            return QCoreApplication::translate("TitleBar", "Close");                     // 关闭
        }

        // 按钮 objectName 前缀，便于样式表与自动化测试定位
        QString buttonObjectName(const QString& buttonId)
        {
            return QStringLiteral("msui.titlebar.button.") + buttonId;
        }
    }  // namespace

    TitleBar::TitleBar(const TitleBarSpec& spec, ITitleBarHost* host, QWidget* parent)
        : QWidget(parent)
        , m_spec(spec)
        , m_host(host)
    {
        setObjectName(QStringLiteral("msui.titlebar"));

        // 描述对象归一化：类自己兜底缺省值，调用方无需再做校验。
        m_spec.nHeight = (std::max)(kMinHeight, (std::min)(m_spec.nHeight <= 0 ? kDefaultHeight : m_spec.nHeight, kMaxHeight));
        m_title = m_spec.strTitle.trimmed();
        if (m_title.isEmpty())
            m_title = QStringLiteral("UI");

        // 只接受受支持的按钮 id，并去重，保证后续按钮指针一定能被正确归类。
        for (const QString& buttonId : m_spec.listButtons)
        {
            if (buttonId != QLatin1String("minimize") && buttonId != QLatin1String("maximize") &&
                buttonId != QLatin1String("close"))
                continue;

            if (m_buttonIds.contains(buttonId))
                continue;

            m_buttonIds.append(buttonId);
        }

        setFixedHeight(m_spec.nHeight);
        buildUi();
        applyAppearance();
        // 构造时宿主可能已处于最大化；setHost() 会立即同步按钮字形。
        setHost(host);
    }

    void TitleBar::buildUi()
    {
        // 左侧留出 10px 与图标对齐，按钮贴右侧且内部无间距，接近系统标题栏视觉。
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 0, 0);
        layout->setSpacing(8);

        if (!m_spec.strIcon.trimmed().isEmpty())
        {
            m_iconLabel = new QLabel(this);
            m_iconLabel->setObjectName(QStringLiteral("msui.titlebar.icon"));
            m_iconLabel->setAlignment(Qt::AlignCenter);

            const QString icon = m_spec.strIcon.trimmed();
            // 优先把 strIcon 当资源（文件路径 / qrc / 主题图标名）加载；
            // 只有资源确实产出了有效的 QPixmap 才用 setPixmap 显示，
            // 否则退回文本——允许调用方直接传一个字形（◆）或纯文本。
            QPixmap pixmap;
            QIcon iconResource(icon);
            if (!iconResource.isNull())
            {
                const int iconSize = (std::max)(12, (std::min)(16, m_spec.nHeight - 8));
                pixmap = iconResource.pixmap(iconSize, iconSize);
            }
            if (pixmap.isNull())
            {
                // QIcon 本身可能非 null 但没有可用的图形（例如字符串既不是文件路径
                // 也不是有效的主题图标名），这种情况下退回文本显示。
                m_iconLabel->setText(icon);
            }
            else
            {
                m_iconLabel->setPixmap(pixmap);
            }
            // 图标垂直居中，与菜单、标题对齐。
            layout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);
        }

        // 菜单插槽：位于图标之后、标题之前，空槽位隐藏以免占位。
        m_menuContainer = new QWidget(this);
        m_menuContainer->setObjectName(QStringLiteral("msui.titlebar.menu"));
        m_menuContainer->setProperty("msuiRole", QStringLiteral("titlebarMenuArea"));
        auto* menuLayout = new QHBoxLayout(m_menuContainer);
        menuLayout->setContentsMargins(0, 0, 0, 0);
        menuLayout->setSpacing(0);
        m_menuContainer->hide();
        // 垂直居中而非被拉伸到全高，避免菜单项挤顶
        layout->addWidget(m_menuContainer, 0, Qt::AlignVCenter);

        // 显示标题时占满剩余宽度；隐藏时用弹簧把按钮推右
        if (m_spec.bShowTitle)
        {
            m_titleLabel = new QLabel(m_title, this);
            m_titleLabel->setObjectName(QStringLiteral("msui.titlebar.title"));
            m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            layout->addWidget(m_titleLabel, 1);
        }
        else
        {
            layout->addStretch(1);
        }

        m_buttons = new QWidget(this);
        m_buttons->setObjectName(QStringLiteral("msui.titlebar.buttons"));
        auto* buttonLayout = new QHBoxLayout(m_buttons);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->setSpacing(0);

        for (const QString& buttonId : m_buttonIds)
        {
            // 不获取焦点，避免键盘焦点落在标题栏按钮上打断主窗口交互。
            auto* button = new QToolButton(m_buttons);
            button->setObjectName(buttonObjectName(buttonId));
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setCursor(Qt::PointingHandCursor);
            button->setFixedSize(46, m_spec.nHeight);
            button->setText(glyphForButton(buttonId, false));
            button->setToolTip(tooltipForButton(buttonId, false));
            button->setAccessibleName(button->toolTip());

            // 按钮只把意图交给宿主，不直接操作窗口。
            if (buttonId == QLatin1String("minimize"))
            {
                m_minimizeButton = button;
                connect(button, &QToolButton::clicked, this, [this]() {
                    if (m_host)
                        m_host->requestMinimize();
                    });
            }
            else if (buttonId == QLatin1String("maximize"))
            {
                m_maximizeButton = button;
                connect(button, &QToolButton::clicked, this, [this]() {
                    if (m_host)
                        m_host->requestToggleMaximize();
                    });
            }
            else if (buttonId == QLatin1String("close"))
            {
                m_closeButton = button;
                connect(button, &QToolButton::clicked, this, [this]() {
                    if (m_host)
                        m_host->requestClose();
                    });
            }

            buttonLayout->addWidget(button);
        }

        layout->addWidget(m_buttons, 0);
    }

    void TitleBar::applyAppearance()
    {
        // 静态外观全由皮肤 QSS 按 msuiRole 命中，这里只登记语义角色，
        // profile 给的颜色用 palette 表达；不直接 setStyleSheet
        setProperty("msuiRole", QStringLiteral("titlebar"));

        if (m_titleLabel)
        {
            m_titleLabel->setProperty("msuiRole", QStringLiteral("titlebarText"));
            QFont font = m_titleLabel->font();
            font.setBold(true);
            m_titleLabel->setFont(font);
        }

        if (m_iconLabel)
            m_iconLabel->setProperty("msuiRole", QStringLiteral("titlebarText"));

        if (m_buttons)
            m_buttons->setProperty("msuiRole", QStringLiteral("titlebarButtons"));

        // 只有 profile 显式给合法颜色才覆盖调色板；未配置时跟随皮肤
        const QString backgroundValue = m_spec.strBkgColor.trimmed();
        if (!backgroundValue.isEmpty())
        {
            const QColor background(backgroundValue);
            if (background.isValid())
            {
                m_hasBackground = true;
                QPalette pal = palette();
                pal.setColor(QPalette::Window, background);
                setAutoFillBackground(true);
                setPalette(pal);
            }
        }

        const QString foregroundValue = m_spec.strTextColor.trimmed();
        if (!foregroundValue.isEmpty())
        {
            const QColor foreground(foregroundValue);
            if (foreground.isValid())
            {
                QPalette pal = palette();
                pal.setColor(QPalette::WindowText, foreground);
                setPalette(pal);

                // 子标签显式继承同一调色板，避免 QSS 继承链把颜色重置回皮肤默认值。
                if (m_titleLabel)
                    m_titleLabel->setPalette(pal);

                if (m_iconLabel)
                    m_iconLabel->setPalette(pal);

                // 按钮字形使用 ButtonText；QSS 只画背景/悬停态，颜色由调色板决定。
                if (m_buttons)
                {
                    QPalette buttonPal = m_buttons->palette();
                    buttonPal.setColor(QPalette::ButtonText, foreground);
                    buttonPal.setColor(QPalette::WindowText, foreground);
                    m_buttons->setPalette(buttonPal);

                    for (QToolButton* button : m_buttons->findChildren<QToolButton*>())
                        button->setPalette(buttonPal);
                }
            }
        }

        applyMenuAppearance();
    }

    void TitleBar::setMenuWidget(QWidget* menu)
    {
        if (!m_menuContainer || m_menuWidget == menu)
            return;

        auto* menuLayout = qobject_cast<QBoxLayout*>(m_menuContainer->layout());
        if (menuLayout)
        {
            // 旧菜单归标题栏所有：换绑时直接销毁，避免残留一份不可见的菜单。
            while (QLayoutItem* item = menuLayout->takeAt(0))
            {
                if (QWidget* oldWidget = item->widget())
                    delete oldWidget;
                delete item;
            }
        }

        m_menuWidget = menu;
        if (!menu)
        {
            m_menuContainer->hide();
            return;
        }

        menu->setParent(m_menuContainer);
        if (menuLayout)
            menuLayout->addWidget(menu);
        menu->show();
        m_menuContainer->show();
        applyMenuAppearance();
    }

    void TitleBar::applyMenuAppearance()
    {
        if (!m_menuWidget || !m_menuContainer)
            return;

        // msuiRole 是 QSS 的角色标记。
        m_menuWidget->setProperty("msuiRole", QStringLiteral("titlebarMenu"));

        // 标题栏文字色同时驱动菜单文字色；未配置时沿用当前皮肤调色板。
        const QString foregroundValue = m_spec.strTextColor.trimmed();
        if (!foregroundValue.isEmpty())
        {
            const QColor foreground(foregroundValue);
            if (foreground.isValid())
            {
                QPalette pal = m_menuWidget->palette();
                pal.setColor(QPalette::WindowText, foreground);
                pal.setColor(QPalette::ButtonText, foreground);
                pal.setColor(QPalette::Text, foreground);
                m_menuWidget->setPalette(pal);
            }
        }

        // 动态属性在控件显示后设置不会自动触发重新匹配，需要手动 repolish。
        if (QStyle* widgetStyle = m_menuWidget->style())
        {
            widgetStyle->unpolish(m_menuWidget);
            widgetStyle->polish(m_menuWidget);
        }
        m_menuWidget->update();
    }

    void TitleBar::setMaximized(bool maximized)
    {
        // 状态未变化时不重复设置，避免无谓的样式/重绘开销。
        if (m_maximized == maximized)
            return;

        m_maximized = maximized;
        updateMaximizeButton();
    }

    void TitleBar::updateMaximizeButton()
    {
        if (!m_maximizeButton)
            return;

        m_maximizeButton->setText(glyphForButton(QStringLiteral("maximize"), m_maximized));
        m_maximizeButton->setToolTip(tooltipForButton(QStringLiteral("maximize"), m_maximized));
        m_maximizeButton->setAccessibleName(m_maximizeButton->toolTip());
    }

    void TitleBar::setHost(ITitleBarHost* host)
    {
        m_host = host;
        // 绑定/重建后立刻按宿主真实状态刷新，避免图标停在旧状态。
        setMaximized(host && host->isMaximized());
    }

    void TitleBar::paintEvent(QPaintEvent* event)
    {
        // 皮肤 QSS 一旦命中标题栏，autoFillBackground 就不再保证绘制背景。
        // profile 明确给了 strBkgColor 时由视图自己填充，保证客户配置可见。
        if (m_hasBackground)
        {
            QPainter painter(this);
            painter.fillRect(rect(), palette().color(QPalette::Window));
        }

        QWidget::paintEvent(event);
    }

    void TitleBar::mousePressEvent(QMouseEvent* event)
    {
        // 按下只登记候选拖动，真正拖等阈值超了再交给宿主
        if (m_host && event->button() == Qt::LeftButton)
        {
            m_dragPending = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            m_dragPressGlobal = event->globalPosition().toPoint();
#else
            m_dragPressGlobal = event->globalPos();
#endif
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void TitleBar::mouseMoveEvent(QMouseEvent* event)
    {
        if (m_dragPending && (event->buttons() & Qt::LeftButton))
        {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint delta = event->globalPosition().toPoint() - m_dragPressGlobal;
#else
            const QPoint delta = event->globalPos() - m_dragPressGlobal;
#endif
            if (delta.manhattanLength() <= QApplication::startDragDistance())
            {
                event->accept();
                return;
            }

            m_dragPending = false;

            // 只发意图：系统级移动（含靠边分屏、跨屏 DPI）由宿主/平台层负责。
            m_host->requestDrag();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void TitleBar::mouseReleaseEvent(QMouseEvent* event)
    {
        m_dragPending = false;
        QWidget::mouseReleaseEvent(event);
    }

    void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
    {
        // 双击最大化与按钮同路径，保证两种入口的状态判定一致。
        if (event->button() == Qt::LeftButton && m_host)
        {
            m_host->requestToggleMaximize();
            event->accept();

            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

    ITitleBarView* createTitleBar(const TitleBarSpec& spec, ITitleBarHost* host, QWidget* parent)
    {
        return new TitleBar(spec, host, parent);
    }
}  // namespace RD