#pragma once

#include "IClientUiPlugin.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QLibrary;
class UiPanelRegistry;

/**
 * @file UiClientPluginHost.h
 * @brief 客户 UI 插件宿主（进程级）
 *
 * 只从以下目录加载：
 *   1. SANYI_CLIENT_PLUGIN_DIR
 *   2. <AppDir>/plugins/clients/<clientId>
 *
 * 插件若同时导出 createCommandPlugin，复用同一个 QLibrary 句柄注册到
 * UICommon 的 CommandPluginRegistry，避免同一 DLL 被加载两次。
 */
class UiClientPluginHost
{
public:
    static UiClientPluginHost& instance();

    /// 加载当前客户目录下的全部插件；重复调用幂等
    int loadForClient(const QString& clientId);

    /// 把已加载插件的面板工厂注册到布局构建器使用的 registry
    void applyPanels(UiPanelRegistry& registry);

    QStringList loadedPluginIds() const;
    QStringList loadedPluginPaths() const;

    int loadedPluginCount() const
    {
        return static_cast<int>(m_plugins.size());
    }

private:
    UiClientPluginHost() = default;

    struct LoadedPlugin
    {
        std::unique_ptr<QLibrary> library;
        IClientUiPlugin* plugin{ nullptr };
        DestroyClientUiPluginFn destroy{ nullptr };
        QString path;
    };

    int loadDirectory(const QString& directory, const QString& clientId);
    bool loadLibrary(const QString& libraryPath, const QString& clientId);

    std::vector<std::unique_ptr<LoadedPlugin>> m_plugins;
    QString m_loadedClientId;
    bool m_loaded{ false };
};
