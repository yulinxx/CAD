#pragma once

namespace AA
{
    // 版本
    inline constexpr int kProfileVersion = 2;

    // 解析与装配的结构上限，防异常配置
    namespace ProfileLimits
    {
        inline constexpr int maxProfileBytes = 4 * 1024 * 1024;  // 4 Mb
        inline constexpr int maxRegions = 64;
        inline constexpr int maxPaneAreasPerRegion = 64;
        inline constexpr int maxPanels = 512;
        inline constexpr int maxTopLevelMenus = 64;
        inline constexpr int maxMenuDepth = 16;    // 顶层节点深度为 0
        inline constexpr int maxMenuNodes = 1024;  // 所有菜单节点总和
    }  // namespace ProfileLimits
}  // namespace AA
