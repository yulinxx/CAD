#include "LeftToolWidget.h"

#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace MS
{
    LeftToolWidget::LeftToolWidget(QWidget* parent)
        : QWidget(parent)
    {
        // 必须与 QSS 里 QWidget#LeftToolWidget 选择器对齐
        setObjectName(QStringLiteral("LeftToolWidget"));
        setAttribute(Qt::WA_StyledBackground);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        hide();  // 默认隐藏：由 LeftSidePanel 的 LeftToolBar 按钮驱动

        m_scrolLayout = new QVBoxLayout(this);
        m_scrolLayout->setContentsMargins(1, 0, 1, 1);
        m_scrolLayout->setSpacing(0);

        m_scroll = new QScrollArea(this);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        m_scroll->setFocusPolicy(Qt::NoFocus);
        m_scroll->setFrameShape(QFrame::NoFrame);

        auto* scrollHost = new QWidget(m_scroll);
        m_scroll->setWidget(scrollHost);

        m_contentLayout = new QVBoxLayout(scrollHost);
        m_contentLayout->setContentsMargins(0, 0, 0, 0);
        m_contentLayout->setSpacing(0);

        m_scrolLayout->addWidget(m_scroll);

        connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() { this->update(); });
        connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
            [this](int value) {
                if (value != 0)
                    m_scroll->horizontalScrollBar()->setValue(0);
            });
    }

    void LeftToolWidget::set_content(QWidget* w, int width /*= s_nMinWidth*/)
    {
        if (!w)
        {
            //throw "can not set nullptr to clear content, please use remove_content()";
            return;
        }

        remove_all_content();

        m_curWidget = w;
        m_contentLayout->addWidget(w);
        w->show();

        m_scroll->verticalScrollBar()->setValue(0);
        show();

        setFixedWidth(width);

        emit sigContentChanged(true);
    }

    void LeftToolWidget::remove_content(QWidget* w)
    {
        if (w && m_curWidget == w)
        {
            remove_all_content();
        }
    }

    void LeftToolWidget::remove_all_content()
    {
        m_preWidget = m_curWidget;
        if (m_curWidget)
        {
            m_contentLayout->removeWidget(m_curWidget);
            m_curWidget->hide();

            m_curWidget = nullptr;
            hide();
        }

        emit sigContentChanged(false);
    }

    QWidget* LeftToolWidget::current_content() const
    {
        return m_curWidget;
    }

    void LeftToolWidget::setPreWidget(QWidget* widget)
    {
        if (m_preWidget && widget != m_preWidget)
        {
            set_content(m_preWidget);
        }
        else
        {
            remove_all_content();
        }
    }
}  // namespace MS