#include "Assembly/Widgets/RegionContainer.h"

#include <QBoxLayout>
#include <QLayoutItem>

namespace RD
{
    RegionContainer::RegionContainer(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("msui.region"));
    }

    RegionContainer::RegionContainer(const QString& regionId, RegionType type, QWidget* parent)
        : QWidget(parent)
        , m_regionId(regionId)
        , m_regionType(type)
    {
        setObjectName(QStringLiteral("msui.region.") + regionId);
    }

    RegionContainer::~RegionContainer() = default;

    void RegionContainer::clearPaneAreas()
    {
        m_panels.clear();
        // 中央控件挂在子区域（或本容器）下，随下面的 delete 一起销毁；先清指针避免悬空。
        m_centerWidget = nullptr;

        if (m_paneAreaLayout)
        {
            // 先逐个移除并销毁控件，再删除布局本身，避免控件仍挂在布局上被二次释放。
            while (QLayoutItem* item = m_paneAreaLayout->takeAt(0))
            {
                if (QWidget* widget = item->widget())
                    delete widget;
                delete item;
            }
            delete m_paneAreaLayout;
            m_paneAreaLayout = nullptr;
        }

        m_paneAreas.clear();
    }

    int RegionContainer::indexOfPaneArea(const QString& paneAreaId) const
    {
        // 线性查找保持子区域插入顺序稳定；子区域数量很少，无需额外索引。
        for (int i = 0; i < m_paneAreas.size(); ++i)
        {
            if (m_paneAreas.at(i).id == paneAreaId)
                return i;
        }
        return -1;
    }

    void RegionContainer::apply(const RegionInfo& info)
    {
        // 先清空旧子区域，保证重复 apply 不会残留控件或旧布局。
        clearPaneAreas();

        m_regionId = info.strId;
        m_regionType = info.type;
        setObjectName(QStringLiteral("msui.region.") + info.strId);

        const QBoxLayout::Direction direction =
            isHorizontalStack(info.type) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;

        m_paneAreaLayout = new QBoxLayout(direction, this);
        m_paneAreaLayout->setContentsMargins(0, 0, 0, 0);
        m_paneAreaLayout->setSpacing(2);

        // 子区域顺序直接采用计划中已排好的顺序，容器侧不再排序，保证装配结果稳定。
        for (const PaneAreaInfo& paneAreaInfo : info.paneAreas)
        {
            auto* paneAreaWidget = new QWidget(this);
            paneAreaWidget->setObjectName(QStringLiteral("msui.area.") + paneAreaInfo.strId);

            auto* paneAreaLayout = new QBoxLayout(direction, paneAreaWidget);
            paneAreaLayout->setContentsMargins(0, 0, 0, 0);
            paneAreaLayout->setSpacing(2);

            if (paneAreaInfo.nWidth > 0)
                paneAreaWidget->setFixedWidth(paneAreaInfo.nWidth);
            if (paneAreaInfo.nHeight > 0)
                paneAreaWidget->setFixedHeight(paneAreaInfo.nHeight);

            m_paneAreaLayout->addWidget(paneAreaWidget, paneAreaInfo.nStretch);

            PaneAreaEntry entry;
            entry.id = paneAreaInfo.strId;
            entry.widget = paneAreaWidget;
            entry.layout = paneAreaLayout;
            m_paneAreas.append(entry);
        }
    }

    void RegionContainer::mountPanel(const PanelInfo& info, QWidget* widget)
    {
        if (!widget)
            return;

        const int index = indexOfPaneArea(info.paneAreaId);
        if (index < 0)
        {
            // 子区域不存在时保留控件但不占位，避免静默丢失业务控件。
            widget->setParent(this);
            widget->hide();
            return;
        }

        PaneAreaEntry& area = m_paneAreas[index];
        widget->setParent(area.widget);
        area.layout->addWidget(widget, 1);
        // 面板自身声明的期望尺寸：覆盖子区域默认布局，0 表示由子区域决定。
        if (info.nWidth > 0)
            widget->setFixedWidth(info.nWidth);
        if (info.nHeight > 0)
            widget->setFixedHeight(info.nHeight);
        // 先落位再按计划设置可见性，避免控件短暂以默认可见状态闪出。
        widget->setVisible(info.bVisible);

        PanelEntry entry;
        entry.id = info.strId;
        entry.paneAreaId = info.paneAreaId;
        entry.widget = widget;
        m_panels.append(entry);

        emit sigPanelVisibilityChanged(info.strId, info.bVisible);
    }

    bool RegionContainer::unmountPanel(const QString& panelId)
    {
        for (int i = 0; i < m_panels.size(); ++i)
        {
            if (m_panels.at(i).id != panelId)
                continue;

            QWidget* widget = m_panels.at(i).widget;
            const int index = indexOfPaneArea(m_panels.at(i).paneAreaId);
            // 先从布局摘除再销毁控件，避免布局持有悬空指针。
            if (index >= 0 && widget)
                m_paneAreas[index].layout->removeWidget(widget);

            m_panels.removeAt(i);
            delete widget;

            emit sigPanelVisibilityChanged(panelId, false);
            return true;
        }
        return false;
    }

    bool RegionContainer::isPanelMounted(const QString& panelId) const
    {
        for (const PanelEntry& entry : m_panels)
        {
            if (entry.id == panelId)
                return true;
        }
        return false;
    }

    QStringList RegionContainer::panelIds() const
    {
        // 按挂载顺序返回，便于上层做无序遍历时保持可预期的结果。
        QStringList ids;
        for (const PanelEntry& entry : m_panels)
            ids.append(entry.id);
        return ids;
    }

    QString RegionContainer::paneAreaIdForPanel(const QString& panelId) const
    {
        for (const PanelEntry& entry : m_panels)
        {
            if (entry.id == panelId)
                return entry.paneAreaId;
        }
        return QString();
    }

    QWidget* RegionContainer::panelWidget(const QString& panelId) const
    {
        for (const PanelEntry& entry : m_panels)
        {
            if (entry.id == panelId)
                return entry.widget;
        }
        return nullptr;
    }

    QWidget* RegionContainer::paneAreaWidget(const QString& paneAreaId) const
    {
        const int index = indexOfPaneArea(paneAreaId);
        return index >= 0 ? m_paneAreas.at(index).widget : nullptr;
    }

    void RegionContainer::setCenterWidget(QWidget* widget)
    {
        if (!widget || widget == m_centerWidget)
            return;

        // 同一区域只保留一个中央控件：先销毁旧的，再放新的（语言切换替换标题栏/菜单栏）。
        clearCenterWidget();
        m_centerWidget = widget;

        if (m_paneAreas.isEmpty())
        {
            // 没有子区域时直接挂到区域容器上，保证业务控件仍能显示。
            widget->setParent(this);
            widget->show();
            return;
        }

        // 中央控件固定落到第一个子区域，与计划中该区域的子区域顺序约定一致。
        PaneAreaEntry& area = m_paneAreas.first();
        widget->setParent(area.widget);
        area.layout->addWidget(widget, 1);
        widget->show();
    }

    void RegionContainer::clearCenterWidget()
    {
        if (!m_centerWidget)
            return;

        // delete 会自动把它从子区域布局中摘除；只删中央控件，面板不受影响。
        delete m_centerWidget;
        m_centerWidget = nullptr;
    }

    QWidget* RegionContainer::takeCenterWidget()
    {
        QWidget* widget = m_centerWidget;
        if (!widget)
            return nullptr;

        // 先移出布局再断父，保证旧布局不会再持有该控件。
        if (QWidget* oldParent = widget->parentWidget())
        {
            if (QLayout* oldLayout = oldParent->layout())
                oldLayout->removeWidget(widget);
        }
        widget->setParent(nullptr);
        m_centerWidget = nullptr;
        return widget;
    }
}  // namespace RD