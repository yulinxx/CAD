#pragma once

#include <QString>

/**
 * @file UiThemeService.h
 * @brief 主题服务接口定义
 *
 * 定义了 UI 主题服务接口，负责管理应用程序的主题样式。
 */

 /**
  * @class UiThemeService
  * @brief 主题服务抽象接口
  *
  * 提供主题加载和样式表管理功能，支持系统、浅色、深色、蓝色等主题。
  */
class UiThemeService
{
public:
    virtual ~UiThemeService() = default;

    /// 根据主题 ID 加载主题
    /// @param themeId 主题标识符（system/light/dark/blue）
    /// @return 是否加载成功
    virtual bool loadThemeFromId(const QString& themeId) = 0;

    /// 获取当前样式表
    /// @return 当前主题的样式表内容
    virtual QString styleSheet() const = 0;

    /// 获取当前主题 ID
    /// @return 当前主题标识符
    virtual QString currentThemeId() const = 0;
};

/**
 * @class DefaultUiThemeService
 * @brief 默认主题服务实现
 *
 * 内置四种主题样式：系统、浅色、深色、蓝色。
 * 使用内联样式表实现主题切换。
 */
class DefaultUiThemeService final : public UiThemeService
{
public:
    bool loadThemeFromId(const QString& themeId) override;
    QString styleSheet() const override;
    QString currentThemeId() const override;

private:
    /// 当前主题 ID
    QString m_currentThemeId;
    /// 当前样式表内容
    QString m_styleSheet;
};
