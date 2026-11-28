/**
 * @file UiDockIds.h
 * @brief 骨架停靠面板身份常量（SceneDock / PropertiesDock）
 *
 * QDockWidget::objectName 是布局快照恢复、setSkeletonDocksVisible、
 * setSceneDockVisible / setPropertiesDockVisible 识别面板的唯一身份键。
 * 字面量散落会导致改名时静默失效，统一收口到本头。
 */
#pragma once

#include <QString>

namespace UiDockIds
{
/// 场景树停靠面板 objectName（与 JSON docks[].id 一致）
inline constexpr const char* Scene = "SceneDock";
/// 属性面板停靠面板 objectName（与 JSON docks[].id 一致）
inline constexpr const char* Properties = "PropertiesDock";

inline QString sceneQString()
{
    return QString::fromLatin1(Scene);
}

inline QString propertiesQString()
{
    return QString::fromLatin1(Properties);
}
}  // namespace UiDockIds
