#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "i18n/TextContext.h"
#include "UiShell/i18n/IQtTextProvider.h"
#include "UiShell/i18n/ITranslationService.h"

using namespace RD;

namespace
{
    // CMake 注入的 .qm 输出目录，避免测试依赖当前工作目录。
    QString translationsDirectory()
    {
        return QString::fromUtf8(UI_TEST_TRANSLATIONS_DIR);
    }
} // namespace

class TranslationServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void normalizesLanguageIds();
    void builtinLanguagesCoverRequestedSet();
    void contextComesFromKeyPrefix();
    void englishTranslatorProvidesEnglishText();
    void missingKeyReturnsKey();
    void unknownLanguageFallsBackToEnglish();
    void switchingLanguageChangesText();
    void uninstallFallsBackToKey();
    void customerPanelTextFollowsLanguage();
    void titleBarTooltipFollowsLanguage();
    void japaneseTranslationIsProvided();
    void goodLaserTitleResolves();

private:
    // 通过导出面取得实现，测试与宿主走同一条工厂路径。
    ApiPtr<ITranslationService> m_service;
};

void TranslationServiceTest::initTestCase()
{
    m_service.reset(createTranslationService());
    QVERIFY2(m_service, "createTranslationService() returned nullptr");
}

// 每个用例结束后卸下 translator，避免全局状态污染后续用例。
void TranslationServiceTest::cleanup()
{
    m_service->uninstall();
}

// 语言 id 统一大小写与分隔符：各语言前缀归并，空值表示跟随系统，未知语言保持原样。
void TranslationServiceTest::normalizesLanguageIds()
{
    QCOMPARE(m_service->normalizeLanguageId("en").toStdString(), std::string("en_US"));
    QCOMPARE(m_service->normalizeLanguageId("en-US").toStdString(), std::string("en_US"));
    QCOMPARE(m_service->normalizeLanguageId("EN_us").toStdString(), std::string("en_US"));
    QCOMPARE(m_service->normalizeLanguageId("zh-Hans").toStdString(), std::string("zh_CN"));
    QCOMPARE(m_service->normalizeLanguageId("ja-JP").toStdString(), std::string("ja_JP"));
    QCOMPARE(m_service->normalizeLanguageId("KO_kr").toStdString(), std::string("ko_KR"));
    QCOMPARE(m_service->normalizeLanguageId("ru").toStdString(), std::string("ru_RU"));
    QCOMPARE(m_service->normalizeLanguageId("de-AT").toStdString(), std::string("de_DE"));
    // 空值不再硬编码为中文，而是交给 defaultLanguage() 跟随系统。
    QCOMPARE(m_service->normalizeLanguageId("  ").toStdString(), std::string());
    QCOMPARE(m_service->normalizeLanguageId("fr_FR").toStdString(), std::string("fr_FR"));
}

// 内置语言应覆盖需求中的全部语种，且英文排在首位作为默认回退。
void TranslationServiceTest::builtinLanguagesCoverRequestedSet()
{
    const QStringList languages = m_service->builtinLanguages();
    QCOMPARE(languages.size(), 6);
    QCOMPARE(languages.first().toStdString(), std::string("en_US"));

    for (const char* id : { "en_US", "zh_CN", "ja_JP", "ko_KR", "ru_RU", "de_DE" })
        QVERIFY2(languages.contains(QString::fromLatin1(id)), id);

    QVERIFY(m_service->isSupported(QStringLiteral("zh_CN")));
    QVERIFY(!m_service->isSupported(QStringLiteral("fr_FR")));
    // 系统语言无论是什么，defaultLanguage() 必须落在受支持集合内。
    QVERIFY(m_service->isSupported(m_service->defaultLanguage()));
}

// profile 的稳定 key 按前缀落到不同 context，便于译者按模块浏览。
void TranslationServiceTest::contextComesFromKeyPrefix()
{
    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QVERIFY2(text, "createQtTextProvider() returned nullptr");

    QCOMPARE(textContextForKey(QStringLiteral("menu.file")).toStdString(),
        std::string("menu"));
    QCOMPARE(textContextForKey(QStringLiteral("mantisoft.title")).toStdString(),
        std::string("mantisoft"));
    QCOMPARE(textContextForKey(QStringLiteral("command.file.new")).toStdString(),
        std::string("command"));
    QCOMPARE(textContextForKey(QStringLiteral("plain")).toStdString(),
        std::string("Profile"));
}

