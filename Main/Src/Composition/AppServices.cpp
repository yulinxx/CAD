/**
 * @file AppServices.cpp
 * @brief 应用服务定位器实现
 */
#include "AppServices.h"

#include "UI/ThemeManager.h"
#include "UI/LanguageManager.h"

AppServices& AppServices::instance()
{
    static AppServices instance;
    return instance;
}

AppServices::AppServices(QObject* parent)
    : QObject(parent)
{
}

AppServices::~AppServices() = default;

UI::ThemeManager* AppServices::themeManager() const
{
    // 目前直接返回现有单例，未来可替换为注入的服务
    return UI::ThemeManager::instance();
}

LanguageManager* AppServices::languageManager() const
{
    return LanguageManager::instance();
}

void AppServices::setThemeManager(std::unique_ptr<UI::ThemeManager> manager)
{
    m_themeManager = std::move(manager);
    emit serviceChanged("themeManager", m_themeManager.get());
}

void AppServices::setLanguageManager(std::unique_ptr<LanguageManager> manager)
{
    m_languageManager = std::move(manager);
    emit serviceChanged("languageManager", m_languageManager.get());
}

