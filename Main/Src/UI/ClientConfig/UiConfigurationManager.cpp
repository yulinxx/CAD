#include "UiConfigurationManager.h"

#include "UiClientContext.h"

#include "Log/SyLogger.h"

UiConfigurationManager::UiConfigurationManager() = default;

UiConfigurationManager::~UiConfigurationManager() = default;

UiConfigurationManager& UiConfigurationManager::shared()
{
    static UiConfigurationManager manager;
    // 懒加载：首次访问时按 UiClientContext 解析出的资源路径加载。
    // 用 Fallback 策略是刻意的——配置驱动已是唯一 UI 构建路径，
    // 客户 JSON 损坏时必须退到默认客户配置，否则会构建出空窗口。
    if (!manager.hasConfig())
    {
        const QString resourcePath = UiClientContext::instance().configResourcePath();
        if (!manager.applyConfiguration(resourcePath, ConfigFallbackPolicy::Fallback))
        {
            SY_ERRORF("[UiConfigurationManager] Shared config unavailable, resource=%s", qPrintable(resourcePath));
        }
    }
    return manager;
}


bool UiConfigurationManager::applyConfiguration(const QString& resourcePath, ConfigFallbackPolicy fallback)
{
    UiConfigLoader loader(resourcePath);
    auto config = loader.load();
    if (config)
    {
        m_configData = std::make_unique<UiConfigData>(std::move(*config));
        SY_INFOF("[UiConfigurationManager] Config loaded: %s (menus=%zu, toolbars=%zu, docks=%zu)",
            qPrintable(resourcePath),
            m_configData->menus.size(),
            m_configData->toolBars.size(),
            m_configData->docks.size());
        return true;
    }

    // 加载失败，按策略处理
    if (fallback == ConfigFallbackPolicy::Fallback && !resourcePath.endsWith(QStringLiteral("/san_yi.json")))
    {
        SY_WARNF("[UiConfigurationManager] Loading '%s' failed, falling back to san_yi.json", qPrintable(resourcePath));
        UiConfigLoader fallbackLoader(QStringLiteral(":/configs/san_yi.json"));
        auto fallbackConfig = fallbackLoader.load();
        if (fallbackConfig)
        {
            m_configData = std::make_unique<UiConfigData>(std::move(*fallbackConfig));
            return true;
        }
    }

    SY_ERRORF("[UiConfigurationManager] Failed to load config: %s (%s)",
        qPrintable(resourcePath),
        qPrintable(loader.lastError()));
    m_configData.reset();
    return false;
}

void UiConfigurationManager::reset()
{
    m_configData.reset();
    m_panelRegistry.reset();
}