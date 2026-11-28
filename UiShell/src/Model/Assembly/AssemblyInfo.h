#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "Model/Spec/RegionType.h"
#include "Model/Profile/ProfileModel.h"

namespace AA
{
    // 子区域信息：区域内的一个位置，描述尺寸与伸缩策略
    struct PaneAreaInfo
    {
        QString strId;
        int nOrder = 100;  // 升序排列，数值小的靠前
        int nStretch = 0;  // 布局伸缩因子，0 表示不抢占多余空间
        int nWidth = 0;    // 宽度，0 由布局决定
        int nHeight = 0;   // 高度，0 由布局决定
    };

    // 面板信息：面板子区域，以及工厂创建
    struct PanelInfo
    {
        QString strId;
        QString factory;
        QString paneAreaId;
        int nOrder = 100;
        bool bVisible = true;
        int nWidth = 0;   // 宽度；0 由子区域布局决定
        int nHeight = 0;  // 高度；0 由子区域布局决定
        QJsonObject props;
    };

    // 区域信息：生成的区域及其子区域/面板清单
    struct RegionInfo
    {
        QString strId;
        RegionType type = RegionType::TopBar;
        int nOrder = 100;
        bool bVisible = true;
        QList<PaneAreaInfo> paneAreas;
        QList<PanelInfo> panels;
    };

    // 菜单节点信息：递归树结构，title 为按当前语种解析后的文字
    struct MenuNodeInfo
    {
        MenuNodeType type = MenuNodeType::Command;

        QString strId;          // Submenu 的唯一标识；Builtin 节点的内置菜单 id
        QString strTitleKey;
        QString strTitle;       // 按语言包解析后的文字；未提供语言包时等于 titleKey
        QString strCmdId;       // Command 节点：执行的命令 id
        QString strIconKey;     // 图标 key；由 MenuController 解析成 QIcon
        int nOrder = 100;

        // Command 节点属性
        bool bCheckable = false;
        QString strGroupKey;           // 勾选组：同组内的 checkable 命令互斥
        QStringList requiredFeatures;  // Command 节点需要的全部功能开关

        QList<MenuNodeInfo> children;  // Submenu 的子项
    };

    // 标题栏装配信息：同样是纯数据，widget 由 TitleBar 库按此描述独立构造。
    struct TitleBarInfo
    {
        bool bPresent = false;
        QString strTitleKey;
        QString strTitle;  // 按语言包解析后的文字
        QString strIcon;
        QStringList buttons;
        int nHeight = 36;
        bool bShowTitle = true;
        QString strBkgColor;
        QString strTextColor;
    };

    // 装配信息只含 strId/顺序/策略，不含任何 QWidget/QAction。
    // 它是一次构建的完整结果快照，生成后不再修改；warnings 记录被跳过的配置项。
    struct AssemblyInfo
    {
        QList<RegionInfo> regions;
        QList<MenuNodeInfo> menus;

        // 右键菜单：id → 菜单项树；与 menus 一样只含数据，QMenu 由 MenuController 按共享动作构建。
        QHash<QString, QList<MenuNodeInfo>> contextMenus;

        TitleBarInfo titleBar;

        int nMinWidth = 800;
        int nMinHeight = 600;

        // 是否使用 Windows 原生标题栏
        bool bNativeTitleBar = false;

        // 菜单栏是否交给系统原生菜单栏（macOS 上位于屏幕顶部）。
        // false：菜单栏是窗口内控件，菜单项图标可见、可用 QSS 皮肤。
        bool bNativeMenuBar = false;

        QStringList strListWarnings;

        // 结构指纹：用于判断开关变化能否只刷 Shell
        // 不含 menus —— 菜单由 ShellView::refreshShell() 整条重建，无需重建面板；
        // 不含 strTitle/strIcon/props/strListWarnings —— 不影响控件树形状。
        // 新增 region/area/panel 结构字段时必须同步更新本函数。
        QString structureKey() const
        {
            QString key;
            for (const RegionInfo& region : regions)
            {
                key += QStringLiteral("R/%1|%2|%3;").arg(region.strId).arg(static_cast<int>(region.type)).arg(region.nOrder);

                for (const PaneAreaInfo& area : region.paneAreas)
                    key += QStringLiteral("A/%1|%2|%3;").arg(area.strId).arg(area.nOrder).arg(area.nStretch);

                for (const PanelInfo& panel : region.panels)
                {
                    key += QStringLiteral("P/%1|%2|%3|%4|%5;")
                        .arg(panel.strId, panel.factory, panel.paneAreaId)
                        .arg(panel.nOrder)
                        .arg(panel.bVisible ? 1 : 0);
                }
            }

            key += QStringLiteral("TB/%1|NAT/%2|NMB/%3;")
                .arg(titleBar.bPresent ? 1 : 0)
                .arg(bNativeTitleBar ? 1 : 0)
                .arg(bNativeMenuBar ? 1 : 0);
            return key;
        }
    };
}  // namespace AA
