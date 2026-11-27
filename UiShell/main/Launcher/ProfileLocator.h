#pragma once

#include <QString>
#include <QStringList>

// Profile 的来源，按查找优先级排列。
enum class ProfileSource
{
    None,      // 未找到
    AppDir,    // <exe 目录>/profile/ui.json）
    Resource   //  :/UiCustomerX/profile/ui.json
};

struct ProfileInfo
{
    QString path;                                // 选中的 Profile
    ProfileSource source = ProfileSource::None;
    QStringList tried;                           // 查过的路径，用于报错
    QString error;                               // 未找到时的错误信息

    bool isValid() const
    {
        return !path.isEmpty();
    }
};

// 查找顺序：
//   1. <appDir>/profile/ui.json
//   2. 客户模块提供的资源
class ProfileLocator
{
public:
    static ProfileInfo locate(const QString& appDir, const QString& path);

    // <appDir>/profile/ui.json
    static QString deployedPath(const QString& appDir);

    // 日志用的来源名：appDir / resource / none
    static QString sourceName(ProfileSource source);
};