// 英文 translator 装载后，profile key 应返回英文。
void TranslationServiceTest::englishTranslatorProvidesEnglishText()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("en_US"));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("File"));
    QCOMPARE(text->text(QStringLiteral("mantisoft.title")).toStdString(),
        std::string("MantiSoft · Manufacturing Workbench"));
    QCOMPARE(text->text(QStringLiteral("command.file.new")).toStdString(), std::string("New"));
}

// 翻译文件里没有的 key 返回 key 本身，便于发现漏译且不会显示空白。
void TranslationServiceTest::missingKeyReturnsKey()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("no.such.key")).toStdString(),
        std::string("no.such.key"));
}

// 请求不支持的语言：回退英文，返回 false 并给出可读原因。
void TranslationServiceTest::unknownLanguageFallsBackToEnglish()
{
    QVERIFY(!m_service->install(QStringLiteral("fr_FR"), translationsDirectory()));
    QCOMPARE(m_service->requestedLanguage().toStdString(), std::string("fr_FR"));
    QCOMPARE(m_service->language().toStdString(), std::string("en_US"));
    QVERIFY(!m_service->lastError().isEmpty());

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("File"));
}

// 运行期切换必须真的卸下旧 translator：同一 provider 先英文后中文。
void TranslationServiceTest::switchingLanguageChangesText()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("File"));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("zh_CN"));
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("文件"));
}

// 卸载后没有 translator，Qt 直接返回 source（即 key）。
void TranslationServiceTest::uninstallFallsBackToKey()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));

    m_service->uninstall();
    QVERIFY(!m_service->isInstalled());
    QVERIFY(m_service->language().isEmpty());

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("menu.file"));
}

// 客户面板里的代码文案同样走 translator，不能出现“切英文仍是中文”。
void TranslationServiceTest::customerPanelTextFollowsLanguage()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(QCoreApplication::translate("MantiSoft", "MantiSoft top toolbar").toStdString(),
        std::string("MantiSoft top toolbar"));
    QCOMPARE(QCoreApplication::translate("TyvokStudio", "Outline Layer").toStdString(),
        std::string("Outline Layer"));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(QCoreApplication::translate("MantiSoft", "MantiSoft top toolbar").toStdString(),
        std::string("MantiSoft 顶部工具栏"));
    QCOMPARE(QCoreApplication::translate("TyvokStudio", "Outline Layer").toStdString(),
        std::string("轮廓层"));
}

// 标题栏按钮提示也已纳入翻译。
void TranslationServiceTest::titleBarTooltipFollowsLanguage()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(QCoreApplication::translate("TitleBar", "Minimize").toStdString(),
        std::string("Minimize"));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(QCoreApplication::translate("TitleBar", "Minimize").toStdString(),
        std::string("最小化"));
}

// 新增语种必须真的有 .qm：日文菜单与客户文案都能解析成日文。
void TranslationServiceTest::japaneseTranslationIsProvided()
{
    QVERIFY2(m_service->install(QStringLiteral("ja_JP"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("ja_JP"));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("ファイル"));
    QCOMPARE(QCoreApplication::translate("MantiSoft", "MantiSoft top toolbar").toStdString(),
        std::string("MantiSoft 上部ツールバー"));
}

// 回归：GoodLaser 标题 key 之前漏配小写 goodlaser context，导致标题栏直接显示命令 id。
void TranslationServiceTest::goodLaserTitleResolves()
{
    QVERIFY2(m_service->install(QStringLiteral("en_US"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("goodlaser.title")).toStdString(),
        std::string("GoodLaser · Laser Workbench"));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(text->text(QStringLiteral("goodlaser.title")).toStdString(),
        std::string("GoodLaser · 激光工作台"));
}

QTEST_GUILESS_MAIN(TranslationServiceTest)

#include "tstTranslation.moc"
