#pragma once

/**
 * @file ILayoutConfig.h
 * @brief UI 布局配置的只读抽象（P3 边界接口）
 *
 * 消费者（工作台布局/菜单构建等）只依赖本接口读取布局配置，
 * 不依赖具体配置来源（当前实现为 UiConfigurationManager 单例）。
 *
 * 这样"配置从哪来"（JSON 资源 / 文件 / 未来远程下发）与"谁消费配置"解耦：
 * 新增配置来源只需实现本接口，无需改动消费者。
 */

class UiConfigData;
class UiPanelRegistry;

class ILayoutConfig
{
public:
    virtual ~ILayoutConfig() = default;

    /// 已加载的配置数据（未加载时为 nullptr）
    virtual const UiConfigData* configData() const = 0;

    /// 菜单配置数据（当前与主配置同源）
    virtual const UiConfigData* menuConfigData() const = 0;

    /// 当前配置是否已加载
    virtual bool hasConfig() const = 0;

    /// 面板注册表（可为 nullptr）
    virtual UiPanelRegistry* panelRegistry() const = 0;
};
