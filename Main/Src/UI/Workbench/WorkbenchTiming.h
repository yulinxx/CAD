#pragma once

/**
 * @file WorkbenchTiming.h
 * @brief 2D/3D 工作台共享的防抖/节流窗口常量（P2-1 收敛）
 *
 * 场景树重建与属性面板重建在 2D/3D 两侧使用同一组时间窗，
 * 避免魔数各自漂移；语义见《场景树架构定义.md》《属性面板架构定义.md》。
 */
namespace WorkbenchTiming
{
    /// 场景树结构变更防抖窗口（批量增删合并为一次重建，ms）
    inline constexpr int kSceneTreeDebounceMs = 150;
    /// 属性面板重建尾包合并窗口（active 期间多次请求合并为窗口末尾一次，ms）
    inline constexpr int kPropertiesDebounceMs = 100;
}  // namespace WorkbenchTiming
