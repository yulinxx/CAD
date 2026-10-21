/**
 * @file BrandConfigTests.cpp
 * @brief 白标品牌层：brand.json 解析、资源查找、client.id 解析
 */

#include <gtest/gtest.h>

#include "UI/ClientConfig/UiBrandConfig.h"
#include "UI/ClientConfig/UiBrandManager.h"
#include "UI/ClientConfig/UiClientContext.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace
{
UiBrandConfig parse(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return UiBrandConfig::fromJson(doc.object(), QStringLiteral("fallback_client"));
}
}  // namespace

TEST(UiBrandConfigTest, ParsesCoreFields)
{
    const UiBrandConfig config = parse(R"({
        "clientId": "acme",
        "productName": "AcmeCut",
        "companyName": "Acme Technologies",
        "appId": "AcmeCutApp",
        "organizationDomain": "acme.example",
        "windowTitle": "AcmeCut Pro",
        "theme": "DARK",
        "splashDurationMs": 1200
    })");

    EXPECT_TRUE(config.isValid());
    EXPECT_EQ(config.clientId, QStringLiteral("acme"));
    EXPECT_EQ(config.productName, QStringLiteral("AcmeCut"));
    EXPECT_EQ(config.effectiveWindowTitle(), QStringLiteral("AcmeCut Pro"));
    EXPECT_EQ(config.effectiveAppId(), QStringLiteral("AcmeCutApp"));
    EXPECT_EQ(config.theme, QStringLiteral("dark"));
    EXPECT_EQ(config.splashDurationMs, 1200);
}

TEST(UiBrandConfigTest, FallsBackToClientIdAndDefaults)
{
    const UiBrandConfig config = parse(R"({ "productName": "OrionCAD" })");

    EXPECT_TRUE(config.isValid());
    EXPECT_EQ(config.clientId, QStringLiteral("fallback_client"));
    EXPECT_EQ(config.effectiveWindowTitle(), QStringLiteral("OrionCAD"));
    EXPECT_EQ(config.effectiveAppId(), QStringLiteral("OrionCAD"));
    EXPECT_TRUE(config.effectiveCopyright().contains(QStringLiteral("OrionCAD")));
}

TEST(UiBrandConfigTest, RejectsUnknownTheme)
{
    const UiBrandConfig config = parse(R"({
        "productName": "AcmeCut",
        "theme": "neon"
    })");

    EXPECT_TRUE(config.theme.isEmpty());
    EXPECT_TRUE(UiBrandConfig::isValidThemeName(QStringLiteral("high_contrast")));
    EXPECT_FALSE(UiBrandConfig::isValidThemeName(QStringLiteral("neon")));
}

TEST(UiBrandManagerTest, LoadsEmbeddedClientBrand)
{
    auto& manager = UiBrandManager::instance();
    ASSERT_TRUE(manager.loadForClient(QStringLiteral("client_a")));

    EXPECT_TRUE(manager.hasBrandFile());
    EXPECT_EQ(manager.clientId(), QStringLiteral("client_a"));
    EXPECT_EQ(manager.productName(), QStringLiteral("AcmeCut"));
    EXPECT_EQ(manager.windowTitle(), QStringLiteral("AcmeCut - Professional Cutting Suite"));
    EXPECT_EQ(manager.config().theme, QStringLiteral("dark"));

    UiClientContext::instance().setClientIdOverride(QString());
}

TEST(UiClientContextTest, ConfigCandidatesPreferExternalThenResource)
{
    const QStringList candidates = UiClientContext::configCandidatePaths(QStringLiteral("client_a"));

    ASSERT_GE(candidates.size(), 3);
    EXPECT_EQ(candidates.constLast(), QStringLiteral(":/configs/client_a.json"));
    EXPECT_TRUE(candidates.at(0).endsWith(QStringLiteral("/ui/client_a.json")));
    EXPECT_TRUE(candidates.at(1).endsWith(QStringLiteral("/configs/client_a.json")));
}

TEST(UiClientContextTest, ReadsClientIdFileWithBomAndWhitespace)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("client.id"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("\xEF\xBB\xBF  client_b  \r\n");
    file.close();

    EXPECT_EQ(UiClientContext::readClientIdFile(dir.path()), QStringLiteral("client_b"));
}
