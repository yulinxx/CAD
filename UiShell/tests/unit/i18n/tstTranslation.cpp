#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "i18n/TextContext.h"
#include "UiShell/i18n/IQtTextProvider.h"
#include "UiShell/i18n/ITranslationService.h"
#include "UiShell/i18n/LanguageArgument.h"

using namespace AA;

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
    void languageArgumentAcceptsBothForms();
    void builtinLanguagesCoverRequestedSet();
    void shortCodeAliasLoadsLegacyQmFiles();
    void repeatedInstallKeepsSingleTranslationSet();
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
    QCOMPARE(m_service->normalizeLanguageId("es-ES").toStdString(), std::string("es_ES"));
    QCOMPARE(m_service->normalizeLanguageId("pt_BR").toStdString(), std::string("pt_PT"));
    QCOMPARE(m_service->normalizeLanguageId("ar").toStdString(), std::string("ar_AR"));
    QCOMPARE(m_service->normalizeLanguageId("it-IT").toStdString(), std::string("it_IT"));
    QCOMPARE(m_service->normalizeLanguageId("hi-IN").toStdString(), std::string("hi_IN"));
    QCOMPARE(m_service->normalizeLanguageId("tr").toStdString(), std::string("tr_TR"));
    QCOMPARE(m_service->normalizeLanguageId("vi-VN").toStdString(), std::string("vi_VN"));
}

// 入口与运行期共用同一份 --lang 解析：空格与等号写法都必须被识别，
// 否则“显式语言优先”只对其中一种写法成立。
void TranslationServiceTest::languageArgumentAcceptsBothForms()
{
    const QStringList spaced{QStringLiteral("app"), QStringLiteral("--lang"), QStringLiteral("zh_CN")};
    QCOMPARE(AA::requestedLanguageArgument(spaced).toStdString(), std::string("zh_CN"));
    QVERIFY(AA::hasExplicitLanguageArgument(spaced));

    const QStringList joined{QStringLiteral("app"), QStringLiteral("--lang=ja_JP")};
    QCOMPARE(AA::requestedLanguageArgument(joined).toStdString(), std::string("ja_JP"));
    QVERIFY(AA::hasExplicitLanguageArgument(joined));

    const QStringList none{QStringLiteral("app")};
    QVERIFY(AA::requestedLanguageArgument(none).isEmpty());
    QVERIFY(!AA::hasExplicitLanguageArgument(none));

    const QStringList emptyValue{QStringLiteral("app"), QStringLiteral("--lang=")};
    QVERIFY(AA::requestedLanguageArgument(emptyValue).isEmpty());
    QVERIFY(!AA::hasExplicitLanguageArgument(emptyValue));
}

// 内置语言应覆盖需求中的全部语种，且英文排在首位作为默认回退。
void TranslationServiceTest::builtinLanguagesCoverRequestedSet()
{
    const QStringList languages = m_service->builtinLanguages();
    QCOMPARE(languages.size(), 14);
    QCOMPARE(languages.first().toStdString(), std::string("en_US"));

    for (const char* id : { "en_US", "zh_CN", "ja_JP", "ko_KR", "fr_FR", "de_DE",
             "es_ES", "pt_PT", "ru_RU", "ar_AR", "it_IT", "hi_IN", "tr_TR", "vi_VN" })
        QVERIFY2(languages.contains(QString::fromLatin1(id)), id);

    QVERIFY(m_service->isSupported(QStringLiteral("zh_CN")));
    QVERIFY(m_service->isSupported(QStringLiteral("fr_FR")));
    QVERIFY(!m_service->isSupported(QStringLiteral("zz_ZZ")));
    // 系统语言无论是什么，defaultLanguage() 必须落在受支持集合内。
    QVERIFY(m_service->isSupported(m_service->defaultLanguage()));
}

// 历史客户 .qm 用短码命名（April_zh.qm）。安装 locale id 时必须补装短码别名，
// 否则菜单语言切到 zh_CN 后客户自己的文案仍是源语言。
void TranslationServiceTest::shortCodeAliasLoadsLegacyQmFiles()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));

    const QString source = translationsDirectory() + QStringLiteral("/UiShell_zh_CN.qm");
    QVERIFY2(QFile::exists(source), "canonical shell translation is required by this test");
    QVERIFY(QFile::copy(source, dir.path() + QStringLiteral("/UiShell_zh.qm")));
    QVERIFY(QFile::copy(source, dir.path() + QStringLiteral("/Customer_zh.qm")));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), dir.path()), qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("zh_CN"));
    QVERIFY(m_service->isInstalled());
    QCOMPARE(m_service->directory(), dir.path());
}

// 语言切换会反复调用 install()；重复安装同一组文件不能叠加 translator 或改变结果。
void TranslationServiceTest::repeatedInstallKeepsSingleTranslationSet()
{
    const QString dir = translationsDirectory();
    QVERIFY2(m_service->install(QStringLiteral("en_US"), dir), qPrintable(m_service->lastError()));
    QVERIFY2(m_service->install(QStringLiteral("en_US"), dir), qPrintable(m_service->lastError()));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("File"));

    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), dir), qPrintable(m_service->lastError()));
    QVERIFY2(m_service->install(QStringLiteral("zh_CN"), dir), qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("zh_CN"));
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("\u6587\u4ef6"));
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
    QVERIFY(!m_service->install(QStringLiteral("zz_ZZ"), translationsDirectory()));
    QCOMPARE(m_service->requestedLanguage().toStdString(), std::string("zz_ZZ"));
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
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("\u6587\u4ef6"));
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
        std::string("MantiSoft \u9876\u90e8\u5de5\u5177\u680f"));
    QCOMPARE(QCoreApplication::translate("TyvokStudio", "Outline Layer").toStdString(),
        std::string("\u8f6e\u5ed3\u5c42"));
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
        std::string("\u6700\u5c0f\u5316"));
}

// 新增语种必须真的有 .qm：日文菜单与客户文案都能解析成日文。
void TranslationServiceTest::japaneseTranslationIsProvided()
{
    QVERIFY2(m_service->install(QStringLiteral("ja_JP"), translationsDirectory()),
        qPrintable(m_service->lastError()));
    QCOMPARE(m_service->language().toStdString(), std::string("ja_JP"));

    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    QCOMPARE(text->text(QStringLiteral("menu.file")).toStdString(), std::string("\u30d5\u30a1\u30a4\u30eb"));
    QCOMPARE(QCoreApplication::translate("MantiSoft", "MantiSoft top toolbar").toStdString(),
        std::string("MantiSoft \u4e0a\u90e8\u30c4\u30fc\u30eb\u30d0\u30fc"));
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
        std::string("GoodLaser \u00b7 \u6fc0\u5149\u5de5\u4f5c\u53f0"));
}

QTEST_GUILESS_MAIN(TranslationServiceTest)

#include "tstTranslation.moc"
