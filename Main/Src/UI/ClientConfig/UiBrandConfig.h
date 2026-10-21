#pragma once

#include <QString>

class QJsonObject;

/**
 * @file UiBrandConfig.h
 * @brief 白标品牌配置：brand.json 的内存模型
 *
 * 与 UiConfig 分离：品牌描述「这个软件叫什么、长什么样」，
 * UiConfig 描述「菜单/工具栏/面板怎么排」。二者按 clientId 关联，
 * 但不参与 extends 合并——品牌是整包替换，不做字段级继承。
 */
struct UiBrandConfig
{
    QString clientId;
    QString productName;
    QString companyName;
    QString appId;
    QString organizationDomain;
    QString version;
    QString windowTitle;
    QString tagline;
    QString copyright;
    QString website;
    QString supportEmail;
    QString aboutText;
    QString appIcon;
    QString logo;
    QString aboutLogo;
    QString splash;
    int splashDurationMs = 800;
    QString iconDirectory;
    QString theme;
    QString styleSheet;

    /// 解析 brand.json；clientId 为空时使用 fallbackClientId
    static UiBrandConfig fromJson(const QJsonObject& root, const QString& fallbackClientId = {});

    /// 必填字段是否齐备（productName 非空即可用）
    bool isValid() const;

    /// 窗口标题：windowTitle > productName
    QString effectiveWindowTitle() const;

    /// 版权行：copyright > "Copyright (C) <year> <companyName>"
    QString effectiveCopyright() const;

    /// 应用标识：appId > 去空格的 companyName+productName > "SanYiCAD"
    QString effectiveAppId() const;

    /// 主题名是否合法（light/dark/blue/slate/high_contrast/system）
    static bool isValidThemeName(const QString& theme);
};
