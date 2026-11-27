#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "UiShell/Assembly/IRegionContainer.h"
#include "Model/Assembly/AssemblyInfo.h"

class QBoxLayout;
class QLayout;

namespace RD
{
    // 面板（TopBar/LeftDock/CenterView/RightDock/StatusBar ...）的容器。
    // 容器只认识 PaneAreaInfo/PanelInfo 与工厂产出的 QWidget，
    // 不感知面板内容，面板也不感知自己落在哪个区域。
    // 具体类不导出：外部通过 IShellRuntime::regionContainer() 取 IRegionContainer*。
    class RegionContainer : public QWidget, public IRegionContainer
    {
        Q_OBJECT

    public:
        explicit RegionContainer(QWidget* parent = nullptr);
        RegionContainer(const QString& regionId, RegionType type, QWidget* parent = nullptr);
        ~RegionContainer() override;

    public:
        QWidget* widget() override
        {
            return this;
        }

        // 按计划重建子区域。会清空已挂载的面板控件。
        // info 提供区域 id、类型与已排序的子区域列表，子区域顺序即最终布局顺序。
        void apply(const RegionInfo& info);

        // 把工厂产出的控件挂到 info.paneAreaId 指定的子区域。
        // 子区域不存在时只隐藏控件、不销毁，避免业务控件被静默丢弃。
        void mountPanel(const PanelInfo& info, QWidget* widget);
        // 卸载并销毁指定面板；返回是否找到该面板。
        bool unmountPanel(const QString& panelId);
        // 判断面板是否已挂载到本区域。
        bool isPanelMounted(const QString& panelId) const override;

        // 区域 strId 与区域类型，供 ShellRuntime 做区域查找和上下中分组。
        QString regionId() const override
        {
            return m_regionId;
        }

        RegionType type() const
        {
            return m_regionType;
        }

        // 已挂载面板 strId 列表，按挂载顺序返回。
        QStringList panelIds() const override;
        // 面板所在子区域 strId；面板未挂载时返回空字符串。
        QString paneAreaIdForPanel(const QString& panelId) const override;
        // 面板控件指针；面板未挂载时返回 nullptr。
        QWidget* panelWidget(const QString& panelId) const override;
        // 子区域控件指针；子区域不存在时返回 nullptr。
        QWidget* paneAreaWidget(const QString& paneAreaId) const override;

        // 供 CenterView / TitleBar 等由业务侧提供控件的区域使用。
        // widget 的所有权转交给本区域首个子区域；无子区域时直接挂在本容器下。
        // 重复调用会先销毁上一个中央控件，避免同一区域残留两份标题栏/菜单栏。
        void setCenterWidget(QWidget* widget);
        // 只销毁由 setCenterWidget 放入的中央控件，保留面板与子区域。
        // 语言切换时用它替换标题栏/菜单栏，而不重建整个区域。
        void clearCenterWidget();
        // 把中央控件从区域中摘出但不销毁，所有权交回调用方。
        // 菜单内嵌到标题栏时用它对菜单栏做“换父”，避免 clearPaneAreas 二次释放。
        QWidget* takeCenterWidget();

    signals:
        // 面板挂载/卸载时发出，visible 表示该面板当前是否可见。
        void sigPanelVisibilityChanged(const QString& panelId, bool visible);

    private:
        // 销毁全部子区域及子区域中的控件，并清空面板记录。
        void clearPaneAreas();
        // 按子区域 strId 查找下标；不存在返回 -1，子区域顺序保持与计划一致。
        int indexOfPaneArea(const QString& paneAreaId) const;

    private:
        struct PaneAreaEntry
        {
            QString id;
            QWidget* widget = nullptr;
            QBoxLayout* layout = nullptr;
        };

        struct PanelEntry
        {
            QString id;
            QString paneAreaId;
            QWidget* widget = nullptr;
        };

        QString m_regionId;
        RegionType m_regionType = RegionType::TopBar;
        QBoxLayout* m_paneAreaLayout = nullptr;
        QList<PaneAreaEntry> m_paneAreas;
        QList<PanelEntry> m_panels;
        // 由 setCenterWidget 放入的控件；clearPaneAreas 会随子区域一起销毁它。
        QWidget* m_centerWidget = nullptr;
    };
}  // namespace RD
