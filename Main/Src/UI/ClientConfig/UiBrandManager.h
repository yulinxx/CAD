#pragma once

#include "UiBrandConfig.h"

#include <QString>

/**
 * @file UiBrandManager.h
 * @brief 进程级白标品牌加载器
 *
 * 加载时机早于 QApplication：只依赖 argv / 环境变量 / 文件系统，
 * 保证 organizationName / applicationName 在 Qt 创建任何路径前就是客户身份。
 *
 * 查找顺序：
 *   1. SANYI_BRAND_DIR（开发/自动化测试）
 *   2. <exe 目录>/branding/<clientId>/brand.json
 *   3. :/brand/<clientId>/brand.json
 *   4. :/brand/default/brand.json
 *   5. 内置默认（当前 SanYi CAD）
 */
class UiBrandManager
{
public:
    static UiBrandManager& instance();

    /// 解析客户 ID 与品牌文件；argc/argv 可为空（此时只看环境变量与 client.id）
    bool loadForProcess(int argc = 0, char** argv = nullptr);

    /// 直接按客户 ID 加载（测试与工具使用）
    bool loadForClient(const QString& clientId, const QString& explicitBrandDir = {});

    /// 在 QApplication 创建前调用：设置组织名/应用名/版本/域名
    void applyApplicationIdentity() const;

    /// 在 QApplication 创建后调用：窗口图标、默认主题、图标目录、品牌 QSS、qApp 属性
    void applyRuntimeIdentity() const;

    /// 把品牌字段写入 QCoreApplication 动态属性，供 UICommon 的 BrandInfo 读取
    void applyApplicationProperties() const;

    const UiBrandConfig& config() const
    {
        return m_config;
    }

    bool hasBrandFile() const
    {
        return m_hasBrandFile;
    }

    QString clientId() const
    {
        return m_config.clientId;
    }

    QString brandDirectory() const
    {
        return m_brandDirectory;
    }

    /// 解析品牌资源：绝对路径/:/ 原样；相对路径依次查品牌目录、:/brand/<id>、:/brand/default
    QString assetPath(const QString& relative) const;

    QString productName() const;
    QString companyName() const;
    QString windowTitle() const;
    QString copyright() const;

    static QString resolveClientIdFromProcess(int argc, char** argv);
    static QString executableDirectory(int argc, char** argv);

private:
    UiBrandManager() = default;

    bool loadFromFile(const QString& filePath, const QString& clientId);
    void applyFallback(const QString& clientId);
    QString resolveAssetFile(const QString& relative) const;

    UiBrandConfig m_config;
    QString m_brandDirectory;
    bool m_hasBrandFile{ false };
};
