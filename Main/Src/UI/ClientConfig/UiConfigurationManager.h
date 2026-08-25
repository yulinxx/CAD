#pragma once

/**
 * @file UiConfigurationManager.h
 * @brief 客户化 UI 配置管理器
 *
 * 与 Docs/01-当前架构/UI定制变更设计方案.md 第 6.3 节对应。
 * 多客户 UI 配置的总控点：加载客户配置、持有面板工厂、提供统一配置入口。
 */

#include "UiConfigLoader.h"
#include "UiPanelRegistry.h"

#include <memory>

class QMainWindow;

/// 配置加载失败时的回退策略
enum class ConfigFallbackPolicy
{
    Strict,    // 加载失败直接返回失败（开发阶段）
    Fallback,  // 加载失败回退到默认 san_yi 配置（生产阶段，推荐）
    Silent     // 加载失败使用空配置（极端情况，调用方自行降级）
};

/// UI 配置管理器
class UiConfigurationManager
{
public:
    UiConfigurationManager();
    ~UiConfigurationManager();

    /// 进程级共享实例：客户 UI 配置的唯一事实源（P0-1）
    ///
    /// 历史实现里 WorkbenchMenuManager 与 WorkbenchLayoutManager 各自 new 了一个
    /// UiConfigurationManager 并**分别**加载配置，两份副本一旦解析出不同客户
    /// （菜单读环境变量、布局读编译期宏）就会出现「菜单和布局来自不同客户」。
    /// 现在所有消费方都从这里取配置：首次访问时按 UiClientContext 解析出的
    /// 资源路径加载，之后复用。
    ///
    /// @return 共享实例；配置加载失败时 configData() 为 nullptr，调用方需自行判空
    static UiConfigurationManager& shared();

    /// 加载并应用客户 UI 配置
    /// @param resourcePath 配置路径（Qt 资源或文件）
    /// @param fallback 失败时的回退策略
    /// @return 是否成功
    bool applyConfiguration(const QString& resourcePath, ConfigFallbackPolicy fallback = ConfigFallbackPolicy::Fallback);

    /// 面板注册表
    UiPanelRegistry* panelRegistry() const
    {
        return m_panelRegistry.get();
    }

    /// 已加载的配置数据（未加载时为 nullptr）
    const UiConfigData* configData() const
    {
        return m_configData.get();
    }

    /// 菜单配置数据（当前与主配置共用，便于菜单/工具栏/Dock 同源生成）
    const UiConfigData* menuConfigData() const
    {
        return m_configData.get();
    }

    /// 当前配置是否已加载
    bool hasConfig() const
    {
        return m_configData != nullptr;
    }

    /// 清空已加载配置
    void reset();

private:
    std::unique_ptr<UiConfigData> m_configData;
    std::unique_ptr<UiPanelRegistry> m_panelRegistry;
};
