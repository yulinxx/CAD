#pragma once

#include <QString>

class QWidget;

/**
 * @brief 许可引导类
 *
 * 封装试用期检查、许可证验证和激活流程。
 * 从 CADApplicationRuntime::run() 提取，使运行时类专注于应用生命周期管理。
 */
class LicenseBootstrap
{
public:
    /**
     * @brief 执行许可引导流程
     * @param configDir 配置目录路径
     * @param parentWidget 父窗口（用于显示对话框）
     * @return 0 表示成功继续，负值表示退出（-1: 试用期过期用户退出，-3: 许可证被拒绝）
     */
    static int run(const QString& configDir, QWidget* parentWidget = nullptr);
};
