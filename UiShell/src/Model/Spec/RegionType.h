#pragma once

#include <QString>

namespace RD
{
    // 固定语义区域。区域类型是产品语义，不随客户变化；
    // 客户可配置的是区域顺序、可见性与子区域/面板归属。
    enum class RegionType
    {
        TitleBar,
        MenuBar,
        TopBar,
        LeftDock,
        CenterView,
        RightDock,
        StatusBar
    };

    // Ui区域类型
    inline bool getRegionType(const QString& name, RegionType* out)
    {
        if (!out)
            return false;

        if (name == QLatin1String("TitleBar"))
        {
            *out = RegionType::TitleBar;
            return true;
        }
        if (name == QLatin1String("MenuBar"))
        {
            *out = RegionType::MenuBar;
            return true;
        }
        if (name == QLatin1String("TopBar"))
        {
            *out = RegionType::TopBar;
            return true;
        }
        if (name == QLatin1String("LeftDock"))
        {
            *out = RegionType::LeftDock;
            return true;
        }
        if (name == QLatin1String("CenterView"))
        {
            *out = RegionType::CenterView;
            return true;
        }
        if (name == QLatin1String("RightDock"))
        {
            *out = RegionType::RightDock;
            return true;
        }
        if (name == QLatin1String("StatusBar"))
        {
            *out = RegionType::StatusBar;
            return true;
        }
        return false;
    }

    // 是否属于顶部组（标题栏/菜单栏/工具条），布局时按组做垂直堆叠。
    inline bool isTopGroup(RegionType type)
    {
        return ((type == RegionType::TitleBar) || (type == RegionType::MenuBar) || (type == RegionType::TopBar));
    }

    // 是否属于中部组（左停靠/中央视图/右停靠），布局时按水平排列。
    inline bool isMiddleGroup(RegionType type)
    {
        return ((type == RegionType::LeftDock) || (type == RegionType::CenterView) || (type == RegionType::RightDock));
    }

    // 是否属于底部组（状态栏）。
    inline bool isBottomGroup(RegionType type)
    {
        return type == RegionType::StatusBar;
    }

    // 区域内部子区域是否横向排列。
    inline bool isHorizontalStack(RegionType type)
    {
        return (isTopGroup(type) || isBottomGroup(type));
    }
}  // namespace RD
