/**
 * @file AppServices.h
 * @brief 应用服务定位器 - 统一访问全局服务
 *
 * 本类作为依赖注入的入口点，提供对全局服务的统一访问。
 * 新增服务应通过此类提供访问，而非直接使用单例。
 *
 * 使用示例：
 *   // 旧方式（直接单例）
 *   auto theme = ThemeManager::instance()->currentTheme();
 *
 *   // 新方式（通过服务定位器）
 *   auto theme = AppServices::instance().themeManager().currentTheme();
 *
 * 注意：此类采用渐进式迁移策略，现有单例代码暂不强制修改。
 * 新增代码优先使用此类访问服务。
 */
#pragma once

#include "UI/UICommonAPI.h"

#include <QObject>
#include <memory>

// 前向声明
class ThemeManager;
class LanguageManager;

namespace UI
{
    class ThemeManager;  // 在 UI 命名空间中
}

/**
 * @brief 应用服务定位器（单例）
 *
 * 统一管理全局服务的访问点，支持：
 * - 服务的延迟初始化
 * - 服务的替换（用于测试）
 * - 服务的生命周期管理
 */
class UICOMMON_API AppServices final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取全局服务定位器实例
     */
    static AppServices& instance();

    // 禁止拷贝和赋值
    AppServices(const AppServices&) = delete;
    AppServices& operator=(const AppServices&) = delete;

    // ===== 服务访问接口 =====

    /**
     * @brief 获取主题管理器
     *
     * 注意：目前仍使用 ThemeManager::instance() 的方式获取服务。
     * 此方法展示了服务定位器的接口模式。
     */
    UI::ThemeManager* themeManager() const;

    /**
     * @brief 获取语言/国际化管理器
     */
    LanguageManager* languageManager() const;

    // ===== 服务注册接口（用于依赖注入）=====

    /**
     * @brief 注册主题管理器服务
     * @note 保留此接口用于未来 DI 容器集成
     */
    void setThemeManager(std::unique_ptr<UI::ThemeManager> manager);

    /**
     * @brief 注册语言管理器服务
     */
    void setLanguageManager(std::unique_ptr<LanguageManager> manager);

signals:
    /**
     * @brief 服务变更信号
     */
    void serviceChanged(const QString& serviceName, QObject* service);

private:
    explicit AppServices(QObject* parent = nullptr);
    ~AppServices();

    // 私有成员
    std::unique_ptr<UI::ThemeManager> m_themeManager;
    std::unique_ptr<LanguageManager> m_languageManager;
};

