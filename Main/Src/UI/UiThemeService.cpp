/**
 * @file UiThemeService.cpp
 * @brief 主题服务实现
 */

#include "UiThemeService.h"

 /// 根据主题 ID 加载对应的样式表
 /// @param themeId 主题标识符（system/light/dark/blue）
 /// @return 是否加载成功
bool DefaultUiThemeService::loadThemeFromId(const QString& themeId)
{
    if (themeId == QStringLiteral("system"))
    {
        m_styleSheet = QStringLiteral("");
    }
    else if (themeId == QStringLiteral("light"))
    {
        m_styleSheet = QStringLiteral(
            "QMainWindow { background-color: #f0f0f0; }"
            "QWidget { color: #333333; }"
            "QToolBar { background-color: #e8e8e8; border: none; }"
            "QDockWidget::title { background-color: #e8e8e8; padding: 4px; }"
            "QStatusBar { background-color: #e8e8e8; }"
        );
    }
    else if (themeId == QStringLiteral("dark"))
    {
        m_styleSheet = QStringLiteral(
            "QMainWindow { background-color: #2d2d2d; }"
            "QWidget { color: #e0e0e0; }"
            "QToolBar { background-color: #3d3d3d; border: none; }"
            "QDockWidget::title { background-color: #3d3d3d; padding: 4px; }"
            "QStatusBar { background-color: #3d3d3d; }"
            "QMenuBar { background-color: #3d3d3d; }"
            "QMenu { background-color: #3d3d3d; }"
        );
    }
    else if (themeId == QStringLiteral("blue"))
    {
        m_styleSheet = QStringLiteral(
            "QMainWindow { background-color: #1a237e; }"
            "QWidget { color: #e3f2fd; }"
            "QToolBar { background-color: #283593; border: none; }"
            "QDockWidget::title { background-color: #283593; padding: 4px; }"
            "QStatusBar { background-color: #283593; }"
            "QMenuBar { background-color: #283593; }"
            "QMenu { background-color: #283593; }"
        );
    }
    else
    {
        return false;
    }

    m_currentThemeId = themeId;
    return true;
}

/// 获取当前样式表
/// @return 当前主题的样式表内容
QString DefaultUiThemeService::styleSheet() const
{
    return m_styleSheet;
}

/// 获取当前主题 ID
/// @return 当前主题标识符
QString DefaultUiThemeService::currentThemeId() const
{
    return m_currentThemeId;
}