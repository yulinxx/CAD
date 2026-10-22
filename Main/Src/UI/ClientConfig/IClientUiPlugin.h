#pragma once

#include <QString>

#include <functional>

class QWidget;

/**
 * @file IClientUiPlugin.h
 * @brief 客户 UI 插件 ABI（Main 侧，随交付包 SDK 提供）
 *
 * 插件只依赖 Qt Widgets 与 C++ 标准库，不链接主 exe：
 *   - 宿主传入 UiPanelRegisterSink，插件调用其虚函数完成注册；
 *   - 插件工厂通过 extern "C" 导出，避免 C++ 名字修饰差异。
 *
 * 同一 DLL 如同时导出 createCommandPlugin，宿主会复用同一个 QLibrary 句柄，
 * 把命令插件注册到现有 CommandPluginRegistry（仅 3D 消费）。
 */
class UiPanelRegisterSink
{
public:
    virtual ~UiPanelRegisterSink() = default;

    /// @param id 面板类型 ID，对应客户 JSON 中 dock/panel 的 widgetType
    /// @param factory 面板工厂；parent 由布局构建器传入
    virtual void registerPanel(const QString& id, std::function<QWidget*(QWidget* parent)> factory) = 0;
};

class IClientUiPlugin
{
public:
    virtual ~IClientUiPlugin() = default;

    /// 插件唯一 ID（日志与诊断用）
    virtual QString id() const = 0;

    /// 归属客户 ID；空串表示所有客户可用
    virtual QString clientId() const
    {
        return {};
    }

    /// 注册客户面板；未使用的插件可保持空实现
    virtual void registerPanels(UiPanelRegisterSink& sink)
    {
        Q_UNUSED(sink);
    }
};

/// 插件 DLL 必须导出的工厂函数
extern "C" typedef IClientUiPlugin* (*CreateClientUiPluginFn)();
extern "C" typedef void (*DestroyClientUiPluginFn)(IClientUiPlugin*);

#if defined(_WIN32)
    #define SANYI_CLIENT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
    #define SANYI_CLIENT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// ABI 版本：接口发生破坏性变更时递增，打包脚本写入 abi.txt 供宿主校验
#define SANYI_CLIENT_UI_PLUGIN_ABI 1
