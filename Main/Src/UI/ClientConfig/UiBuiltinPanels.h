#pragma once

/**
 * @file UiBuiltinPanels.h
 * @brief 内置面板与状态栏槽位的工厂注册入口
 *
 * 存在原因（P0-1/P0-2）：
 *   历史实现中 WorkbenchMenuManager 与 WorkbenchLayoutManager 各自
 *   new 了一个 UiPanelRegistry 并**分别**注册了同一批面板工厂。两处注册表内容
 *   一旦漂移，就会出现「菜单里的 Dock 开关能找到面板、布局构建却找不到」这类
 *   难查的问题。这里把注册逻辑收敛成唯一入口。
 *
 * 客户定制的接入方式：
 *   客户新增面板 / 状态栏指示器时，在自己的注册函数里调用
 *   UiPanelRegistry::registerPanel("MyPanel", factory)，再在客户 JSON 里用
 *   widgetType: "MyPanel" 引用即可，无需改动本文件。
 */

class UiPanelRegistry;

/// 注册全部内置面板工厂（Dock 与状态栏槽位共用同一个注册表）
/// @param registry 目标注册表；重复调用是幂等的（同 id 覆盖）
void registerBuiltinUiPanels(UiPanelRegistry& registry);
